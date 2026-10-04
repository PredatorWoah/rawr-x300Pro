#!/usr/bin/env python3
import sys, pathlib, numpy as np
ROOT=pathlib.Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'tools'))
sys.path.insert(0,str(ROOT/'../rcd/tools'))
sys.path.insert(0,str(ROOT/'../vng4/tools'))
import rcd_reference
import vng4_reference as vngref
from dual_reference import dual_blend
patterns=['RGGB','GRBG','GBRG','BGGR']
# Smooth + weak texture CFA deliberately exercises the low/mid-contrast transition instead of saturating the mask at RCD.
y,x=np.mgrid[0:72,0:80]
base=(0.12+0.38*x/79+0.20*y/71+0.003*np.sin(x*0.61)+0.002*np.cos(y*0.47)).astype(np.float32)
for pi,p in enumerate(patterns):
    raw=base.copy()
    # CFA-local color modulation to force demosaicer disagreement without clipping/headroom loss.
    raw += (((x+y+pi)%7)==0).astype(np.float32)*0.004
    r=rcd_reference.demosaic(raw,p,mode='rawr').astype(np.float32)
    v=vngref.demosaic(raw,pi).astype(np.float32)
    for c in (0.0,1.0,5.0,10.0,20.0,40.0,80.0,100.0):
        out,mask=dual_blend(r,v,c)
        assert out.shape==r.shape==v.shape
        assert np.isfinite(out).all() and np.isfinite(mask).all()
        if c > 0.0:
            from dual_reference import rgb_to_l, raw_mask
            rm=raw_mask(rgb_to_l(r),np.float32(c/100.0))
            assert rm.min()>=0.0 and rm.max()<=1.0
        assert mask.min()>=-2e-6 and mask.max()<=1.000002
        recon=mask[...,None]*(r-v)+v
        assert np.max(np.abs(out-recon)) <= 1e-7
        if c == 0.0:
            assert np.array_equal(out,r) and np.all(mask==1.0)
        print(f'DUAL_FULL_ORACLE pattern={p} contrast={c:g} mask_min={mask.min():.9g} mask_mean={mask.mean():.9g} mask_max={mask.max():.9g} rcd_vng_max={np.max(np.abs(r-v)):.9g}')
print('DUAL_FULL_ORACLE_PASS')
