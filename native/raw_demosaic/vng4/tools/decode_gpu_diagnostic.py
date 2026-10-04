#!/usr/bin/env python3
import argparse, json, struct
ap=argparse.ArgumentParser();ap.add_argument("path");ap.add_argument("--x",type=int,required=True);ap.add_argument("--y",type=int,required=True)
a=ap.parse_args()
b=open(a.path,"rb").read()
if len(b) not in (19*4,147*4,275*4,375*4,775*4): raise SystemExit(f"bad diagnostic size: {len(b)}")
u=list(struct.unpack(f"<{len(b)//4}I",b))
def f(i): return struct.unpack("<f",struct.pack("<I",u[i]))[0]
if u[0]!=0x564E4734: raise SystemExit(f"bad diagnostic magic: 0x{u[0]:08x}")
o={"x":a.x,"y":a.y,"centerColor":u[1],"gval":[f(i) for i in range(2,10)],"min":f(10),"max":f(11),"threshold":f(12),"accepted_mask":u[13],"accepted_bits":format(u[13],"08b"),"num":u[14],"greenval":f(15),"sum0":f(16),"sum1":f(17),"green_result":f(18)}
print("VNG4_GPU_PIXEL_DIAGNOSTIC "+json.dumps(o,separators=(",",":")))
if len(u)>=147:
    ds=[]; g3=[]
    for i in range(64):
        v=f(19+i); ds.append(None if v!=v else v); g3.append(f(83+i))
    o={"d":ds,"g3":g3}
    if len(u)>=275:
        aa=[];bb=[]
        for i in range(64):
            va=f(147+i);vb=f(211+i);aa.append(None if va!=va else va);bb.append(None if vb!=vb else vb)
        o["a"]=aa;o["b"]=bb
    print("VNG4_GPU_TERM_TRACE "+json.dumps(o,separators=(",",":")))
if len(u)>=375:
    print("VNG4_GPU_WORKING4_5X5 "+json.dumps({"values":[f(i) for i in range(275,375)]},separators=(",",":")))
if len(u)>=775:
    print("VNG4_GPU_LINEAR_CANDIDATES "+json.dumps({
      "direct":[f(i) for i in range(375,475)],
      "recipmul":[f(i) for i in range(475,575)],
      "sum":[f(i) for i in range(575,675)],
      "den":[f(i) for i in range(675,775)]},separators=(",",":")))
