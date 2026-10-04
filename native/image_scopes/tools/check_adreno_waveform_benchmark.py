#!/usr/bin/env python3
from pathlib import Path
import re,sys
if len(sys.argv)!=2: raise SystemExit('usage: check_adreno_waveform_benchmark.py BENCHMARK_LOG')
text=Path(sys.argv[1]).read_text(errors='replace')
if 'Adreno (TM) 840' not in text:
    print('ADRENO_WAVEFORM_BENCHMARK_FAIL: production GPU Adreno (TM) 840 not found');raise SystemExit(1)
resolutions=re.findall(r'^RESOLUTION ([0-9]+x[0-9]+)$',text,re.M)
if resolutions!=['2048x1536','2040x1532','2040x1536']:
    print(f'ADRENO_WAVEFORM_BENCHMARK_FAIL: expected exact production resolutions 2048x1536,2040x1532,2040x1536; got {resolutions}');raise SystemExit(1)
# 768x512 render target, exact production source resolutions 2048x1536, 2040x1532 and 2040x1536.
limits={
 'wave_rgb_render_only':0.80,
 'wave_50_measure_render':1.90,
 'wave_full_measure_render':2.20,
 'wave_luma_render_only':0.35,
 'wave_luma_50_measure_render':0.75,
 'wave_luma_full_measure_render':0.85,
}
fail=[];vals_by={}
for metric,ceiling in limits.items():
    vals=[float(x) for x in re.findall(rf'^{re.escape(metric)} .*?median=([0-9.]+)',text,re.M)]
    vals_by[metric]=vals
    if len(vals)!=3: fail.append(f'{metric}: expected 3 production-resolution samples, found {len(vals)}');continue
    for i,v in enumerate(vals):
        if v>ceiling: fail.append(f'{metric}[{i}] median {v:.4f} ms > {ceiling:.2f} ms')
mem=re.findall(r'^WAVEFORM_MEMORY .*?actual_device_allocation_per_slot_bytes=([0-9]+).*?three_slots_MiB=([0-9.]+).*?scratch_bytes=([0-9]+)',text,re.M)
if len(mem)!=1: fail.append(f'WAVEFORM_MEMORY: expected 1 report, found {len(mem)}')
else:
    actual=int(mem[0][0]);three=float(mem[0][1]);scratch=int(mem[0][2])
    if actual>1600000: fail.append(f'waveform allocation per slot {actual} > 1,600,000 bytes')
    if three>4.60: fail.append(f'waveform 3-slot allocation {three:.4f} MiB > 4.60 MiB')
    if scratch!=0: fail.append(f'waveform scratch_bytes={scratch}, expected 0')
if fail:
    print('ADRENO_WAVEFORM_BENCHMARK_FAIL')
    for x in fail: print(' -',x)
    raise SystemExit(1)
print('ADRENO_WAVEFORM_BENCHMARK_PASS')
for metric,ceiling in limits.items(): print(f"{metric}: "+', '.join(f'{v:.4f} ms' for v in vals_by[metric])+f' (ceiling {ceiling:.2f} ms)')
if mem: print(f'waveform memory: {mem[0][0]} bytes/slot, {mem[0][1]} MiB/3 slots, scratch={mem[0][2]}')
