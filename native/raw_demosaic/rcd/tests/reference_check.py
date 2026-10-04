#!/usr/bin/env python3
from pathlib import Path
import json, sys
import numpy as np
ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'tools'))
from rcd_reference import demosaic, PATTERNS

def c(pattern,y,x): return PATTERNS[pattern][y&1][x&1]

# Constant fields must remain constant for every CFA arrangement.
for pattern in PATTERNS:
    a=np.full((32,34),0.37,np.float32)
    rgb=demosaic(a,pattern,'rawr')
    if not np.all(np.isfinite(rgb)) or np.max(np.abs(rgb-0.37))>2e-5:
        raise SystemExit(f'constant-field failure {pattern}: {np.max(np.abs(rgb-0.37))}')

# Known CFA samples must survive exactly in their own channel, including values
# outside [0,1], because Rawr deliberately removes the upstream I/O clamps.
rng=np.random.default_rng(1234)
a=rng.uniform(-0.03,1.25,size=(40,42)).astype(np.float32)
for pattern in PATTERNS:
    rgb=demosaic(a,pattern,'rawr')
    if not np.all(np.isfinite(rgb)): raise SystemExit(f'nonfinite output {pattern}')
    err=0.0
    for y in range(a.shape[0]):
        for x in range(a.shape[1]): err=max(err,abs(float(rgb[y,x,c(pattern,y,x)]-a[y,x])))
    if err>1e-6: raise SystemExit(f'CFA preservation failure {pattern}: {err}')

# Shipped DNG-derived oracle fixtures are self-consistent and source-identified.
fixtures=ROOT/'validation'/'dng_oracle'
for meta_path in sorted(fixtures.glob('*.json')):
    m=json.loads(meta_path.read_text()); stem=meta_path.stem
    cfa=np.load(fixtures/f'{stem}_cfa.npy'); expected=np.load(fixtures/f'{stem}_rcd_rawr.npy')
    got=demosaic(cfa,m['pattern'],'rawr')
    err=float(np.max(np.abs(got-expected)))
    if err>1e-7: raise SystemExit(f'DNG oracle drift {stem}: {err}')
print('RCD_CPU_REFERENCE_PASS')
