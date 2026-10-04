#!/usr/bin/env python3
import argparse,json,numpy as np
from fcc_reference import fcc
ap=argparse.ArgumentParser()
ap.add_argument("--input",required=True);ap.add_argument("--gpu",required=True)
ap.add_argument("--width",type=int,required=True);ap.add_argument("--height",type=int,required=True)
ap.add_argument("--steps",type=int,required=True)
a=ap.parse_args();W,H=a.width,a.height
inp=np.fromfile(a.input,dtype="<f2").reshape(H,W,4).astype(np.float32)
gpu=np.fromfile(a.gpu,dtype="<f2").reshape(H,W,4).astype(np.float32)
ref=fcc(inp,a.steps)
# GPU storage is fp16; compare against fp16-quantized reference.
refq=ref.astype(np.float16).astype(np.float32)
e=np.abs(gpu[...,:3]-refq[...,:3])
st={"steps":a.steps,"max":float(e.max()),"mean":float(e.mean()),"p99":float(np.quantile(e,.99)),
    "alpha_max":float(np.abs(gpu[...,3]-inp[...,3]).max())}
print("FCC_GPU_ORACLE",json.dumps(st))
# Allow a handful of half-ULP-level numerical differences from shader arithmetic.
if not np.isfinite(gpu).all() or st["max"]>0.004 or st["p99"]>0.0011 or st["alpha_max"]>0.0:
    raise SystemExit("FCC_GPU_ORACLE_FAIL")
print("FCC_GPU_ORACLE_PASS")
