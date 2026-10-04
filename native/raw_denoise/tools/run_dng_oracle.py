#!/usr/bin/env python3
"""Run DT-denoiseprofile oracle on crops of device DNGs; write visuals + metrics.

Usage: uv run --no-project --with numpy --with numba --with tifffile --with pillow \\
         native/raw_denoise/tools/run_dng_oracle.py
Output: ./tmp/denoise_visuals/<dng_stem>/{center,shadow}_{before,after}.png + metrics.json
"""
from __future__ import annotations

import glob
import json
import os
import sys
import time

import numpy as np

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from denoiseprofile_oracle import load_dng_linear, process_wavelets_y0u0v0

CROP = 512
OUT = os.path.join("tmp", "denoise_visuals")


def render_viewable(linear: np.ndarray, wb: np.ndarray) -> np.ndarray:
    balanced = np.clip(linear * wb[None, None, :], 0, None)
    # simple auto-exposure: scale so p99 green ~0.8, then gamma
    g = balanced[:, :, 1]
    s = 0.8 / max(np.percentile(g, 99), 1e-6)
    view = np.clip(balanced * s, 0, 1) ** (1 / 2.2)
    return (view * 255 + 0.5).astype(np.uint8)


def main() -> None:
    from PIL import Image
    os.makedirs(OUT, exist_ok=True)
    metrics = {}
    for path in sorted(glob.glob(os.path.join("tmp", "device_dng", "*.dng"))):
        stem = os.path.splitext(os.path.basename(path))[0]
        _, rgb, wb, a_norm, b_norm, meta = load_dng_linear(path)
        H, W, _ = rgb.shape
        crops = {
            "center": rgb[H // 2 - CROP // 2:H // 2 + CROP // 2,
                          W // 2 - CROP // 2:W // 2 + CROP // 2],
        }
        # darkest tile (lowest luma mean over a 4x3 grid)
        best, bestv = (0, 0), 1e9
        for gy in range(3):
            for gx in range(4):
                y0, x0 = gy * H // 3, gx * W // 4
                tile = rgb[y0:y0 + CROP, x0:x0 + CROP]
                if tile.shape[:2] != (CROP, CROP):
                    continue
                v = tile.mean()
                if v < bestv:
                    bestv, best = v, (y0, x0)
        y0, x0 = best
        crops["shadow"] = rgb[y0:y0 + CROP, x0:x0 + CROP]
        entry = {"meta": meta, "crops": {}}
        for name, crop in crops.items():
            t0 = time.perf_counter()
            out, scales = process_wavelets_y0u0v0(crop, a_norm, b_norm, wb)
            dt = time.perf_counter() - t0
            out = np.clip(out, 0, None)
            before = render_viewable(crop, wb)
            after = render_viewable(out, wb)
            Image.fromarray(before).save(os.path.join(OUT, f"{stem}_{name}_before.png"))
            Image.fromarray(after).save(os.path.join(OUT, f"{stem}_{name}_after.png"))
            resid = (crop - out).reshape(-1, 3)
            entry["crops"][name] = {
                "seconds": round(dt, 2),
                "scales": scales,
                "resid_std_rgb": [round(float(v), 6) for v in resid.std(0)],
                "resid_mean_abs_rgb": [round(float(v), 6) for v in np.abs(resid).mean(0)],
                "out_mean": round(float(out.mean()), 6),
                "in_mean": round(float(crop.mean()), 6),
            }
            print(f"{stem}/{name}: {dt:.1f}s scales={scales} "
                  f"resid_std={entry['crops'][name]['resid_std_rgb']}")
        metrics[stem] = entry
    with open(os.path.join(OUT, "metrics.json"), "w") as f:
        json.dump(metrics, f, indent=2)
    print("wrote", OUT)


if __name__ == "__main__":
    main()
