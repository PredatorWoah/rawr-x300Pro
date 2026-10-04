#!/usr/bin/env python3
"""Deterministic full-resolution VNG4 production-geometry oracle."""
import argparse,json,sys,time
from pathlib import Path
import numpy as np
sys.path.insert(0,str(Path(__file__).resolve().parent))
from vng4_reference import demosaic
ap=argparse.ArgumentParser(); ap.add_argument('--width',type=int,required=True); ap.add_argument('--height',type=int,required=True); ap.add_argument('--out-dir',required=True); a=ap.parse_args()
if a.width<7 or a.height<7: raise SystemExit('geometry must be >=7x7')
out=Path(a.out_dir); out.mkdir(parents=True,exist_ok=True); stem=f'geom_{a.width}x{a.height}'
y=np.arange(a.height,dtype=np.float32)[:,None]; x=np.arange(a.width,dtype=np.float32)[None,:]
xf=x/np.float32(max(1,a.width-1)); yf=y/np.float32(max(1,a.height-1))
base=np.float32(.04)+np.float32(.55)*xf+np.float32(.20)*yf
edge=np.where(xf>np.float32(.46)+np.float32(.12)*(yf-np.float32(.5)),np.float32(.16),np.float32(-.06))
tex=np.float32(.04)*np.sin(np.float32(.111)*x+np.float32(.073)*y)+np.float32(.025)*np.sin(np.float32(.033)*x-np.float32(.095)*y)
spot=np.float32(.30)*np.exp(-(((xf-np.float32(.76))/np.float32(.055))**2+((yf-np.float32(.23))/np.float32(.07))**2))
L=base+edge+tex+spot
R=np.clip(L*np.float32(1.06)+np.float32(.03)*np.sin(np.float32(.019)*x),np.float32(.03),np.float32(.97))
G=np.clip(L*np.float32(.99)+np.float32(.018)*np.sin(np.float32(.023)*y),np.float32(.03),np.float32(.97))
B=np.clip(L*np.float32(.91)+np.float32(.04)*np.cos(np.float32(.017)*(x+y)),np.float32(.03),np.float32(.97))
cfa=np.empty((a.height,a.width),np.float32); cfa[0::2,0::2]=R[0::2,0::2]; cfa[0::2,1::2]=G[0::2,1::2]; cfa[1::2,0::2]=G[1::2,0::2]; cfa[1::2,1::2]=B[1::2,1::2]
(cfa*np.float32(255)).astype('<f4').tofile(out/f'{stem}_cfa_linear255.f32')
demosaic(cfa[:32,:32].copy(),0); t=time.perf_counter(); rgb=demosaic(cfa,0); sec=time.perf_counter()-t
np.save(out/f'{stem}_vng4_cpu.npy',rgb)
meta={'width':a.width,'height':a.height,'pattern':'RGGB','cfa_min':float(cfa.min()),'cfa_max':float(cfa.max()),'rgb_min':float(rgb.min()),'rgb_max':float(rgb.max()),'cpu_vng4_seconds':sec}
(out/f'{stem}.json').write_text(json.dumps(meta,indent=2)+'\n'); print('VNG4_GEOMETRY_CPU_ORACLE_PASS '+json.dumps(meta,separators=(',',':')))
