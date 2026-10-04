#!/usr/bin/env python3
"""Experiment v3: per-band Y/UV forces + scale count, at fixed s2_sh1.

Configs (force lists are per-scale, scale 0 = finest):
  base    Y=.5 UV=.5 (DT defaults)
  chroma  Y=.25 UV=.75 (chroma-first: kill blotches, spare luma detail)
  profine Y=[.15,.2,.35,.5,.6,.6] UV=.6 (spare fine luma, clean coarse)
  both    Y=[.15,.2,.35,.5,.6,.6] UV=.75
  uvgrad  Y=.25 UV=[.5,.5,.5,.75,.9,.9] (UV ramps into coarse scales)
  s4      base forces, max_scale=4 (large-blotch behavior check)

Usage: uv run --no-project --with numpy --with numba --with tifffile --with pillow \\
         native/raw_denoise/tools/experiment_v3.py
Output: ./tmp/denoise_visuals/v3/
"""
from __future__ import annotations

import json
import os
import sys
import time

import numpy as np

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from denoiseprofile_oracle import load_dng_linear, process_wavelets_y0u0v0
from sweep_v2 import metrics_for, render, chroma_view, CROP, EV

OUT = os.path.join("tmp", "denoise_visuals", "v3")
TARGETS = {
    "RAWR_20260926_000415_161": "books-text",
    "RAWR_20260926_000643_129": "flowers-fiber",
    "RAWR_20260926_000405_865": "tele-dark",
}
CONFIGS = {
    "base": dict(force=0.5, force_uv=0.5, max_scale=None),
    "chroma": dict(force=0.25, force_uv=0.75, max_scale=None),
    "profine": dict(force=[.15, .2, .35, .5, .6, .6], force_uv=0.6, max_scale=None),
    "both": dict(force=[.15, .2, .35, .5, .6, .6], force_uv=0.75, max_scale=None),
    "uvgrad": dict(force=0.25, force_uv=[.5, .5, .5, .75, .9, .9], max_scale=None),
    "s4": dict(force=0.5, force_uv=0.5, max_scale=4),
}


def texture_crop(rgb: np.ndarray) -> np.ndarray:
    from sweep_v2 import sobel_energy
    H, W, _ = rgb.shape
    small = rgb[::4, ::4, 1]
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
    return np.ascontiguousarray(rgb[y0:y0 + CROP, x0:x0 + CROP])


def main() -> None:
    from PIL import Image, ImageDraw
    os.makedirs(OUT, exist_ok=True)
    t0 = time.perf_counter()
    summary = {}
    for stem, tag in TARGETS.items():
        path = os.path.join("tmp", "device_dng", stem + ".dng")
        _, rgb, wb, a_norm, b_norm, meta = load_dng_linear(path)
        crop = texture_crop(rgb)
        entry = {"meta": meta, "runs": {}}
        cells, chromas = [], []
        cells.append(("off", Image.fromarray(render(crop, wb))))
        chromas.append(("off", Image.fromarray(chroma_view(crop, wb))))
        entry["runs"]["off"] = metrics_for(crop, crop, wb)
        for name, kw in CONFIGS.items():
            t1 = time.perf_counter()
            out, scales = process_wavelets_y0u0v0(crop, a_norm, b_norm, wb,
                                                  strength=2.0, shadows=1.0, **kw)
            dt = time.perf_counter() - t1
            out = np.clip(out, 0, None)
            m = metrics_for(crop, out, wb)
            m.update({"seconds": round(dt, 2), "scales": scales})
            entry["runs"][name] = m
            cells.append((name, Image.fromarray(render(out, wb))))
            chromas.append((name, Image.fromarray(chroma_view(out, wb))))
            print(f"{tag} {name}: {dt:.1f}s hp={m['hp_ratio']} "
                  f"chr={m['chroma_p99']} bleed={m['edge_bleed']} tex={m['texture_ret']}",
                  flush=True)
        for suffix, imgs in (("grid", cells), ("chroma", chromas)):
            cols = 3
            rows = (len(imgs) + cols - 1) // cols
            sheet = Image.new("RGB", (cols * CROP, rows * (CROP + 22)), (20, 20, 20))
            dr = ImageDraw.Draw(sheet)
            for i, (label, im) in enumerate(imgs):
                r, c = divmod(i, cols)
                sheet.paste(im, (c * CROP, r * (CROP + 22) + 22))
                dr.text((c * CROP + 6, r * (CROP + 22) + 5),
                        f"{tag} {label} s2_sh1 (+{EV:g}EV)", fill=(255, 255, 0))
            sheet.save(os.path.join(OUT, f"{tag}_{suffix}.png"))
        summary[stem] = entry
    json.dump(summary, open(os.path.join(OUT, "v3_metrics.json"), "w"), indent=2)
    print("TOTAL", round(time.perf_counter() - t0, 1), "s; wrote", OUT)


if __name__ == "__main__":
    main()
