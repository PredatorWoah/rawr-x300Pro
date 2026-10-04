#!/usr/bin/env python3
import sys, pathlib, numpy as np
sys.path.insert(0,str(pathlib.Path(__file__).resolve().parents[1]/'tools'))
from dual_reference import *
rng=np.random.default_rng(7)
rcd=rng.uniform(-0.05,1.4,(37,41,3)).astype(np.float32)
vng=(rcd+rng.normal(0,0.02,rcd.shape)).astype(np.float32)
out0,m0=dual_blend(rcd,vng,0.0)
assert np.array_equal(out0,rcd) and np.all(m0==1)
for c in (1.0,5.0,10.0,20.0,40.0,80.0,100.0):
    out,m=dual_blend(rcd,vng,c)
    assert np.isfinite(out).all() and np.isfinite(m).all()
    rm=raw_mask(rgb_to_l(rcd),np.float32(c/100.0))
    assert rm.min()>=0 and rm.max()<=1
    # RawTherapee's recursive Gaussian can overshoot by float-rounding epsilon; do not clamp it.
    assert m.min()>=-2e-6 and m.max()<=1.000002
    same,_=dual_blend(rcd,rcd,c)
    assert np.max(np.abs(same-rcd)) < 2e-7
    recon=m[...,None]*(rcd-vng)+vng
    assert np.max(np.abs(out-recon)) <= 1e-7
    print(f'DUAL_REFERENCE_SWEEP contrast={c:g} mask_min={m.min():.9g} mask_mean={m.mean():.9g} mask_max={m.max():.9g}')
print('DUAL_REFERENCE_MATH_PASS')
