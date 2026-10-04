#!/usr/bin/env python3
import json,sys,struct
def get(path,prefix):
    for line in open(path):
        if line.startswith(prefix): return json.loads(line[len(prefix):])
    raise SystemExit(f"missing {prefix} in {path}")
c=get(sys.argv[1],"VNG4_CPU_TERM_TRACE "); g=get(sys.argv[2],"VNG4_GPU_TERM_TRACE ")
first=None
for i,(cd,gd,cg,gg) in enumerate(zip(c["d"],g["d"],c["g3"],g["g3"])):
    dd=None if cd is None or gd is None else gd-cd
    dg=gg-cg
    if first is None and ((cd is None)!=(gd is None) or (dd is not None and dd!=0.0) or dg!=0.0):
        first=i
    if (dd is not None and dd!=0.0) or dg!=0.0:
        print("VNG4_TERM_DIFF "+json.dumps({"term":i,"cpu_d":cd,"gpu_d":gd,"delta_d":dd,"cpu_g3":cg,"gpu_g3":gg,"delta_g3":dg},separators=(",",":")))
print("VNG4_TERM_FIRST_DIVERGENCE "+("none" if first is None else str(first)))
if first is not None and "a" in c and "a" in g:
    print("VNG4_FIRST_DIVERGENCE_OPERANDS "+json.dumps({"term":first,"cpu_a":c["a"][first],"gpu_a":g["a"][first],"delta_a":g["a"][first]-c["a"][first],"cpu_b":c["b"][first],"gpu_b":g["b"][first],"delta_b":g["b"][first]-c["b"][first]},separators=(",",":")))
