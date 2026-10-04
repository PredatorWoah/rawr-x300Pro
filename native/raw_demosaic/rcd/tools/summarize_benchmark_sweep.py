#!/usr/bin/env python3
import re,sys
from pathlib import Path
out=Path(sys.argv[1]); geom=sys.argv[2]
rows=[]
for p in sorted(out.glob(f'geom_{geom}_*.txt')):
    if '_control_' in p.name or p.name.endswith('_preheat.txt'): continue
    variant=p.stem[len(f'geom_{geom}_'):]
    vals={}; cfg=''
    for line in p.read_text().splitlines():
        if line.startswith('RCD_BENCHMARK_BEGIN '): cfg=line.split('input=NormalizedFloatBuffer_LINEAR255',1)[-1].strip()
        m=re.search(r'RCD_BENCH_STAGE name=([^ ]+).*?median_ms=([0-9.]+)',line)
        if m: vals[m.group(1)]=float(m.group(2))
    if 'rcd.total' in vals: rows.append((vals['rcd.total'],variant,vals,cfg))
rows.sort()
print('================================================================')
print(f'RCD_SWEEP_RANKING geometry={geom}')
for rank,(total,v,vals,cfg) in enumerate(rows,1):
    print(f'RCD_SWEEP_RESULT rank={rank} variant={v} total_median_ms={total:.6f} direction={vals.get("rcd.direction",float("nan")):.6f} green={vals.get("rcd.green",float("nan")):.6f} diagonal={vals.get("rcd.diagonal",float("nan")):.6f} green_sites={vals.get("rcd.green_sites",float("nan")):.6f} export={vals.get("rcd.export",float("nan")):.6f} {cfg}')
if rows:
    print(f'RCD_SWEEP_WINNER variant={rows[0][1]} total_median_ms={rows[0][0]:.6f} {rows[0][3]}')
