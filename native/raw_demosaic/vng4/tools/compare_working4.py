#!/usr/bin/env python3
import json,sys,struct
def get(path,p):
    for line in open(path):
        if line.startswith(p): return json.loads(line[len(p):])
    raise SystemExit("missing "+p)
c=get(sys.argv[1],"VNG4_CPU_WORKING4_5X5 ")["values"]
g=get(sys.argv[2],"VNG4_GPU_WORKING4_5X5 ")["values"]
bad=[]
for i,(a,b) in enumerate(zip(c,g)):
    if a!=b:
        pix=i//4; ch=i%4; oy=pix//5-2; ox=pix%5-2
        bad.append({"ox":ox,"oy":oy,"channel":ch,"cpu":a,"gpu":b,"delta":b-a})
print("VNG4_WORKING4_MISMATCH_COUNT "+str(len(bad)))
for x in bad: print("VNG4_WORKING4_DIFF "+json.dumps(x,separators=(",",":")))
print("VNG4_WORKING4_FIRST_DIVERGENCE "+("none" if not bad else json.dumps(bad[0],separators=(",",":"))))
