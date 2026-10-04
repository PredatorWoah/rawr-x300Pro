#!/usr/bin/env python3
import argparse, numpy as np
ap=argparse.ArgumentParser();ap.add_argument("--width",type=int,default=96);ap.add_argument("--height",type=int,default=80);ap.add_argument("--out",required=True)
a=ap.parse_args();W,H=a.width,a.height
y,x=np.mgrid[0:H,0:W].astype(np.float32)
# Deterministic mixture: smooth ramps + Nyquist-ish chroma + edges + negative/headroom samples.
r=.08+1.35*x/max(W-1,1)+.12*np.sin(x*.91)+.06*np.cos(y*.73)
g=.10+.95*y/max(H-1,1)+.10*np.sin((x+y)*1.17)
b=.06+1.15*(x+y)/max(W+H-2,1)+.14*np.cos(x*1.31-y*.67)
rgb=np.stack([r,g,b,np.ones_like(r)],-1).astype(np.float32)
rgb[H//4:H//2,W//3:W//3+3,:3]=np.array([1.8,.2,1.45],np.float32)
rgb[H//2:H//2+2,W//5:4*W//5,:3]=np.array([-.08,.85,.12],np.float32)
np.asarray(rgb,dtype=np.float16).tofile(a.out)
print("FCC_FIXTURE_PASS",W,H,a.out)
