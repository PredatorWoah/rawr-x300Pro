#!/usr/bin/env python3
from pathlib import Path
import sys

if len(sys.argv) != 2:
    raise SystemExit("usage: check_waveform_photo_artifacts.py ARTIFACT_DIR")

root = Path(sys.argv[1])
ACTIVE_THRESHOLD = 6
CHROMA_THRESHOLD = 3
NEAR_TOTAL_FILL = 0.97

def tok(f):
    while True:
        x = f.readline()
        if not x:
            raise ValueError("EOF")
        x = x.strip()
        if x and not x.startswith(b"#"):
            return x

def ppm(p):
    with p.open("rb") as f:
        if tok(f) != b"P6":
            raise ValueError(f"{p}: not P6")
        wh = tok(f).split()
        while len(wh) < 2:
            wh += tok(f).split()
        w, h = map(int, wh[:2])
        if int(tok(f)) != 255:
            raise ValueError(f"{p}: maxval")
        d = f.read(w * h * 3)
        if len(d) != w * h * 3:
            raise ValueError(f"{p}: short")
        return w, h, d

def check(base, mode):
    p = root / f"{base}_waveform_{mode}_data.ppm"
    w, h, d = ppm(p)
    active = chrom = 0
    minx, miny, maxx, maxy = w, h, -1, -1
    for i in range(w * h):
        r, g, b = d[i*3:i*3+3]
        mx, mn = max(r,g,b), min(r,g,b)
        if mx >= ACTIVE_THRESHOLD:
            active += 1
            x, y = i % w, i // w
            minx, maxx = min(minx,x), max(maxx,x)
            miny, maxy = min(miny,y), max(maxy,y)
            if mx - mn >= CHROMA_THRESHOLD:
                chrom += 1
    frac = active / (w*h)
    if active < 200:
        raise ValueError(f"{base}/{mode}: nearly empty ({active})")
    # Waveform E is density-adaptive. High occupancy is legitimate; only
    # near-total raster contamination is treated as an explosion/corruption.
    if frac >= NEAR_TOTAL_FILL:
        raise ValueError(f"{base}/{mode}: near-total raster fill ({frac:.1%})")
    if maxx-minx < 20 or maxy-miny < 20:
        raise ValueError(f"{base}/{mode}: collapsed bbox")
    if mode == "rgb" and chrom < 100:
        raise ValueError(f"{base}/rgb: insufficient channel separation ({chrom})")
    if mode == "luma" and chrom != 0:
        raise ValueError(f"{base}/luma: non-monochrome pixels ({chrom})")
    print(f"{base}/{mode}: active={active} ({frac:.2%}) chromatic={chrom} bbox={maxx-minx+1}x{maxy-miny+1}")

try:
    for b in ["portrait_a","portrait_b"]:
        check(b,"rgb")
        check(b,"luma")
except Exception as e:
    print("WAVEFORM_PHOTO_ARTIFACT_FAIL:", e)
    raise SystemExit(1)
print("WAVEFORM_PHOTO_ARTIFACT_PASS")
