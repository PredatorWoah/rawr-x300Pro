#!/usr/bin/env python3
import argparse, hashlib, json, time
from pathlib import Path
import numpy as np
import tifffile
from rcd_numba_core import demosaic_rg

def black_values(v):
    if isinstance(v,tuple) and len(v)==8: return [float(v[i])/float(v[i+1]) for i in range(0,8,2)]
    a=np.asarray(v).reshape(-1); return [float(x) for x in a]

def main():
    ap=argparse.ArgumentParser(); ap.add_argument('dng'); ap.add_argument('--out-dir',required=True); a=ap.parse_args(); out=Path(a.out_dir); out.mkdir(parents=True,exist_ok=True)
    t0=time.perf_counter()
    with tifffile.TiffFile(a.dng) as tf:
        p=tf.pages[0]; raw=p.asarray(); white=float(np.asarray(p.tags['WhiteLevel'].value).reshape(-1)[0]); blacks=black_values(p.tags['BlackLevel'].value)
        cv=p.tags['CFAPattern'].value
        if isinstance(cv,(bytes,bytearray)): cfa=list(cv)
        else:
            arr=np.asarray(cv).reshape(-1)
            if arr.size==1 and isinstance(arr[0],(bytes,bytearray)): cfa=list(arr[0])
            else: cfa=[int(x) for x in arr]
    if cfa != [0,1,1,2]: raise SystemExit(f'FULL_DNG_ORACLE_UNSUPPORTED_CFA {cfa}; expected RGGB')
    h,w=raw.shape; yy=np.arange(h)[:,None]&1; xx=np.arange(w)[None,:]&1; parity=yy*2+xx; b=np.take(np.asarray(blacks,np.float32),parity)
    norm=(raw.astype(np.float32)-b)/(np.float32(white)-b)
    stem=Path(a.dng).stem
    (norm*np.float32(255)).astype('<f4').tofile(out/f'{stem}_cfa_linear255.f32')
    # Warm JIT on a representative crop before timing the full image.
    demosaic_rg(norm[:32,:32].copy())
    t1=time.perf_counter(); rgb=demosaic_rg(norm); t2=time.perf_counter()
    np.save(out/f'{stem}_rcd_cpu.npy',rgb)
    meta={'source':str(Path(a.dng).resolve()),'sha256':hashlib.sha256(Path(a.dng).read_bytes()).hexdigest(),'width':w,'height':h,'pattern':'RGGB','black_level':blacks,'white_level':white,'normalized_min':float(norm.min()),'normalized_max':float(norm.max()),'rgb_min':float(rgb.min()),'rgb_max':float(rgb.max()),'cpu_rcd_seconds':t2-t1,'total_prepare_seconds':t2-t0}
    (out/f'{stem}.json').write_text(json.dumps(meta,indent=2)+'\n')
    print('RCD_FULL_DNG_CPU_ORACLE_PASS '+json.dumps(meta,separators=(',',':')))
if __name__=='__main__': main()
