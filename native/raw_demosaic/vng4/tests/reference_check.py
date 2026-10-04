import numpy as np, sys
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'tools'))
from vng4_reference import demosaic
z=np.load(ROOT/'tests/data/vng4_librtprocess_golden.npz')
raw=z['raw'].astype(np.float32,copy=False)
worst=0.0
for p in range(4):
    out=demosaic(raw,p)
    ref=z[f'p{p}']
    assert np.isfinite(out).all()
    assert out.shape==ref.shape
    d=np.abs(out-ref)
    m=float(d.max())
    worst=max(worst,m)
    if m > 2.0e-7:
        i=np.unravel_index(np.argmax(d),d.shape)
        raise AssertionError(f'pattern={p} max={m} at={i} got={out[i]} canonical={ref[i]}')
print(f'VNG4_CPU_REFERENCE_PASS patterns=4 canonical_librtprocess_golden=true max={worst:.9g}')
