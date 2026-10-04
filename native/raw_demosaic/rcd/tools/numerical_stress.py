#!/usr/bin/env python3
"""Generate an intentionally ill-conditioned RCD stress fixture.

This is NOT a production-geometry parity fixture. It deliberately allows
negative/near-zero CFA structure that can drive RCD ratio denominators near
singularity. The gate checks finite/safe Vulkan behavior and reports divergence
without pretending FP16 should numerically match float32 at singularities.
"""
import argparse, json, sys, time
from pathlib import Path
import numpy as np
sys.path.insert(0,str(Path(__file__).resolve().parent))
from rcd_numba_core import demosaic_rg
ap=argparse.ArgumentParser(); ap.add_argument('--width',type=int,default=256); ap.add_argument('--height',type=int,default=192); ap.add_argument('--out-dir',required=True); a=ap.parse_args()
out=Path(a.out_dir); out.mkdir(parents=True,exist_ok=True); stem=f'stress_{a.width}x{a.height}'
y=np.arange(a.height,dtype=np.float32)[:,None]; x=np.arange(a.width,dtype=np.float32)[None,:]
xf=x/np.float32(max(1,a.width-1)); yf=y/np.float32(max(1,a.height-1))
L=np.float32(0.015)+np.float32(0.30)*xf+np.float32(0.10)*yf
L+=np.float32(0.11)*np.sin(np.float32(0.17)*x+np.float32(0.09)*y)
L+=np.where(xf>np.float32(0.47)+np.float32(0.1)*(yf-.5),np.float32(0.11),np.float32(-0.12))
R=L*np.float32(1.08)+np.float32(0.04)*np.sin(np.float32(.11)*x)
G=L*np.float32(.98)+np.float32(0.03)*np.sin(np.float32(.13)*y)
B=L*np.float32(.88)+np.float32(0.05)*np.cos(np.float32(.07)*(x+y))
cfa=np.empty((a.height,a.width),np.float32); cfa[0::2,0::2]=R[0::2,0::2]; cfa[0::2,1::2]=G[0::2,1::2]; cfa[1::2,0::2]=G[1::2,0::2]; cfa[1::2,1::2]=B[1::2,1::2]
(cfa*np.float32(255)).astype('<f4').tofile(out/f'{stem}_cfa_linear255.f32')
demosaic_rg(cfa[:32,:32].copy()); t=time.perf_counter(); rgb=demosaic_rg(cfa); sec=time.perf_counter()-t
np.save(out/f'{stem}_rcd_cpu.npy',rgb)
meta={'width':a.width,'height':a.height,'cfa_min':float(cfa.min()),'cfa_max':float(cfa.max()),'cpu_rgb_min':float(np.nanmin(rgb)),'cpu_rgb_max':float(np.nanmax(rgb)),'cpu_finite':bool(np.isfinite(rgb).all()),'cpu_seconds':sec}
(out/f'{stem}.json').write_text(json.dumps(meta,indent=2)+'\n'); print('RCD_NUMERICAL_STRESS_CPU_PASS '+json.dumps(meta,separators=(',',':')))
