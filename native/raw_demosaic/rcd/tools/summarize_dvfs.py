#!/usr/bin/env python3
import re, sys, math
from pathlib import Path
if len(sys.argv) != 2:
    raise SystemExit("usage: summarize_dvfs.py LOG")
text=Path(sys.argv[1]).read_text(errors='replace').splitlines()
batches=[]; cur=None
for line in text:
    m=re.search(r'RCD_DVFS_BATCH_BEGIN batch=(\d+)',line)
    if m: cur={'batch':int(m.group(1))}
    m=re.search(r'RCD_BENCH_STAGE name=rcd\.total .*?median_ms=([0-9.]+)',line)
    if m and cur is not None: cur['median']=float(m.group(1))
    m=re.search(r'RCD_DEVICE_TELEMETRY .*?elapsed_ms=(\d+) .*?gpu_freq_hz=([^ ]+) .*?thermal_max_mC=([^ ]+)',line)
    if m and cur is not None:
        cur['elapsed_ms']=int(m.group(1)); cur['gpu_freq']=m.group(2); cur['thermal']=m.group(3)
    m=re.search(r'RCD_DVFS_BATCH_END batch=(\d+)',line)
    if m and cur is not None:
        if 'median' in cur: batches.append(cur)
        cur=None
if len(batches)<2: raise SystemExit('RCD_DVFS_SUMMARY_FAIL insufficient_batches')
base=batches[0]['median']; lo=min(x['median'] for x in batches); hi=max(x['median'] for x in batches)
# Detect first sustained >=20% slowdown (two consecutive batches when available).
transition=None
for i,b in enumerate(batches):
    if b['median'] >= base*1.20:
        if i+1==len(batches) or batches[i+1]['median'] >= base*1.20:
            transition=b; break
print(f"RCD_DVFS_SUMMARY batches={len(batches)} first_median_ms={base:.6f} best_ms={lo:.6f} worst_ms={hi:.6f} worst_vs_first={(hi/base-1):.6f}")
if transition:
    print(f"RCD_DVFS_TRANSITION_DETECTED batch={transition['batch']} median_ms={transition['median']:.6f} elapsed_ms={transition.get('elapsed_ms','NA')} gpu_freq_hz={transition.get('gpu_freq','NA')} thermal_max_mC={transition.get('thermal','NA')}")
else:
    print("RCD_DVFS_TRANSITION_NOT_DETECTED threshold=20pct")
print("RCD_DVFS_DIAGNOSTIC_PASS")
