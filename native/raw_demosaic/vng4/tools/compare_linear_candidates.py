#!/usr/bin/env python3
import json,sys
def get(path,p):
    for line in open(path):
        if line.startswith(p): return json.loads(line[len(p):])
    raise SystemExit("missing "+p)
c=get(sys.argv[1],"VNG4_CPU_LINEAR_CANDIDATES ")
g=get(sys.argv[2],"VNG4_GPU_LINEAR_CANDIDATES ")
for name in ("direct","recipmul","sum","den"):
    bad=[]
    for i,(a,b) in enumerate(zip(c[name],g[name])):
        if a!=b:
            pix=i//4; bad.append({"ox":pix%5-2,"oy":pix//5-2,"channel":i%4,"cpu":a,"gpu":b,"delta":b-a})
    print("VNG4_LINEAR_"+name.upper()+"_MISMATCH_COUNT "+str(len(bad)))
    for x in bad[:20]: print("VNG4_LINEAR_"+name.upper()+"_DIFF "+json.dumps(x,separators=(",",":")))
# Compare GPU candidates against CPU canonical direct output.
for name in ("direct","recipmul"):
    exact=sum(a==b for a,b in zip(c["direct"],g[name]))
    mae=sum(abs(a-b) for a,b in zip(c["direct"],g[name]))/len(c["direct"])
    print("VNG4_LINEAR_CANDIDATE_SCORE "+json.dumps({"gpu_candidate":name,"exact":exact,"total":len(c["direct"]),"mae":mae},separators=(",",":")))
