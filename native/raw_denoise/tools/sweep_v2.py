#!/usr/bin/env python3
"""Sweep v2: fidelity criteria — chroma bleed + texture/sharpness retention.

Crop: most-textured 512 tile per DNG (max Sobel energy over 4x3 grid).
Combos: off + strength{1,2,4} x shadows{0.5,1.0}.
Metrics per run:
  hp_ratio      green high-pass std vs input (luma grain removal)
  chroma_shift  mean/p99 of |dUV| in YUV (color fidelity, bleed proxy)
  edge_bleed    chroma gradient across strong luma edges, out vs in (>1 = bleed)
  texture_ret   HF energy retained inside textured mask
Views: normal +2EV + chroma-only (luma-flattened) for best candidates.

Usage: uv run --no-project --with numpy --with numba --with tifffile --with pillow \\
         native/raw_denoise/tools/sweep_v2.py [substring-filter]
Output: ./tmp/denoise_visuals/v2/
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
EV = 2.0
OUT = os.path.join("tmp", "denoise_visuals", "v2")
COMBOS = [(1.0, 0.5), (1.0, 1.0), (2.0, 0.5), (2.0, 1.0), (4.0, 0.5), (4.0, 1.0)]

RGB2YUV = np.array([[0.299, 0.587, 0.114],
                    [-0.169, -0.331, 0.5],
                    [0.5, -0.419, -0.081]])


def to_yuv(v: np.ndarray) -> np.ndarray:
    return v @ RGB2YUV.T


def render(v: np.ndarray, wb: np.ndarray) -> np.ndarray:
    b = np.clip(v * wb[None, None, :] * (2.0 ** EV), 0, None)
    s = 0.8 / max(np.percentile(b[:, :, 1], 99), 1e-6)
    return (np.clip(b * s, 0, 1) ** (1 / 2.2) * 255 + 0.5).astype(np.uint8)


def chroma_view(v: np.ndarray, wb: np.ndarray) -> np.ndarray:
    """Luma-flattened chroma: chromaticity amplified around gray."""
    b = np.clip(v * wb[None, None, :], 0, None)
    luma = (b @ np.array([0.299, 0.587, 0.114]))[:, :, None]
    chrom = (b - luma) / np.maximum(luma, 0.02)
    disp = np.clip(0.5 + chrom * 1.5, 0, 1)
    return (disp * 255 + 0.5).astype(np.uint8)


def sobel_energy(g: np.ndarray) -> np.ndarray:
    gx = np.abs(g[:, 1:] - g[:, :-1])
    gy = np.abs(g[1:, :] - g[:-1, :])
    e = np.zeros_like(g)
    e[:, 1:] += gx
    e[1:, :] += gy
    return e


def metrics_for(inp: np.ndarray, out: np.ndarray, wb: np.ndarray) -> dict:
    b_in = np.clip(inp * wb[None, None, :], 0, None)
    b_out = np.clip(out * wb[None, None, :], 0, None)
    yuv_in, yuv_out = to_yuv(b_in), to_yuv(b_out)
    # luma grain
    gi, go = b_in[:, :, 1], b_out[:, :, 1]
    hpi = gi - (np.roll(gi, 1, 0) + np.roll(gi, -1, 0) + np.roll(gi, 1, 1) + np.roll(gi, -1, 1)) / 4
    hpo = go - (np.roll(go, 1, 0) + np.roll(go, -1, 0) + np.roll(go, 1, 1) + np.roll(go, -1, 1)) / 4
    hp_ratio = float(hpo[4:-4, 4:-4].std() / max(hpi[4:-4, 4:-4].std(), 1e-12))
    # chroma shift
    duv = np.sqrt(((yuv_out - yuv_in)[:, :, 1:3] ** 2).sum(-1))
    # edge-bleed: chroma gradient magnitude at strong luma edges
    se = sobel_energy(yuv_in[:, :, 0])
    edge = se > np.percentile(se, 90)
    ce_in = sobel_energy(yuv_in[:, :, 1]) + sobel_energy(yuv_in[:, :, 2])
    ce_out = sobel_energy(yuv_out[:, :, 1]) + sobel_energy(yuv_out[:, :, 2])
    bleed = float(ce_out[edge].mean() / max(ce_in[edge].mean(), 1e-12))
    # texture retention: HF energy in textured (non-edge, high-detail) mask
    tex = (se > np.percentile(se, 40)) & (~edge)
    tr = float(ce_out[tex].mean() / max(ce_in[tex].mean(), 1e-12)) if tex.any() else 1.0
    return {
        "hp_ratio": round(hp_ratio, 4),
        "chroma_mean": round(float(duv.mean()), 6),
        "chroma_p99": round(float(np.percentile(duv, 99)), 6),
        "edge_bleed": round(bleed, 4),
        "texture_ret": round(tr, 4),
    }


def main() -> None:
    from PIL import Image, ImageDraw
    filt = sys.argv[1] if len(sys.argv) > 1 else "20260926"
    os.makedirs(OUT, exist_ok=True)
    t0 = time.perf_counter()
    summary = {}
    for path in sorted(glob.glob(os.path.join("tmp", "device_dng", "*.dng"))):
        if filt not in path:
            continue
        stem = os.path.splitext(os.path.basename(path))[0]
        _, rgb, wb, a_norm, b_norm, meta = load_dng_linear(path)
        H, W, _ = rgb.shape
        # most-textured tile
        small = rgb[::4, ::4, 1]
        e = sobel_energy(small)
        gh, gw = e.shape[0] // 3, e.shape[1] // 4
        best, bestv = (0, 0), -1.0
        for gy in range(3):
            for gx in range(4):
                v = e[gy * gh:(gy + 1) * gh, gx * gw:(gx + 1) * gw].mean()
                if v > bestv:
                    bestv, best = v, (gy * gh * 4, gx * gw * 4)
        y0, x0 = best
        y0 = min(max(y0, 0), H - CROP)
        x0 = min(max(x0, 0), W - CROP)
        crop = np.ascontiguousarray(rgb[y0:y0 + CROP, x0:x0 + CROP])
        meta["texture_crop_origin"] = [y0, x0]
        entry = {"meta": meta, "runs": {}}
        cells, chromas = [], []
        base = metrics_for(crop, crop, wb)
        base.update({"seconds": 0.0, "scales": 0})
        entry["runs"]["off"] = base
        cells.append(("off", Image.fromarray(render(crop, wb))))
        chromas.append(("off", Image.fromarray(chroma_view(crop, wb))))
        for s, sh in COMBOS:
            label = f"s{s:g}_sh{sh:g}"
            t1 = time.perf_counter()
            out, scales = process_wavelets_y0u0v0(crop, a_norm, b_norm, wb,
                                                  strength=s, shadows=sh)
            dt = time.perf_counter() - t1
            out = np.clip(out, 0, None)
            m = metrics_for(crop, out, wb)
            m.update({"seconds": round(dt, 2), "scales": scales})
            entry["runs"][label] = m
            cells.append((label, Image.fromarray(render(out, wb))))
            chromas.append((label, Image.fromarray(chroma_view(out, wb))))
            print(f"{stem} {label}: {dt:.1f}s hp={m['hp_ratio']} "
                  f"chroma_p99={m['chroma_p99']} bleed={m['edge_bleed']} tex={m['texture_ret']}",
                  flush=True)
        for tag, imgs in (("grid", cells), ("chroma", chromas)):
            cols = 3
            rows = (len(imgs) + cols - 1) // cols
            sheet = Image.new("RGB", (cols * CROP, rows * (CROP + 22)), (20, 20, 20))
            dr = ImageDraw.Draw(sheet)
            for i, (label, im) in enumerate(imgs):
                r, c = divmod(i, cols)
                sheet.paste(im, (c * CROP, r * (CROP + 22) + 22))
                dr.text((c * CROP + 6, r * (CROP + 22) + 5),
                        f"{stem} {label} (+{EV:g}EV)", fill=(255, 255, 0))
            sheet.save(os.path.join(OUT, f"{stem}_{tag}.png"))
        summary[stem] = entry
    with open(os.path.join(OUT, "v2_metrics.json"), "w") as f:
        json.dump(summary, f, indent=2)
    print("TOTAL", round(time.perf_counter() - t0, 1), "s; wrote", OUT)


if __name__ == "__main__":
    main()
