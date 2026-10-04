#!/usr/bin/env python3
from pathlib import Path
import subprocess, tempfile

ROOT = Path(__file__).resolve().parents[1]
CHECKER = ROOT / "tools/check_waveform_photo_artifacts.py"
W=H=100

def write_ppm(path, mode, frac):
    d=bytearray(W*H*3)
    n=int(W*H*frac)
    used=set(); i=0
    while len(used)<n:
        used.add(i); i=(i+37)%(W*H)
    for i in used:
        d[i*3:i*3+3] = bytes((24,10,7) if mode=="rgb" else (18,18,18))
    with path.open("wb") as f:
        f.write(f"P6\n{W} {H}\n255\n".encode()); f.write(d)

def make(d,bfrac):
    for base in ("portrait_a","portrait_b"):
        for mode in ("rgb","luma"):
            write_ppm(d/f"{base}_waveform_{mode}_data.ppm", mode,
                      0.70 if base=="portrait_a" else bfrac)

with tempfile.TemporaryDirectory() as td:
    d=Path(td); make(d,0.868)
    p=subprocess.run(["uv","run","--no-project",str(CHECKER),str(d)],text=True,capture_output=True)
    if p.returncode:
        raise SystemExit("dense-valid fixture rejected:\\n"+p.stdout+p.stderr)

with tempfile.TemporaryDirectory() as td:
    d=Path(td); make(d,1.0)
    p=subprocess.run(["uv","run","--no-project",str(CHECKER),str(d)],text=True,capture_output=True)
    if p.returncode==0 or "near-total raster fill" not in p.stdout:
        raise SystemExit("near-total corruption fixture was not rejected")

print("WAVEFORM_PHOTO_ARTIFACT_CHECKER_SELFTEST_PASS")
