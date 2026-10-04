#!/usr/bin/env python3
from pathlib import Path
import re, sys

if len(sys.argv)!=2:
    raise SystemExit("usage: check_adreno_vectorscope_benchmark.py BENCHMARK_LOG")

text=Path(sys.argv[1]).read_text(errors="replace")
if "Adreno (TM) 840" not in text:
    print("ADRENO_VECTORSCOPE_BENCHMARK_FAIL: production GPU Adreno (TM) 840 not found")
    raise SystemExit(1)
resolutions=re.findall(r"^RESOLUTION ([0-9]+x[0-9]+)$",text,re.M)
if resolutions!=["2048x1536","2040x1532","2040x1536"]:
    print(f"ADRENO_VECTORSCOPE_BENCHMARK_FAIL: expected exact production resolutions 2048x1536,2040x1532,2040x1536; got {resolutions}")
    raise SystemExit(1)

limits={
    "vec256_render_only":0.30,
    "vec256_50_measure_render":4.00,
    "vec256_full_measure_render":5.00,
}
fail=[]
for metric,ceiling in limits.items():
    vals=[float(x) for x in re.findall(rf"^{re.escape(metric)} .*?median=([0-9.]+)",text,re.M)]
    if len(vals)!=3:
        fail.append(f"{metric}: expected 3 production-resolution samples, found {len(vals)}")
        continue
    for i,v in enumerate(vals):
        if v>ceiling:
            fail.append(f"{metric}[{i}] median {v:.4f} ms > {ceiling:.2f} ms")

if fail:
    print("ADRENO_VECTORSCOPE_BENCHMARK_FAIL")
    for x in fail: print(" -",x)
    raise SystemExit(1)

print("ADRENO_VECTORSCOPE_BENCHMARK_PASS")
for metric,ceiling in limits.items():
    vals=[float(x) for x in re.findall(rf"^{re.escape(metric)} .*?median=([0-9.]+)",text,re.M)]
    print(f"{metric}: " + ", ".join(f"{v:.4f} ms" for v in vals) + f" (ceiling {ceiling:.2f} ms)")
