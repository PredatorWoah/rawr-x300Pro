#!/usr/bin/env python3
import argparse, hashlib, json, time
from pathlib import Path
import numpy as np
import tifffile
from vng4_reference import demosaic

PATTERNS = {
    (0,1,1,2): (0,'RGGB'),
    (1,0,2,1): (1,'GRBG'),
    (1,2,0,1): (2,'GBRG'),
    (2,1,1,0): (3,'BGGR'),
}

def rational_values(v):
    if isinstance(v, tuple) and len(v) and len(v) % 2 == 0:
        try:
            return [float(v[i]) / float(v[i+1]) for i in range(0, len(v), 2)]
        except Exception:
            pass
    a=np.asarray(v).reshape(-1)
    out=[]
    for x in a:
        try: out.append(float(x))
        except TypeError:
            # tifffile rationals can be pair-like
            out.append(float(x[0])/float(x[1]))
    return out

def expand_black(vals):
    if len(vals)==1: return vals*4
    if len(vals)==2: return [vals[0],vals[1],vals[0],vals[1]]
    if len(vals)>=4: return vals[:4]
    raise RuntimeError(f'unsupported BlackLevel count {len(vals)}')

def cfa_bytes(v):
    if isinstance(v,(bytes,bytearray)): return tuple(int(x) for x in v)
    a=np.asarray(v).reshape(-1)
    if a.size==1 and isinstance(a[0],(bytes,bytearray)): return tuple(int(x) for x in a[0])
    return tuple(int(x) for x in a)

def main():
    ap=argparse.ArgumentParser()
    ap.add_argument('dng')
    ap.add_argument('--out-dir',required=True)
    a=ap.parse_args(); dng=Path(a.dng); out=Path(a.out_dir); out.mkdir(parents=True,exist_ok=True)
    t0=time.perf_counter()
    with tifffile.TiffFile(dng) as tf:
        p=tf.pages[0]
        raw=p.asarray()
        if raw.ndim!=2: raise RuntimeError(f'expected 2D CFA, got shape={raw.shape}')
        white=float(np.asarray(p.tags['WhiteLevel'].value).reshape(-1)[0])
        blacks=expand_black(rational_values(p.tags['BlackLevel'].value))
        cfa=cfa_bytes(p.tags['CFAPattern'].value)
    if cfa not in PATTERNS:
        raise SystemExit(f'VNG4_FULL_DNG_UNSUPPORTED_CFA {list(cfa)}')
    pattern,pname=PATTERNS[cfa]
    h,w=raw.shape
    yy=np.arange(h,dtype=np.int32)[:,None]&1; xx=np.arange(w,dtype=np.int32)[None,:]&1
    parity=yy*2+xx
    b=np.take(np.asarray(blacks,np.float32),parity)
    denom=np.float32(white)-b
    if np.any(denom<=0): raise RuntimeError('WhiteLevel must exceed BlackLevel for all CFA parities')
    norm=(raw.astype(np.float32)-b)/denom
    stem=dng.stem
    # NormalizedFloatBuffer's historical wire representation is AHD255 float32.
    # The GPU cfa() converts it back with float32 * (1/255).  The CPU oracle must
    # consume that exact effective input, not the pre-roundtrip `norm`, otherwise
    # a 1-ULP input mismatch can flip VNG's <= threshold and create a false ~0.07
    # algorithm mismatch.
    gpu_wire=(norm*np.float32(255.0)).astype(np.float32)
    gpu_wire.astype('<f4').tofile(out/f'{stem}_cfa_linear255.f32')
    oracle_norm=(gpu_wire*np.float32(1.0/255.0)).astype(np.float32)
    # JIT warm-up with same CFA pattern.
    demosaic(oracle_norm[:32,:32].copy(),pattern)
    t1=time.perf_counter(); rgb=demosaic(oracle_norm,pattern); t2=time.perf_counter()
    np.save(out/f'{stem}_vng4_cpu.npy',rgb)
    meta={
        'source':str(dng.resolve()), 'sha256':hashlib.sha256(dng.read_bytes()).hexdigest(),
        'width':int(w),'height':int(h),'pattern':pname,'pattern_id':int(pattern),
        'cfa_pattern':list(cfa),'black_level':blacks,'white_level':white,
        'normalized_min':float(oracle_norm.min()),'normalized_max':float(oracle_norm.max()),'pre_wire_normalized_min':float(norm.min()),'pre_wire_normalized_max':float(norm.max()),
        'rgb_min':float(rgb.min()),'rgb_max':float(rgb.max()),
        'cpu_vng4_seconds':float(t2-t1),'total_prepare_seconds':float(t2-t0),
    }
    (out/f'{stem}.json').write_text(json.dumps(meta,indent=2)+'\n')
    print('VNG4_FULL_DNG_CPU_ORACLE_PASS '+json.dumps(meta,separators=(',',':')))
if __name__=='__main__': main()
