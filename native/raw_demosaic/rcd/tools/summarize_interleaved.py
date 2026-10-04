#!/usr/bin/env python3
import re, sys
from pathlib import Path
if len(sys.argv)!=2: raise SystemExit('usage: summarize_interleaved.py LOG')
lines=Path(sys.argv[1]).read_text(errors='replace').splitlines()
records=[]; current=None
for line in lines:
    m=re.search(r'RCD_INTERLEAVED_RUN_BEGIN kind=(control|candidate) label=([^ ]+) round=(\d+)', line)
    if m: current={'kind':m.group(1),'label':m.group(2),'round':int(m.group(3))}
    m=re.search(r'RCD_BENCH_STAGE name=rcd\.total .*?median_ms=([0-9.]+)', line)
    if m and current is not None: current['median']=float(m.group(1))
    m=re.search(r'RCD_INTERLEAVED_RUN_END kind=(control|candidate) label=([^ ]+) round=(\d+)', line)
    if m and current is not None:
        if 'median' in current: records.append(current)
        current=None
# For each candidate, use nearest control before and after in execution order.
results={}
for i,r in enumerate(records):
    if r['kind']!='candidate': continue
    prev=next((records[j] for j in range(i-1,-1,-1) if records[j]['kind']=='control'),None)
    nxt=next((records[j] for j in range(i+1,len(records)) if records[j]['kind']=='control'),None)
    if not prev or not nxt: continue
    ctl=(prev['median']+nxt['median'])/2
    ratio=r['median']/ctl
    results.setdefault(r['label'],[]).append((ratio,r['median'],ctl,r['round']))
if not results: raise SystemExit('RCD_INTERLEAVED_SUMMARY_FAIL no_normalized_candidates')
rank=[]
for label,vals in results.items():
    ratios=sorted(v[0] for v in vals); raw=sorted(v[1] for v in vals); ctls=sorted(v[2] for v in vals)
    med=lambda a:(a[len(a)//2] if len(a)%2 else (a[len(a)//2-1]+a[len(a)//2])/2)
    rank.append((med(ratios),label,med(raw),med(ctls),len(vals)))
rank.sort()
print('RCD_INTERLEAVED_RANKING')
for idx,(ratio,label,raw,ctl,n) in enumerate(rank,1):
    print(f'RCD_INTERLEAVED_RESULT rank={idx} variant={label} normalized_ratio={ratio:.6f} normalized_gain_pct={(1-ratio)*100:.3f} raw_median_ms={raw:.6f} local_control_ms={ctl:.6f} samples={n}')
best=rank[0]
print(f'RCD_INTERLEAVED_WINNER variant={best[1]} normalized_ratio={best[0]:.6f} normalized_gain_pct={(1-best[0])*100:.3f}')
print('RCD_INTERLEAVED_SUMMARY_PASS')
