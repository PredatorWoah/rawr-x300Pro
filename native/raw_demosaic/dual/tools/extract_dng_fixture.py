#!/usr/bin/env python3
import argparse, json, pathlib
import numpy as np
import rawpy
ap=argparse.ArgumentParser(description='Extract untouched visible CFA and metadata from a DNG for dual-demosaic device experiments.')
ap.add_argument('dng')
ap.add_argument('--out-dir', required=True)
a=ap.parse_args()
out=pathlib.Path(a.out_dir); out.mkdir(parents=True,exist_ok=True)
with rawpy.imread(a.dng) as raw:
    cfa=np.asarray(raw.raw_image_visible).copy()
    if cfa.dtype != np.uint16:
        if not np.issubdtype(cfa.dtype,np.integer): raise SystemExit(f'unsupported CFA dtype {cfa.dtype}')
        cfa=cfa.astype(np.uint16)
    # raw_pattern indexes raw.color_desc; map the visible 2x2 pattern to canonical RGB letters.
    pat=np.asarray(raw.raw_pattern)
    desc=bytes(raw.color_desc).decode('ascii','ignore')
    if pat.shape != (2,2): raise SystemExit(f'unsupported raw_pattern shape {pat.shape}')
    letters=''.join(desc[int(pat[y,x])] for y in range(2) for x in range(2))
    # Collapse G1/G2 naming naturally; only canonical Bayer is accepted by this package.
    letters=letters.replace('g','G')
    if letters not in ('RGGB','GRBG','GBRG','BGGR'):
        raise SystemExit(f'unsupported Bayer pattern {letters!r} from color_desc={desc!r} pattern={pat.tolist()}')
    black=[float(x) for x in raw.black_level_per_channel]
    white=float(raw.white_level)
    meta={'source':str(pathlib.Path(a.dng).resolve()),'width':int(cfa.shape[1]),'height':int(cfa.shape[0]),'pattern':letters,'black_level_per_channel':black,'white_level':white,'dtype':'uint16-le','samples':int(cfa.size)}
    cfa.astype('<u2',copy=False).tofile(out/'raw_visible.u16')
    (out/'manifest.json').write_text(json.dumps(meta,indent=2)+'\n')
    print('DUAL_DNG_EXTRACT_PASS '+json.dumps(meta,separators=(',',':')))
