#!/usr/bin/env python3
import argparse, json
from pathlib import Path
import numpy as np

ap=argparse.ArgumentParser()
ap.add_argument('gpu')
ap.add_argument('oracle')
ap.add_argument('--width',type=int,required=True)
ap.add_argument('--height',type=int,required=True)
ap.add_argument('--json-out')
a=ap.parse_args()

g=np.memmap(a.gpu,dtype='<f2',mode='r',shape=(a.height,a.width,4))[...,:3]
o=np.load(a.oracle,mmap_mode='r')
if o.shape != (a.height,a.width,3):
    raise SystemExit(f'RCD_FULL_GPU_ORACLE_FAIL oracle_shape={o.shape} expected={(a.height,a.width,3)}')

finite=True; maxv=-1.0; maxpos=None; total=0.0; count=0; vals=[]
cmax=np.zeros(3,np.float64); csum=np.zeros(3,np.float64); ccount=0
for y0 in range(9,a.height-9,128):
    y1=min(a.height-9,y0+128)
    gg=np.asarray(g[y0:y1,9:a.width-9],np.float32)
    oo=np.asarray(o[y0:y1,9:a.width-9],np.float32)
    d=np.abs(gg-oo)
    finite = finite and np.isfinite(gg).all()
    idx=np.unravel_index(np.argmax(d),d.shape); m=float(d[idx])
    if m>maxv:
        maxv=m
        maxpos=(int(y0+idx[0]),int(9+idx[1]),int(idx[2]),float(gg[idx]),float(oo[idx]))
    total+=float(d.sum(dtype=np.float64)); count+=d.size
    cmax=np.maximum(cmax,d.max(axis=(0,1)))
    csum+=d.sum(axis=(0,1),dtype=np.float64); ccount+=d.shape[0]*d.shape[1]
    vals.append(d.reshape(-1)[::97])
sample=np.concatenate(vals)
p99=float(np.quantile(sample,0.99)); p999=float(np.quantile(sample,0.999)); mean=total/count

mask=np.zeros((a.height,a.width),bool)
mask[:9,:]=True; mask[-9:,:]=True; mask[:,:9]=True; mask[:,-9:]=True
bd=np.abs(np.asarray(g[mask],np.float32)-np.asarray(o[mask],np.float32))
bmax=float(bd.max()); bmean=float(bd.mean())

# Diagnose the observed interior/border handoff. Distance is measured to the
# nearest image edge. RCD interior begins at distance 9, so distance=9 is the
# first interior ring directly adjacent to librtprocess's 9-pixel border.
y=np.arange(a.height)[:,None]
x=np.arange(a.width)[None,:]
dist=np.minimum(np.minimum(y,a.height-1-y),np.minimum(x,a.width-1-x))
bands=[('d9',9,10),('d10',10,11),('d11_12',11,13),('d13_16',13,17),('d17_32',17,33),('deep',33,10**9)]
band_metrics={}
for name,lo,hi in bands:
    m=(dist>=lo)&(dist<hi)
    if not np.any(m):
        continue
    # Work in chunks by rows to avoid materializing a full 12.5MP RGB diff.
    s=0.0; n=0; mx=0.0; samples=[]
    for y0 in range(0,a.height,128):
        y1=min(a.height,y0+128)
        mm=m[y0:y1]
        if not np.any(mm): continue
        gg=np.asarray(g[y0:y1],np.float32)[mm]
        oo=np.asarray(o[y0:y1],np.float32)[mm]
        d=np.abs(gg-oo)
        mx=max(mx,float(d.max()))
        s+=float(d.sum(dtype=np.float64)); n+=d.size
        samples.append(d.reshape(-1)[::31])
    sm=np.concatenate(samples) if samples else np.empty(0,np.float32)
    band_metrics[name]={'max':mx,'mean':s/n,'p99_sampled':float(np.quantile(sm,0.99)) if sm.size else 0.0,'pixels':int(np.count_nonzero(m))}


# Focused handoff diagnostic. d8 is the last librtprocess-border ring, d9 the
# first RCD-interior ring, d10 the next interior ring. Report per-channel and
# per-side maxima plus worst coordinates so a systematic handoff issue can be
# distinguished from isolated FP16 amplification.
def focused_ring(di):
    m=(dist==di)
    coords=np.argwhere(m)
    if coords.size==0: return {}
    ch_max=[0.0,0.0,0.0]; ch_mean=[0.0,0.0,0.0]; ch_n=0
    side_max={'top':0.0,'bottom':0.0,'left':0.0,'right':0.0}
    side_n={'top':0,'bottom':0,'left':0,'right':0}
    side_sum={'top':0.0,'bottom':0.0,'left':0.0,'right':0.0}
    worst=(-1.0,None)
    # Ring is only O(perimeter), so direct indexed reads are small and clear.
    for yy,xx in coords:
        gv=np.asarray(g[yy,xx],np.float32); ov=np.asarray(o[yy,xx],np.float32)
        dv=np.abs(gv-ov)
        for c in range(3):
            ch_max[c]=max(ch_max[c],float(dv[c])); ch_mean[c]+=float(dv[c])
        ch_n+=1
        md=float(dv.max()); cc=int(np.argmax(dv))
        if md>worst[0]: worst=(md,[int(yy),int(xx),cc,float(gv[cc]),float(ov[cc])])
        # Corners contribute to both relevant sides by design.
        if yy==di:
            side_max['top']=max(side_max['top'],md); side_sum['top']+=md; side_n['top']+=1
        if yy==a.height-1-di:
            side_max['bottom']=max(side_max['bottom'],md); side_sum['bottom']+=md; side_n['bottom']+=1
        if xx==di:
            side_max['left']=max(side_max['left'],md); side_sum['left']+=md; side_n['left']+=1
        if xx==a.width-1-di:
            side_max['right']=max(side_max['right'],md); side_sum['right']+=md; side_n['right']+=1
    return {
        'pixels':int(coords.shape[0]),
        'channel_max':ch_max,
        'channel_mean':[v/ch_n for v in ch_mean],
        'side_max':side_max,
        'side_mean':{k:(side_sum[k]/side_n[k] if side_n[k] else 0.0) for k in side_max},
        'worst_yxc_gpu_cpu':worst[1],
    }

handoff={'d8_border':focused_ring(8),'d9_first_rcd':focused_ring(9),'d10_second_rcd':focused_ring(10)}

r={
    'finite':bool(finite),'interior_max':maxv,'interior_mean':mean,
    'interior_p99_sampled':p99,'interior_p999_sampled':p999,
    'channel_max':cmax.tolist(),'channel_mean':(csum/ccount).tolist(),
    'worst_yxc_gpu_cpu':maxpos,'border_max':bmax,'border_mean':bmean,
    'edge_distance_bands':band_metrics,'handoff_diagnostics':handoff,
}
print('RCD_FULL_GPU_ORACLE '+json.dumps(r,separators=(',',':')))
if a.json_out: Path(a.json_out).write_text(json.dumps(r,indent=2)+'\n')
if (not finite) or maxv>0.03: raise SystemExit('RCD_FULL_GPU_ORACLE_FAIL')
print('RCD_FULL_GPU_ORACLE_PASS')
