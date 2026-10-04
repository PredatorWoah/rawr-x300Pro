#!/usr/bin/env python3
import argparse, json, pathlib, sys
import numpy as np

ap=argparse.ArgumentParser()
ap.add_argument('--input-f32', required=True)
ap.add_argument('--gpu-rgba16f', required=True)
ap.add_argument('--width', type=int, required=True)
ap.add_argument('--height', type=int, required=True)
ap.add_argument('--pattern', choices=['RGGB','GRBG','GBRG','BGGR'], required=True)
ap.add_argument('--contrast-percent', type=float, required=True)
ap.add_argument('--json-out')
a=ap.parse_args()
root=pathlib.Path(__file__).resolve().parents[1]
sys.path.insert(0,str(root/'tools'))
sys.path.insert(0,str(root/'../rcd/tools'))
sys.path.insert(0,str(root/'../vng4/tools'))
import rcd_reference
import vng4_reference
from dual_reference import dual_blend

raw255=np.fromfile(a.input_f32,dtype='<f4')
expected=a.width*a.height
if raw255.size != expected:
    raise SystemExit(f'DUAL_ANDROID_COMPARE_FAIL input_samples={raw255.size} expected={expected}')
raw=(raw255.reshape(a.height,a.width)/np.float32(255.0)).astype(np.float32)
g=np.fromfile(a.gpu_rgba16f,dtype='<f2')
if g.size != expected*4:
    raise SystemExit(f'DUAL_ANDROID_COMPARE_FAIL gpu_samples={g.size} expected={expected*4}')
g=g.reshape(a.height,a.width,4).astype(np.float32)
pi=['RGGB','GRBG','GBRG','BGGR'].index(a.pattern)
r=rcd_reference.demosaic(raw,a.pattern,mode='rawr').astype(np.float32)
if a.contrast_percent == 0.0:
    o=r
    mask=np.ones((a.height,a.width),np.float32)
else:
    v=vng4_reference.demosaic(raw,pi).astype(np.float32)
    o,mask=dual_blend(r,v,a.contrast_percent)
d=np.abs(g[...,:3]-o)
finite=bool(np.isfinite(g).all())
# RCD has a 9px canonical CPU/GPU handoff and VNG4 a 3px handoff. Report both
# full frame and a conservative 9px interior without hiding border behavior.
b=9 if min(a.width,a.height)>20 else 0
inner=d[b:a.height-b,b:a.width-b] if b else d
flat=inner.reshape(-1)
rpt={
    'finite': finite,
    'pattern': a.pattern,
    'contrast_percent': a.contrast_percent,
    'mask_min': float(mask.min()), 'mask_mean': float(mask.mean()), 'mask_max': float(mask.max()),
    'interior_border': b,
    'interior_max': float(flat.max()), 'interior_mean': float(flat.mean()),
    'interior_p99': float(np.quantile(flat,0.99)), 'interior_p999': float(np.quantile(flat,0.999)),
    'full_max': float(d.max()), 'full_mean': float(d.mean()),
    'channel_max': d.reshape(-1,3).max(axis=0).tolist(),
    'alpha_max_error': float(np.abs(g[...,3]-1.0).max()),
}
print('DUAL_ANDROID_ORACLE '+json.dumps(rpt,separators=(',',':')))
if a.json_out:
    pathlib.Path(a.json_out).write_text(json.dumps(rpt,indent=2)+'\n')
# Gate is intentionally inherited from the looser of the frozen RCD/VNG4 GPU
# full-oracle envelopes, plus a strict alpha/finite requirement. Bulk metrics
# stay close to the VNG4 gate. This is not a claim of bit identity.
ok=(finite and rpt['interior_max']<=0.05 and rpt['interior_mean']<=0.0015 and
    rpt['interior_p999']<=0.006 and rpt['alpha_max_error']==0.0)
if not ok:
    raise SystemExit('DUAL_ANDROID_ORACLE_FAIL')
print('DUAL_ANDROID_ORACLE_PASS')
