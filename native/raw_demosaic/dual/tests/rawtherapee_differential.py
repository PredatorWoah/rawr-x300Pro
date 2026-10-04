#!/usr/bin/env python3
import pathlib,sys,numpy as np
ROOT=pathlib.Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'tools')); sys.path.insert(0,str(ROOT/'tests'))
from dual_reference import dual_blend
import rawtherapee_manual_reference as rt
rng=np.random.default_rng(20260828)
# Mixed range intentionally includes Rawr negative values and headroom.
r=rng.uniform(-0.08,1.35,(53,61,3)).astype(np.float32)
v=(r+rng.normal(0,0.06,r.shape)).astype(np.float32)
for c in (0.,1.,5.,10.,20.,40.,80.,100.):
    a,ma=dual_blend(r,v,c); b,mb=rt.render(r,v,c)
    md=float(np.max(np.abs(ma-mb))); od=float(np.max(np.abs(a-b)))
    if md>2e-6 or od>3e-6: raise SystemExit(f'RAWTHERAPEE_DIFFERENTIAL_FAIL contrast={c:g} mask_max={md} output_max={od}')
    print(f'RAWTHERAPEE_DIFFERENTIAL contrast={c:g} mask_max={md:.9g} output_max={od:.9g}')
print('RAWTHERAPEE_MANUAL_DIFFERENTIAL_PASS')
