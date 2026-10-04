#!/usr/bin/env python3
import argparse, json
import numpy as np
ap=argparse.ArgumentParser(); ap.add_argument('gpu'); ap.add_argument('cpu'); ap.add_argument('--width',type=int,required=True); ap.add_argument('--height',type=int,required=True); a=ap.parse_args()
g=np.memmap(a.gpu,dtype='<f2',mode='r',shape=(a.height,a.width,4))[...,:3].astype(np.float32); c=np.load(a.cpu,mmap_mode='r')
finite=bool(np.isfinite(g).all()); d=np.abs(g-c); finite_d=d[np.isfinite(d)]
r={'gpu_finite':finite,'gpu_min':float(np.nanmin(g)),'gpu_max':float(np.nanmax(g)),'cpu_min':float(np.nanmin(c)),'cpu_max':float(np.nanmax(c)),'finite_diff_p99':float(np.quantile(finite_d,.99)) if finite_d.size else None,'finite_diff_max':float(finite_d.max()) if finite_d.size else None}
print('RCD_NUMERICAL_STRESS_GPU '+json.dumps(r,separators=(',',':')))
if not finite: raise SystemExit('RCD_NUMERICAL_STRESS_GPU_FAIL nonfinite')
print('RCD_NUMERICAL_STRESS_GPU_PASS')
