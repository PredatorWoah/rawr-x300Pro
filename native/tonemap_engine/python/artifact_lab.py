import sys, json
from pathlib import Path
import numpy as np
from PIL import Image, ImageDraw
ROOT=Path(__file__).resolve().parents[1]; sys.path.insert(0,str(ROOT/'python'))
from tonemap_ref import *
OUT=ROOT/'artifact_out'; OUT.mkdir(exist_ok=True)

COLORS={
 'red':np.array([1.0,.03,.02]),'orange':np.array([1.0,.22,.03]),'yellow':np.array([1.0,.75,.03]),
 'green':np.array([.03,1.0,.05]),'cyan':np.array([.02,.9,1.0]),'blue':np.array([.03,.12,1.0]),
 'magenta':np.array([1.0,.03,.75]),'skin':np.array([.72,.38,.25])}

def hue(l): return (np.degrees(np.arctan2(l[...,2],l[...,1]))+360)%360
def angle(a,b): return (a-b+180)%360-180

def pre_gamut(samples,p=ParamsBaseline()):
    # local copy through outlin, avoiding render's V1.2 gamut
    rgb=np.asarray(samples,float); work=rgb
    Yin=np.sum(work*AP1_LUMA,axis=-1); safe=np.maximum(Yin,EPS)
    x=np.log2(safe/MIDDLE_GRAY)+p.exposureEV
    k,_,_,c=compile_curve(p); yo=eval_curve(x,k,c); sc=np.where(Yin>EPS,yo/safe,0)
    tone=work*sc[...,None]; yt=np.sum(tone*AP1_LUMA,axis=-1,keepdims=True)
    sat=yt+p.saturation*(tone-yt)
    return sat@M_AP1_TO_SRGB.T

def gamut_metrics(mapper):
    evs=np.linspace(-3,8,177); rep={}
    for name,c in COLORS.items():
        src=c[None,:]*(2**evs)[:,None]; lin=pre_gamut(src); g=mapper(lin)
        l0=linear_srgb_to_oklab(lin); lg=linear_srgb_to_oklab(g)
        C=np.hypot(lg[:,1],lg[:,2]); valid=C>.015
        dh=np.abs(angle(hue(lg),hue(l0)))
        Y0=lin@SRGB_LUMA; Yg=g@SRGB_LUMA
        rep[name]={
          'max_hue_deg':float(dh[valid].max()) if valid.any() else 0,
          'p95_hue_deg':float(np.percentile(dh[valid],95)) if valid.any() else 0,
          'max_abs_luma_change':float(np.max(np.abs(Yg-Y0))),
          'min_rgb':float(g.min()),'max_rgb':float(g.max())}
    return rep

def save_gamut_sheet():
    evs=np.linspace(-2,8,900)
    rows=[]
    for name,c in COLORS.items():
        lin=pre_gamut(c[None,:]*(2**evs)[:,None])
        A=np.clip(srgb_oetf(np.clip(gamut_compress(lin),0,1)),0,1)
        B=np.clip(srgb_oetf(gamut_compress_v12(lin)),0,1)
        strip=np.concatenate([np.tile(A[None,:,:],(50,1,1)),np.tile(B[None,:,:],(50,1,1))],axis=0)
        rows.append(strip)
    arr=np.concatenate(rows,axis=0)
    Image.fromarray(np.rint(arr*255).astype(np.uint8)).save(OUT/'gamut_A_vs_v121.png')

def max_run_1d(v):
    v=np.asarray(v)
    best=cur=1
    for i in range(1,len(v)):
        if np.array_equal(v[i],v[i-1]): cur+=1; best=max(best,cur)
        else: cur=1
    return int(best)

def old_v121_dither(encoded):
    a=np.asarray(encoded,dtype=np.float64)
    h,w=a.shape[-3],a.shape[-2]
    yy,xx=np.indices((h,w),dtype=np.uint32)
    n=(dither_hash01(xx,yy,0)-0.5)/255.0
    return np.clip(a+n[...,None],0.0,1.0)

def _block_rms(err, block=8):
    h,w=err.shape
    h2=(h//block)*block; w2=(w//block)*block
    e=err[:h2,:w2].reshape(h2//block,block,w2//block,block).mean((1,3))
    return float(np.sqrt(np.mean(e*e)))

def _stripe_metrics(q, target_codes):
    # Average error over rows. Coherent vertical contouring survives this average;
    # unstructured dither converges toward zero.
    err=q[...,0].astype(np.float64)-target_codes[None,:]
    col=err.mean(axis=0)
    return {
      'column_mean_error_rms_codes':float(np.sqrt(np.mean(col*col))),
      'column_mean_error_max_codes':float(np.max(np.abs(col))),
      'block8_mean_error_rms_codes':_block_rms(err,8),
    }

def _label_strip(rgb, label):
    a=np.rint(np.clip(rgb,0,1)*255).astype(np.uint8)
    im=Image.fromarray(a)
    d=ImageDraw.Draw(im)
    d.rectangle((0,0,390,28),fill=(20,20,20))
    d.text((8,7),label,fill=(255,255,255))
    return np.asarray(im)

def banding_lab():
    W=2048; H=192
    # Deliberately shallow ENCODED-space ramps. This isolates final 8-bit
    # quantization from the floating-point tone curve and gamut math.
    ranges=[(.015,.035),(.035,.08),(.08,.20),(.45,.55),(.85,.97)]
    panels=[]; stats={}
    for lo,hi in ranges:
        target=np.linspace(lo,hi,W,dtype=np.float64)
        base=np.tile(target[None,:,None],(H,1,3))
        old=old_v121_dither(base)
        new=apply_rgba8_dither(base)
        q0=np.rint(base*255).astype(np.uint8)
        qo=np.rint(old*255).astype(np.uint8)
        qn=np.rint(new*255).astype(np.uint8)
        key=f'{lo:.3f}_{hi:.3f}'
        err=(new-base)*255.0
        stats[key]={
          'unique_codes_no_dither':int(len(np.unique(q0[H//2,:,0]))),
          'unique_codes_v121_center_row':int(len(np.unique(qo[H//2,:,0]))),
          'unique_codes_v122_center_row':int(len(np.unique(qn[H//2,:,0]))),
          'max_flat_run_no_dither':max_run_1d(q0[H//2]),
          'max_flat_run_v121_center_row':max_run_1d(qo[H//2]),
          'max_flat_run_v122_center_row':max_run_1d(qn[H//2]),
          'v122_dither_mean_codes':float(np.mean(err)),
          'v122_dither_std_codes':float(np.std(err)),
          'v122_dither_max_abs_codes':float(np.max(np.abs(err))),
          'no_dither_stripe':_stripe_metrics(q0,target*255),
          'v121_stripe':_stripe_metrics(qo,target*255),
          'v122_stripe':_stripe_metrics(qn,target*255),
        }
        # Explicit labels matter: the diagnostic intentionally includes the
        # broken undithered control and the old dither next to the candidate.
        panel=np.concatenate([
          _label_strip(base,f'{key}  NO DITHER (control; expected to band)'),
          _label_strip(old,f'{key}  V1.2.1 uniform +/-0.5 LSB'),
          _label_strip(new,f'{key}  V1.2.2 low-discrepancy +/-0.5 LSB candidate'),
        ],axis=0)
        panels.append(panel)
    arr=np.concatenate(panels,axis=0)
    Image.fromarray(arr).save(OUT/'banding_dither_AB_labeled.png')

    # Flat-field noise diagnostic: production dither should look like very fine
    # neutral texture with no chromatic tint, bands, checkerboard or low-frequency drift.
    levels=[.02,.08,.18,.5,.9]
    rows=[]
    for lev in levels:
        flat=np.full((128,W,3),lev,dtype=np.float64)
        new=apply_rgba8_dither(flat)
        rows.append(_label_strip(new,f'flat encoded {lev:.2f}  V1.2.2 low-discrepancy'))
    Image.fromarray(np.concatenate(rows,axis=0)).save(OUT/'dither_flat_fields.png')
    return stats

def main():
    A=gamut_metrics(gamut_compress); B=gamut_metrics(gamut_compress_v12); band=banding_lab(); save_gamut_sheet()
    rep={'legacy_gamut':A,'v122_gamut':B,'banding':band}
    (OUT/'artifact_metrics.json').write_text(json.dumps(rep,indent=2))
    print(json.dumps(rep,indent=2)); print('V1_3_ARTIFACT_LAB_PASS')
if __name__=='__main__': main()
