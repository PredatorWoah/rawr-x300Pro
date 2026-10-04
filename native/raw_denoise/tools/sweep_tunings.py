#!/usr/bin/env python3
"""Tuning sweep: DT-denoiseprofile oracle over strength x shadows on device DNGs.

Renders viewables with cranked exposure (+EV push) so shadow noise is visible.
Writes per-sample grid PNGs + metrics JSON to ./tmp/denoise_visuals/.

Usage: uv run --no-project --with numpy --with numba --with tifffile --with pillow \\
         native/raw_denoise/tools/sweep_tunings.py
"""
from __future__ import annotations

import glob
import itertools
import json
import os
import sys
import time

import numpy as np

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from denoiseprofile_oracle import load_dng_linear, process_wavelets_y0u0v0

CROP = 512
EV_PUSH = 2.0  # cranked exposure for visibility
OUT = os.path.join("tmp", "denoise_visuals")

STRENGTHS = [1.0, 2.0, 4.0, 8.0]
SHADOWS = [0.5, 1.0]


def render_viewable(linear: np.ndarray, wb: np.ndarray, ev: float = 0.0) -> np.ndarray:
    balanced = np.clip(linear * wb[None, None, :], 0, None) * (2.0 ** ev)
    g = balanced[:, :, 1]
    s = 0.8 / max(np.percentile(g, 99), 1e-6)
    view = np.clip(balanced * s, 0, 1) ** (1 / 2.2)
    return (view * 255 + 0.5).astype(np.uint8)


def flat_patch_stats(img: np.ndarray):
    """Noise proxy: std of high-pass (img - 5x5 box blur) on green, plus luma Laplacian energy."""
    from scipy import ndimage  # noqa
    raise NotImplementedError


def main() -> None:
    from PIL import Image
    os.makedirs(OUT, exist_ok=True)
    t_warm0 = time.perf_counter()
    all_metrics = {}
    for path in sorted(glob.glob(os.path.join("tmp", "device_dng", "*.dng"))):
        stem = os.path.splitext(os.path.basename(path))[0]
        _, rgb, wb, a_norm, b_norm, meta = load_dng_linear(path)
        H, W, _ = rgb.shape
        # darkest tile = shadow stress case
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
        crop = np.ascontiguousarray(rgb[y0:y0 + CROP, x0:x0 + CROP])
        meta["shadow_crop_origin"] = [y0, x0]

        # green-channel high-pass noise estimate on input at working exposure
        g = crop[:, :, 1]
        hp = g - (np.roll(g, 1, 0) + np.roll(g, -1, 0) + np.roll(g, 1, 1) + np.roll(g, -1, 1)) / 4
        in_noise = float(hp[4:-4, 4:-4].std())

        combos = [("off", None, None)]
        for s, sh in itertools.product(STRENGTHS, SHADOWS):
            combos.append((f"s{s:g}_sh{sh:g}", s, sh))

        cells, entry = [], {"meta": meta, "input_hp_std": in_noise, "runs": {}}
        for label, s, sh in combos:
            if s is None:
                out = crop
                dt = 0.0
                scales = 0
            else:
                t0 = time.perf_counter()
                out, scales = process_wavelets_y0u0v0(crop, a_norm, b_norm, wb,
                                                      strength=s, shadows=sh)
                dt = time.perf_counter() - t0
                out = np.clip(out, 0, None)
            go = out[:, :, 1]
            hp_o = go - (np.roll(go, 1, 0) + np.roll(go, -1, 0) + np.roll(go, 1, 1) + np.roll(go, -1, 1)) / 4
            hp_std = float(hp_o[4:-4, 4:-4].std())
            # detail proxy: gradient energy retained vs input (Sobel-lite)
            gx_in = np.abs(np.diff(crop[:, :, 1], axis=1)).mean()
            gx_out = np.abs(np.diff(out[:, :, 1], axis=1)).mean()
            entry["runs"][label] = {
                "seconds": round(dt, 2), "scales": scales,
                "hp_std": round(hp_std, 7),
                "noise_ratio": round(hp_std / in_noise, 4),
                "detail_retained": round(float(gx_out / max(gx_in, 1e-9)), 4),
            }
            view = render_viewable(out, wb, EV_PUSH)
            cells.append((label, Image.fromarray(view)))
            print(f"{stem} {label}: {dt:.1f}s hp {hp_std:.6f} "
                  f"(x{hp_std / in_noise:.3f}) detail {gx_out / max(gx_in, 1e-9):.3f}",
                  flush=True)
        # grid: 3 cols
        cols = 3
        rows = (len(cells) + cols - 1) // cols
        cw, ch = CROP, CROP + 22
        grid = Image.new("RGB", (cols * CROP, rows * ch), (20, 20, 20))
        from PIL import ImageDraw
        dr = ImageDraw.Draw(grid)
        for i, (label, im) in enumerate(cells):
            r, c = divmod(i, cols)
            grid.paste(im, (c * CROP, r * ch + 22))
            dr.text((c * CROP + 6, r * ch + 5), f"{stem} {label} (+{EV_PUSH:g}EV)",
                    fill=(255, 255, 0))
        grid.save(os.path.join(OUT, f"{stem}_shadow_grid.png"))
        all_metrics[stem] = entry
    with open(os.path.join(OUT, "sweep_metrics.json"), "w") as f:
        json.dump(all_metrics, f, indent=2)
    print("TOTAL", round(time.perf_counter() - t_warm0, 1), "s; wrote", OUT)


if __name__ == "__main__":
    main()
