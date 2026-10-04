#!/usr/bin/env python3
import re,sys

def med(path):
    txt=open(path).read()
    m=re.search(r'RCD_BENCH_STAGE name=rcd\.total .*?median_ms=([0-9.]+)',txt)
    if not m: raise SystemExit(f'ERROR: total median missing: {path}')
    return float(m.group(1))
a,b=med(sys.argv[1]),med(sys.argv[2]); rel=abs(a-b)/min(a,b)
print(f'RCD_SWEEP_CONTROL_EQUIVALENCE legacy_ms={a:.6f} staged_ms={b:.6f} relative_diff={rel:.6f}')
if rel>0.03:
    print('RCD_SWEEP_CONTROL_EQUIVALENCE_FAIL')
    raise SystemExit(1)
print('RCD_SWEEP_CONTROL_EQUIVALENCE_PASS')
