#!/usr/bin/env python3
import argparse, json
from pathlib import Path
import numpy as np

ap=argparse.ArgumentParser()
ap.add_argument('gpu'); ap.add_argument('oracle')
ap.add_argument('--width',type=int,required=True); ap.add_argument('--height',type=int,required=True)
ap.add_argument('--json-out')
a=ap.parse_args(); H=a.height; W=a.width; B=3

g=np.memmap(a.gpu,dtype='<f2',mode='r',shape=(H,W,4))[...,:3]
o=np.load(a.oracle,mmap_mode='r')
if o.shape!=(H,W,3): raise SystemExit(f'VNG4_FULL_GPU_ORACLE_FAIL oracle_shape={o.shape} expected={(H,W,3)}')

finite=True; maxv=-1.0; worst=None; total=0.0; count=0; samples=[]
cmax=np.zeros(3,np.float64); csum=np.zeros(3,np.float64); ccount=0
for y0 in range(B,H-B,128):
    y1=min(H-B,y0+128)
    gg=np.asarray(g[y0:y1,B:W-B],np.float32); oo=np.asarray(o[y0:y1,B:W-B],np.float32)
    finite=finite and bool(np.isfinite(gg).all())
    d=np.abs(gg-oo)
    idx=np.unravel_index(np.argmax(d),d.shape); m=float(d[idx])
    if m>maxv:
        maxv=m; worst=[int(y0+idx[0]),int(B+idx[1]),int(idx[2]),float(gg[idx]),float(oo[idx])]
    total+=float(d.sum(dtype=np.float64)); count+=d.size
    cmax=np.maximum(cmax,d.max(axis=(0,1))); csum+=d.sum(axis=(0,1),dtype=np.float64); ccount+=d.shape[0]*d.shape[1]
    samples.append(d.reshape(-1)[::97])
s=np.concatenate(samples); mean=total/count; p99=float(np.quantile(s,.99)); p999=float(np.quantile(s,.999))
mask=np.zeros((H,W),bool); mask[:B,:]=True; mask[-B:,:]=True; mask[:,:B]=True; mask[:,-B:]=True
bd=np.abs(np.asarray(g[mask],np.float32)-np.asarray(o[mask],np.float32))

# VNG4 border is 3 pixels. Report last border, first interior, and next ring.
y=np.arange(H)[:,None]; x=np.arange(W)[None,:]
dist=np.minimum(np.minimum(y,H-1-y),np.minimum(x,W-1-x))
def ring(di):
    m=(dist==di); coords=np.argwhere(m)
    if not coords.size:return {}
    vals=[]; worst_r=(-1.0,None); chs=[]
    for y0 in range(0,H,128):
        y1=min(H,y0+128); mm=m[y0:y1]
        if not np.any(mm): continue
        gg=np.asarray(g[y0:y1],np.float32)[mm]; oo=np.asarray(o[y0:y1],np.float32)[mm]; d=np.abs(gg-oo)
        vals.append(d.reshape(-1)); chs.append(d)
        im=np.argmax(d); idx=np.unravel_index(im,d.shape); mmx=float(d[idx])
        if mmx>worst_r[0]:
            ys,xs=np.argwhere(mm)[idx[0]]; worst_r=(mmx,[int(y0+ys),int(xs),int(idx[1]),float(gg[idx]),float(oo[idx])])
    z=np.concatenate(vals); zz=np.concatenate(chs,axis=0)
    return {'max':float(z.max()),'mean':float(z.mean()),'p99':float(np.quantile(z,.99)),
            'channel_max':zz.max(axis=0).tolist(),'channel_mean':zz.mean(axis=0).tolist(),
            'pixels':int(coords.shape[0]),'worst_yxc_gpu_cpu':worst_r[1]}

r={'finite':bool(finite),'interior_max':maxv,'interior_mean':mean,'interior_p99_sampled':p99,'interior_p999_sampled':p999,
   'channel_max':cmax.tolist(),'channel_mean':(csum/ccount).tolist(),'worst_yxc_gpu_cpu':worst,
   'border_max':float(bd.max()),'border_mean':float(bd.mean()),
   'border_handoff':{'d2_last_border':ring(2),'d3_first_vng4':ring(3),'d4_second_vng4':ring(4)}}
print('VNG4_FULL_GPU_ORACLE '+json.dumps(r,separators=(',',':')))
if a.json_out: Path(a.json_out).write_text(json.dumps(r,indent=2)+'\n')
# Full-frame gate. The oracle itself now uses canonical float32 VNG semantics; keep an isolated max guard in addition to strict bulk metrics.
ok=finite and p999<=0.005 and mean<=0.001 and maxv<=0.05 and float(bd.max())<=0.005
if not ok: raise SystemExit('VNG4_FULL_GPU_ORACLE_FAIL')
print('VNG4_FULL_GPU_ORACLE_PASS')
