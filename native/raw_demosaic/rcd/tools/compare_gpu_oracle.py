#!/usr/bin/env python3
import argparse, numpy as np
ap=argparse.ArgumentParser();ap.add_argument('gpu');ap.add_argument('oracle');ap.add_argument('--width',type=int,required=True);ap.add_argument('--height',type=int,required=True);a=ap.parse_args()
h=np.fromfile(a.gpu,dtype='<f2').astype(np.float32).reshape(a.height,a.width,4)[...,:3];o=np.load(a.oracle).astype(np.float32)
d=np.abs(h-o); interior=d[9:-9,9:-9]
print(f'RCD_GPU_ORACLE max={interior.max():.8g} mean={interior.mean():.8g} p99={np.quantile(interior,0.99):.8g} finite={np.isfinite(h).all()}')
if not np.isfinite(h).all() or interior.max()>0.03: raise SystemExit('RCD_GPU_ORACLE_FAIL')
print('RCD_GPU_ORACLE_PASS')
