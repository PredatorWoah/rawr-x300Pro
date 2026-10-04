#!/usr/bin/env python3
"""Grid + stats for video demosaic hypotheses. Follows compare.py conventions.
Runner outputs are pre-WB sensor-linear; WB is applied here for display/metrics.
Timings come from runner JSON (full-res dispatch, always).
"""
import json
import subprocess
import sys
from pathlib import Path

import numpy as np
from PIL import Image, ImageDraw, ImageFont

ROOT = Path(__file__).resolve().parents[2]
OUT = ROOT / "tmp/video_demosaic"
DNGS = {
    "RAWR_20260929_005904_255": {"white": 16383.0, "wb": [1.6704726, 1.0, 2.2164497], "size": (3072, 4080)},
    "RAWR_20260929_005911_948": {"white": 8712.0, "wb": [1.6125989, 1.0, 2.4439123], "size": (3064, 4080)},
}
ARGS = sys.argv[1:]
NO_AHD = "--no-ahd" in ARGS  # omit AHD panel; AHD still used as metric reference
VARIANTS = [a for a in ARGS if not a.startswith("--")] or ["H0"]  # e.g. H0 H1 H2 H3 H4
CENTER = (1200, 800)  # w,h center crop at 100%


def gpu_output(stem, variant):
    p = OUT / f"{stem}_{variant}.rgba16f"
    h, w = DNGS[stem]["size"]
    return np.memmap(p, dtype=np.float16, mode="r", shape=(h, w, 4))[:, :, :3].astype(np.float32)


def apply_wb(linear, wb):
    out = linear.copy()
    out[:, :, 0] *= wb[0]
    out[:, :, 1] *= wb[1]
    out[:, :, 2] *= wb[2]
    return np.maximum(out, 0)


def image_of(linear, exposure):
    display = np.clip(linear / exposure, 0, 1) ** (1 / 2.2)
    return Image.fromarray(np.uint8(np.round(display * 255)), "RGB")


def label_panel(img, label, font):
    canvas = Image.new("RGB", (img.width, img.height + 40), "#171717")
    canvas.paste(img, (0, 40))
    d = ImageDraw.Draw(canvas)
    d.text((12, 9), label, fill="white", font=font)
    return canvas


def sobel_mag(g):
    gx = np.abs(g[:, 2:] - g[:, :-2])  # (h, w-2)
    gy = np.abs(g[2:, :] - g[:-2, :])  # (h-2, w)
    m = np.zeros_like(g)
    m[1:-1, 1:-1] = gx[1:-1, :] + gy[:, 1:-1]
    return m


def checker_energy(c):
    # alternating-phase (checkerboard) component: kills smooth gradients,
    # keeps single-pixel dot alternation -> zipper/false-color dot proxy.
    pred = (np.roll(c, 1, 0) + np.roll(c, -1, 0) + np.roll(c, 1, 1) + np.roll(c, -1, 1)) * 0.25
    return np.abs(c - pred)


def psnr(ref, tst, peak):
    mse = float(np.mean((tst.astype(np.float64) - ref.astype(np.float64)) ** 2))
    return float(10 * np.log10(peak * peak / mse)) if mse > 0 else float("inf")


def disp(linear, exposure):
    return np.clip(linear / exposure, 0, 1) ** (1 / 2.2)


def main():
    try:
        font = ImageFont.truetype("/System/Library/Fonts/Supplemental/Arial.ttf", 22)
    except Exception:
        font = ImageFont.load_default()
    summary = {}
    for stem, meta in DNGS.items():
        wb = meta["wb"]
        ahd = (np.load(OUT / f"{stem}_ahd.npy", mmap_mode="r").astype(np.float32) / 65535.0)
        exposure = float(np.percentile(ahd[:, :, 1], 99.5))
        h, w = meta["size"]
        cx, cy = w // 2, h // 2
        cw, ch = CENTER
        x0, y0 = cx - cw // 2, cy - ch // 2
        ahd_c = ahd[y0:y0 + ch, x0:x0 + cw]
        edge = sobel_mag(ahd[:, :, 1])
        edge_m = edge > np.percentile(edge, 90)
        edge_c = edge_m[y0:y0 + ch, x0:x0 + cw]
        ahd_wb = ahd  # rawpy AHD already camera-WB applied

        # overview + detail strips across [AHD?, variants...]
        cols = list(VARIANTS) if NO_AHD else ["AHD"] + VARIANTS
        ov_panels, dt_panels = [], []
        stats = {"exposure": exposure, "variants": {}}
        # AHD stats baseline (self = 0 error, but report dot energy)
        rg = ahd_wb[:, :, 0] - ahd_wb[:, :, 1]
        bg = ahd_wb[:, :, 2] - ahd_wb[:, :, 1]
        stats["AHD"] = {"checker_RG": float(checker_energy(rg)[edge_m].mean()),
                        "checker_BG": float(checker_energy(bg)[edge_m].mean())}
        if not NO_AHD:
            ov_panels.append(label_panel(image_of(ahd_wb, exposure).resize((600, int(600 * h / w))), "AHD reference", font))
            dt_panels.append(label_panel(image_of(ahd_c, exposure), "AHD center 100%", font))
        for v in VARIANTS:
            lin = apply_wb(gpu_output(stem, v), wb)
            d = np.abs(lin - ahd_wb)
            lin_c = lin[y0:y0 + ch, x0:x0 + cw]
            dc = d[y0:y0 + ch, x0:x0 + cw]
            rg = lin[:, :, 0] - lin[:, :, 1]
            bg = lin[:, :, 2] - lin[:, :, 1]
            ch_rg = checker_energy(rg)
            ch_bg = checker_energy(bg)
            tjson = OUT / f"{stem}_{v}.json"
            timing = json.loads(tjson.read_text()) if tjson.exists() else {}
            stats["variants"][v] = {
                "mae_full": float(d.mean()), "p99_full": float(np.percentile(d, 99)),
                "max_full": float(d.max()),
                "mae_center": float(dc.mean()), "p99_center": float(np.percentile(dc, 99)),
                "mae_edge": float(d[edge_m].mean()),
                "psnr_linear_full": psnr(ahd_wb, lin, float(ahd_wb.max())),
                "psnr_linear_center": psnr(ahd_c, lin_c, float(ahd_c.max())),
                "psnr_display_center": psnr(disp(ahd_c, exposure), disp(lin_c, exposure), 1.0),
                "checker_RG_edge": float(ch_rg[edge_m].mean()),
                "checker_BG_edge": float(ch_bg[edge_m].mean()),
                "medianGpuMs": timing.get("medianGpuMs"), "p95GpuMs": timing.get("p95GpuMs"),
            }
            ov_panels.append(label_panel(
                image_of(lin, exposure).resize((600, int(600 * h / w))), f"{v}", font))
            dt_panels.append(label_panel(image_of(lin_c, exposure), f"{v} center 100%", font))
        suffix = "_cmp" if NO_AHD else ""
        W = 600 * len(cols)
        ov = Image.new("RGB", (W, ov_panels[0].height), "#111111")
        for i, p in enumerate(ov_panels):
            ov.paste(p, (i * 600, 0))
        ov.save(OUT / f"{stem}_overview{suffix}.png")
        dt = Image.new("RGB", (cw * len(cols), ch + 40), "#111111")
        for i, p in enumerate(dt_panels):
            dt.paste(p, (i * cw, 0))
        dt.save(OUT / f"{stem}_center{suffix}.png")
        (OUT / f"{stem}_stats.json").write_text(json.dumps(stats, indent=1))
        summary[stem] = stats
        print(f"== {stem} exposure={exposure:.4f}")
        for v, s in stats["variants"].items():
            print(f"  {v}: mae_c={s['mae_center']:.5f} p99_c={s['p99_center']:.5f} "
                  f"psnr_lin_c={s['psnr_linear_center']:.2f}dB psnr_disp_c={s['psnr_display_center']:.2f}dB "
                  f"mae_edge={s['mae_edge']:.5f} chkRG={s['checker_RG_edge']:.5f} "
                  f"chkBG={s['checker_BG_edge']:.5f} gpu_med={s['medianGpuMs']} p95={s['p95GpuMs']}")
        print(f"  AHD edge checker: RG={stats['AHD']['checker_RG']:.5f} BG={stats['AHD']['checker_BG']:.5f}")
    (OUT / "summary.json").write_text(json.dumps(summary, indent=1))


if __name__ == "__main__":
    main()
