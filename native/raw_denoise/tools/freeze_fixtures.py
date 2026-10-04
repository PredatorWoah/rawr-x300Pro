#!/usr/bin/env python3
"""Freeze golden fixtures for GPU parity: input/output float32 pairs + manifest.

Crops reuse the deterministic most-textured-tile selection (see sweep_v2).
Goldens pin the frozen v1 defaults (FORCE_Y=0.25, FORCE_UV=0.75) at the two
shipped tunings: s1_sh1 (default) and s2_sh1 (balanced).

Usage: uv run --no-project --with numpy --with numba --with tifffile \\
         native/raw_denoise/tools/freeze_fixtures.py
Output: native/raw_denoise/tests/data/<stem>_<tuning>_{input,output}.f32 + manifest.json
"""
from __future__ import annotations

import json
import os
import sys

import numpy as np

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from denoiseprofile_oracle import (
    F32, FORCE_UV, FORCE_Y, load_dng_linear, process_wavelets_y0u0v0,
)
from sweep_v2 import CROP, sobel_energy

OUT = os.path.join("native", "raw_denoise", "tests", "data")
TARGETS = [
    "RAWR_20260926_000415_161",  # books text (main ISO3200)
    "RAWR_20260926_000643_129",  # flowers fiber (main ISO2528)
    "RAWR_20260926_000405_865",  # tele dark (tele ISO6400)
]
TUNINGS = [(1.0, 1.0), (2.0, 1.0)]


def texture_crop(rgb: np.ndarray):
    H, W, _ = rgb.shape
    small = np.asarray(rgb[::4, ::4, 1], dtype=np.float64)
    e = sobel_energy(small)
    gh, gw = e.shape[0] // 3, e.shape[1] // 4
    best, bestv = (0, 0), -1.0
    for gy in range(3):
        for gx in range(4):
            v = e[gy * gh:(gy + 1) * gh, gx * gw:(gx + 1) * gw].mean()
            if v > bestv:
                bestv, best = v, (gy * gh * 4, gx * gw * 4)
    y0 = min(max(best[0], 0), H - CROP)
    x0 = min(max(best[1], 0), W - CROP)
    return np.ascontiguousarray(rgb[y0:y0 + CROP, x0:x0 + CROP].astype(F32)), [y0, x0]


def main() -> None:
    os.makedirs(OUT, exist_ok=True)
    manifest = {
        "oracle": "denoiseprofile_oracle.py (DT denoiseprofile wavelets/Y0U0V0/new-VST)",
        "precision": "float32",
        "force_y": float(FORCE_Y),
        "force_uv": float(FORCE_UV),
        "bias": 0.0,
        "fixtures": [],
    }
    for stem in TARGETS:
        path = os.path.join("tmp", "device_dng", stem + ".dng")
        _, rgb, wb, a_norm, b_norm, meta = load_dng_linear(path)
        crop, origin = texture_crop(rgb)
        # GPU layout is RGBA32F (.a unused, like DT's aligned pixels).
        crop4 = np.zeros((CROP, CROP, 4), dtype=F32)
        crop4[:, :, :3] = crop
        crop_path = os.path.join(OUT, f"{stem}_crop_input.f32")
        crop4.astype(F32).tofile(crop_path)
        for s, sh in TUNINGS:
            label = f"s{s:g}_sh{sh:g}"
            out, scales = process_wavelets_y0u0v0(crop, a_norm, b_norm, wb,
                                                  strength=s, shadows=sh)
            out = np.clip(out, 0, None).astype(F32)
            out_path = os.path.join(OUT, f"{stem}_{label}_output.f32")
            out.tofile(out_path)
            manifest["fixtures"].append({
                "stem": stem, "tuning": label,
                "crop": [CROP, CROP], "origin": origin,
                "channels": 4,
                "strength": s, "shadows": sh,
                "wb": [float(v) for v in wb],
                "a_norm": float(a_norm), "b_norm": float(b_norm),
                "scales": scales,
                "input": os.path.basename(crop_path),
                "output": os.path.basename(out_path),
                "dng": os.path.basename(path),
            })
            print(f"{stem} {label}: scales={scales} mean={out.mean():.7f}", flush=True)
    with open(os.path.join(OUT, "manifest.json"), "w") as f:
        json.dump(manifest, f, indent=2)
    print("wrote", OUT)


if __name__ == "__main__":
    main()
