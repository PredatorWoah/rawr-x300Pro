#!/usr/bin/env python3
import argparse, pathlib, numpy as np
ap=argparse.ArgumentParser()
ap.add_argument('--width',type=int,required=True)
ap.add_argument('--height',type=int,required=True)
ap.add_argument('--out',required=True)
ap.add_argument('--mode',choices=['transition','stress'],default='transition')
a=ap.parse_args()
y,x=np.mgrid[0:a.height,0:a.width]
if a.mode=='transition':
    raw=(0.12+0.38*x/max(a.width-1,1)+0.20*y/max(a.height-1,1)+0.003*np.sin(x*.61)+0.002*np.cos(y*.47)).astype(np.float32)
else:
    raw=(0.08+0.45*x/max(a.width-1,1)+0.20*y/max(a.height-1,1)+0.006*np.sin(x*.71)+0.004*np.cos(y*.43)).astype(np.float32)
    raw[:,a.width//2:]+=np.float32(0.18)
    patch=((x//2+y//2)&1).astype(np.float32)
    m=(x>max(2,a.width//12))&(x<max(4,(5*a.width)//12))&(y>(3*a.height)//5)&(y<(9*a.height)//10)
    raw[m]+=np.float32(0.06)*(patch[m]-np.float32(0.5))
    raw[(x==(3*a.width)//4)|(y==a.height//4)]+=np.float32(0.12)
    raw=np.clip(raw,np.float32(0.0),np.float32(1.25))
raw255=(raw*np.float32(255.0)).astype(np.float32)
pathlib.Path(a.out).parent.mkdir(parents=True,exist_ok=True)
raw255.tofile(a.out)
print(f'DUAL_FIXTURE_PASS mode={a.mode} path={a.out} min={raw255.min():.6f} max={raw255.max():.6f}')
