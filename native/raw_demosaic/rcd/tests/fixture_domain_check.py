#!/usr/bin/env python3
from pathlib import Path
import numpy as np
root=Path(__file__).resolve().parents[1]/'validation'/'dng_oracle'
for stem in ('RAWR_20260827_00115596','RAWR_20260827_00101910'):
    nominal=np.load(root/f'{stem}_cfa.npy').astype(np.float32)
    linear=np.fromfile(root/f'{stem}_cfa.f32',dtype='<f4').reshape(nominal.shape)
    err=np.max(np.abs(linear - nominal*np.float32(255.0)))
    if not np.isfinite(linear).all() or err>2e-5:
        raise SystemExit(f'RCD_FIXTURE_DOMAIN_FAIL stem={stem} max={err}')
print('RCD_FIXTURE_DOMAIN_PASS domain=LINEAR_0_255 gpu_to_nominal_scale=1/255')
