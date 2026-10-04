import argparse,json,numpy as np
p=argparse.ArgumentParser();p.add_argument('--meta',required=True);p.add_argument('--cpu',required=True);p.add_argument('--gpu',required=True);a=p.parse_args();m=json.load(open(a.meta));h,w=m['height'],m['width']
cpu=np.fromfile(a.cpu,np.float32).reshape(h,w,3); bits=np.fromfile(a.gpu,np.uint16).reshape(h,w,4);gpu=bits.view(np.float16).astype(np.float32)[...,:3]
d=np.abs(cpu-gpu); print(f"VNG4_GPU_ORACLE max={d.max():.9g} mean={d.mean():.9g} p99={np.quantile(d,.99):.9g} finite={np.isfinite(gpu).all()}")
# Final output is RGBA16F; bulk parity is the primary fixture gate.
if not np.isfinite(gpu).all() or np.quantile(d,.99)>0.004 or d.mean()>0.001: raise SystemExit('VNG4_GPU_ORACLE_FAIL')
print('VNG4_GPU_ORACLE_PASS')
