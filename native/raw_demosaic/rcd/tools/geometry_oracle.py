#!/usr/bin/env python3
"""Generate a deterministic full-resolution RGGB production-geometry oracle.

This is a dimensions/tails/CFA gate, not a photographic quality fixture. The
scene deliberately mixes smooth gradients, hard edges, diagonals, periodic
texture, saturated-ish highlights, and dark regions so RCD exercises all
stages while remaining deterministic at any requested production geometry.
"""
import argparse, json, sys, time
from pathlib import Path
import numpy as np

sys.path.insert(0,str(Path(__file__).resolve().parent))
from rcd_numba_core import demosaic_rg

ap=argparse.ArgumentParser()
ap.add_argument('--width',type=int,required=True)
ap.add_argument('--height',type=int,required=True)
ap.add_argument('--out-dir',required=True)
a=ap.parse_args()
if a.width < 19 or a.height < 19: raise SystemExit('geometry must be >=19x19')
out=Path(a.out_dir); out.mkdir(parents=True,exist_ok=True)
stem=f'geom_{a.width}x{a.height}'

# Build the mosaic directly, row-wise, to keep transient memory bounded.
y=np.arange(a.height,dtype=np.float32)[:,None]
x=np.arange(a.width,dtype=np.float32)[None,:]
xf=x/np.float32(max(1,a.width-1)); yf=y/np.float32(max(1,a.height-1))
base=np.float32(0.025)+np.float32(0.58)*xf+np.float32(0.22)*yf
edge=np.where(xf > np.float32(0.47)+np.float32(0.11)*(yf-np.float32(0.5)),np.float32(0.18),np.float32(-0.07))
texture=np.float32(0.045)*np.sin(np.float32(0.113)*x+np.float32(0.071)*y)+np.float32(0.025)*np.sin(np.float32(0.031)*x-np.float32(0.097)*y)
spot=np.float32(0.34)*np.exp(-(((xf-np.float32(0.76))/np.float32(0.055))**2+((yf-np.float32(0.23))/np.float32(0.07))**2))
L=base+edge+texture+spot
R=L*np.float32(1.06)+np.float32(0.035)*np.sin(np.float32(0.019)*x)
G=L*np.float32(0.99)+np.float32(0.018)*np.sin(np.float32(0.023)*y)
B=L*np.float32(0.91)+np.float32(0.04)*np.cos(np.float32(0.017)*(x+y))
# Geometry/tail correctness should not be conflated with RCD numerical
# singularity stress. Keep this scene physically plausible and bounded away
# from zero so ratio denominators are well-conditioned. Unclamped negative/>1
# behavior is covered by separate contract/stress tests.
R=np.clip(R,np.float32(0.035),np.float32(0.965))
G=np.clip(G,np.float32(0.035),np.float32(0.965))
B=np.clip(B,np.float32(0.035),np.float32(0.965))
cfa=np.empty((a.height,a.width),np.float32)
cfa[0::2,0::2]=R[0::2,0::2]
cfa[0::2,1::2]=G[0::2,1::2]
cfa[1::2,0::2]=G[1::2,0::2]
cfa[1::2,1::2]=B[1::2,1::2]
del R,G,B,L,base,edge,texture,spot,xf,yf,x,y
(cfa*np.float32(255)).astype('<f4').tofile(out/f'{stem}_cfa_linear255.f32')
# warm JIT
demosaic_rg(cfa[:32,:32].copy())
t=time.perf_counter(); rgb=demosaic_rg(cfa); sec=time.perf_counter()-t
np.save(out/f'{stem}_rcd_cpu.npy',rgb)
meta={'width':a.width,'height':a.height,'pattern':'RGGB','cfa_min':float(cfa.min()),'cfa_max':float(cfa.max()),'rgb_min':float(rgb.min()),'rgb_max':float(rgb.max()),'cpu_rcd_seconds':sec}
(out/f'{stem}.json').write_text(json.dumps(meta,indent=2)+'\n')
print('RCD_GEOMETRY_CPU_ORACLE_PASS '+json.dumps(meta,separators=(',',':')))
