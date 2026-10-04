#!/usr/bin/env python3
import argparse, json, numpy as np
p=argparse.ArgumentParser();p.add_argument('--baseline',required=True);p.add_argument('--variant',required=True);p.add_argument('--width',type=int,required=True);p.add_argument('--height',type=int,required=True);p.add_argument('--mode',required=True);a=p.parse_args()
shape=(a.height,a.width,4)
b=np.fromfile(a.baseline,dtype=np.float16).astype(np.float32).reshape(shape)
c=np.fromfile(a.variant,dtype=np.float16).astype(np.float32).reshape(shape)
d=np.abs(c-b); rgb=d[...,:3]
out={'mode':a.mode,'finite':bool(np.isfinite(c).all()),'max':float(rgb.max()),'mean':float(rgb.mean()),'p99':float(np.quantile(rgb,0.99)),'p999':float(np.quantile(rgb,0.999)),'alpha_max':float(d[...,3].max())}
print('DUAL_AB_DIFF '+json.dumps(out,separators=(',',':')))
if not out['finite'] or out['alpha_max']!=0.0: raise SystemExit('DUAL_AB_DIFF_FAIL')
print('DUAL_AB_DIFF_PASS')
