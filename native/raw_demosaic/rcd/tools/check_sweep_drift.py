#!/usr/bin/env python3
import re,sys

def med(path):
    txt=open(path).read()
    m=re.search(r'RCD_BENCH_STAGE name=rcd\.total .*?median_ms=([0-9.]+)',txt)
    if not m: raise SystemExit(f'ERROR: total median missing: {path}')
    return float(m.group(1))
a,b=med(sys.argv[1]),med(sys.argv[2]); rel=(b-a)/a
print(f'RCD_SWEEP_CLOCK_DRIFT start_ms={a:.6f} end_ms={b:.6f} relative_change={rel:.6f}')
if abs(rel)>0.08:
    print('RCD_SWEEP_CLOCK_DRIFT_WARN comparison_may_be_thermally_or_governor_biased')
else:
    print('RCD_SWEEP_CLOCK_DRIFT_PASS')
