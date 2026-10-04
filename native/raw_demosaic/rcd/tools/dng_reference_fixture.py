#!/usr/bin/env python3
import argparse, hashlib, json
from pathlib import Path
import numpy as np
import tifffile
from rcd_reference import demosaic

def rational(v):
    if isinstance(v, tuple) and len(v)==2 and all(isinstance(x,(int,np.integer)) for x in v): return v[0]/v[1]
    if isinstance(v, tuple) and len(v)>=2 and len(v)%2==0: return v[0]/v[1]
    return float(v)

def main():
    ap=argparse.ArgumentParser(); ap.add_argument('dng'); ap.add_argument('--out-dir',required=True); ap.add_argument('--crop',type=int,default=96); ap.add_argument('--pattern',default='RGGB')
    a=ap.parse_args(); out=Path(a.out_dir); out.mkdir(parents=True,exist_ok=True)
    with tifffile.TiffFile(a.dng) as tf:
        p=tf.pages[0]; raw=p.asarray(); white=float(p.tags['WhiteLevel'].value)
        bl=p.tags['BlackLevel'].value
        # DNG rational array from tifffile may be flattened numerator/denominator pairs.
        if isinstance(bl,tuple) and len(bl)==8: blacks=[bl[i]/bl[i+1] for i in range(0,8,2)]
        else: blacks=[float(x) for x in bl]
    n=a.crop; y=((raw.shape[0]-n)//2)&~1; x=((raw.shape[1]-n)//2)&~1; crop=raw[y:y+n,x:x+n].astype(np.float32)
    norm=np.empty_like(crop,dtype=np.float32)
    for yy in range(n):
        for xx in range(n):
            parity=((yy&1)<<1)|(xx&1); b=float(blacks[parity]); norm[yy,xx]=(crop[yy,xx]-b)/(white-b)
    rgb=demasaic= demosaic(norm,a.pattern,'rawr')
    stem=Path(a.dng).stem
    np.save(out/f'{stem}_cfa.npy',norm)
    # NormalizedBayerBufferView uses the established 0..255
    # working-domain contract. The CPU RCD oracle stays in nominal 0..1, while
    # the Vulkan fixture must therefore be scaled by 255 before upload.
    (norm * np.float32(255.0)).astype('<f4').tofile(out/f'{stem}_cfa.f32')
    np.save(out/f'{stem}_rcd_rawr.npy',rgb)
    # compact PFM for visual/debug tools
    with open(out/f'{stem}_rcd_rawr.pfm','wb') as f:
        f.write(f'PF\n{n} {n}\n-1.0\n'.encode()); np.flipud(rgb.astype('<f4')).tofile(f)
    sha=hashlib.sha256(Path(a.dng).read_bytes()).hexdigest()
    meta={'source':Path(a.dng).name,'sha256':sha,'source_shape':[int(raw.shape[1]),int(raw.shape[0])],'crop_xywh':[x,y,n,n],'pattern':a.pattern,'black_level':blacks,'white_level':white,'normalized_min':float(norm.min()),'normalized_max':float(norm.max()),'rgb_min':float(rgb.min()),'rgb_max':float(rgb.max())}
    (out/f'{stem}.json').write_text(json.dumps(meta,indent=2)+'\n')
    print('DNG_RCD_REFERENCE_PASS',json.dumps(meta,separators=(',',':')))
if __name__=='__main__': main()
