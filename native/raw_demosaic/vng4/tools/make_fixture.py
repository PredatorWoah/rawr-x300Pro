import argparse, json, numpy as np, sys
from pathlib import Path
sys.path.insert(0,str(Path(__file__).resolve().parent))
from vng4_reference import demosaic
p=argparse.ArgumentParser();p.add_argument('--out',required=True);p.add_argument('--width',type=int,default=96);p.add_argument('--height',type=int,default=96);p.add_argument('--pattern',type=int,default=0);a=p.parse_args()
y,x=np.mgrid[0:a.height,0:a.width]
raw=(0.055+0.73*x/max(a.width-1,1)+0.09*np.sin(y*.29)+0.055*np.cos((x+2*y)*.17)+0.12*((x>a.width//3)&(x<a.width//3+5))).astype(np.float32)
out=Path(a.out);out.mkdir(parents=True,exist_ok=True)
(raw*255.0).astype(np.float32).tofile(out/'cfa_linear255.f32')
demosaic(raw,a.pattern).astype(np.float32).tofile(out/'cpu_rgb.f32')
(out/'meta.json').write_text(json.dumps({'width':a.width,'height':a.height,'pattern':a.pattern},indent=2))
print(f'VNG4_FIXTURE_PASS width={a.width} height={a.height} pattern={a.pattern}')
