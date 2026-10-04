#!/usr/bin/env python3
"""P0 parity gates: upstream CPU exes vs Vulkan exes on device DNGs.

Drives the reference builds in <repo>/tmp/GALOSH (see ../README.md
"Offline lab"); the P1 native port must reproduce these gates through
GaloshRawPipeline / GaloshYuvPipeline.

Gates (fail = nonzero exit):
  RAW CPU<->VK PSNR            >= 69 dB   (lab measured ~100 dB)
  RAW VK luma=0                byte-identical to input (CPU semantics)
  YUV chroma-only dropouts     == 0 on CPU-patched and VK-patched exes
Recorded (warn on >25% drift vs tests/golden_metrics.json):
  YUV full CPU<->VK parity, per-engine flat-noise effect ratios.

Needs the lab exes; without them reports INCOMPLETE (exit 2), mirroring
upstream verify_table_numbers.py semantics.

Run with: uv run --no-project --with numpy --with rawpy --with pillow
"""
from __future__ import annotations

import json
import os
import subprocess
import sys

import numpy as np
from PIL import Image

HERE = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
ROOT = os.path.dirname(os.path.dirname(HERE))
UP = os.path.join(ROOT, "tmp", "GALOSH", "standalone")
CPU_RAW = os.path.join(UP, "galosh_raw_cpu.exe")
CPU_YUV = os.path.join(UP, "galosh_yuv_cpu.exe")
VK_RAW = os.path.join(UP, "vk", "galosh_vk_macos")
VK_YUV = os.path.join(UP, "vk", "galosh_yuv_vk_macos")
DNGS = {
    "dark": os.path.join(ROOT, "tmp", "device_dng", "RAWR_20260925_234826_906.dng"),
    "bright": os.path.join(ROOT, "tmp", "device_dng", "RAWR_20260926_000619_512.dng"),
}
TMP = os.path.join(HERE, "tmp")
os.makedirs(TMP, exist_ok=True)

fails: list[str] = []
warns: list[str] = []


def run(exe, *args, env_extra=None):
    env = dict(os.environ)
    env.update(env_extra or {})
    r = subprocess.run([exe, *args], capture_output=True, text=True, env=env)
    if r.returncode != 0:
        fails.append(f"{os.path.basename(exe)} rc={r.returncode}: {r.stderr[-300:]}")
    return r


def psnr(a, b):
    mse = float(((a.astype(np.float64) - b.astype(np.float64)) ** 2).mean())
    return 10 * np.log10(1.0 / max(mse, 1e-30)), mse


def main() -> int:
    missing = [p for p in [CPU_RAW, CPU_YUV, VK_RAW, VK_YUV] if not os.path.exists(p)]
    if missing or not all(os.path.exists(d) for d in DNGS.values()):
        print("RESULT: INCOMPLETE - reference exes/DNGs missing:")
        for p in missing:
            print(f"  absent: {p}")
        return 2

    import rawpy  # deferred: only needed when exes exist

    metrics: dict = {}
    for tag, dng in DNGS.items():
        raw = rawpy.imread(dng)
        u16 = raw.raw_image.copy().astype(np.float32)
        H, W = u16.shape
        black = float(raw.black_level_per_channel[0])
        white = float(raw.white_level)
        bayer = np.clip((u16 - black) / (white - black), 0, 1)
        rb = os.path.join(TMP, f"{tag}.raw.bin")
        bayer.tofile(rb)

        # RAW: CPU full + VK full + VK luma=0 bypass.
        ro_c = os.path.join(TMP, f"{tag}.raw_cpu.bin")
        ro_v = os.path.join(TMP, f"{tag}.raw_vk.bin")
        ro_b = os.path.join(TMP, f"{tag}.raw_vk_l0.bin")
        run(CPU_RAW, rb, ro_c, str(W), str(H), "galosh", "1.0", "1.0", "1.0", "0", "0")
        run(VK_RAW, rb, ro_v, str(W), str(H), "galosh", "1.0", "1.0", "1.0", "0", "0")
        run(VK_RAW, rb, ro_b, str(W), str(H), "galosh", "1.0", "0.0", "1.0", "0", "0")
        if not fails:
            oc = np.fromfile(ro_c, np.float32)
            ov = np.fromfile(ro_v, np.float32)
            p, _ = psnr(oc, ov)
            metrics[f"raw_parity_db_{tag}"] = round(p, 2)
            if p < 69.0:
                fails.append(f"RAW parity {tag}: {p:.1f} dB < 69 dB gate")
            ob = np.fromfile(ro_b, np.float32)
            if not np.array_equal(bayer.ravel(), ob):
                fails.append(f"RAW VK luma=0 {tag}: not byte-identical (CPU semantics)")

        # YUV: sRGB render, CPU chroma-only + VK chroma-only dropout census.
        rgb = raw.postprocess(use_camera_wb=True, output_color=rawpy.ColorSpace.sRGB,
                              gamma=(2.222, 4.5), no_auto_bright=False, output_bps=8)
        h, w, _ = rgb.shape
        sb = os.path.join(TMP, f"{tag}.srgb.bin")
        (rgb.astype(np.float32) * (1 / 255.0)).tofile(sb)
        yo_c = os.path.join(TMP, f"{tag}.yuvCO_cpu.bin")
        yo_v = os.path.join(TMP, f"{tag}.yuvCO_vk.bin")
        run(CPU_YUV, sb, yo_c, str(w), str(h), "0.0", "1.0")
        run(VK_YUV, sb, yo_v, str(w), str(h), "0.0", "1.0")
        if not fails:
            ib = np.fromfile(sb, np.float32).reshape(h, w, 3)
            dark = ib.max(-1) > 0.5
            for name, path in (("cpu", yo_c), ("vk", yo_v)):
                ia = np.fromfile(path, np.float32).reshape(h, w, 3)
                drops = int(((ia.max(-1) < 0.05) & dark).sum())
                metrics[f"yuvCO_drops_{tag}_{name}"] = drops
                if drops != 0:
                    fails.append(f"YUV chroma-only {tag}/{name}: {drops} black dropouts")

        # YUV full (1,1): recorded baseline only (estimator divergence between
        # the on-device histogram and CPU exact-MAD estimators is a known
        # ~45 dB gap; P1 closes it with a shared host-side estimate).
        yf_c = os.path.join(TMP, f"{tag}.yuvFull_cpu.bin")
        yf_v = os.path.join(TMP, f"{tag}.yuvFull_vk.bin")
        run(CPU_YUV, sb, yf_c, str(w), str(h), "1.0", "1.0")
        run(VK_YUV, sb, yf_v, str(w), str(h), "1.0", "1.0")
        if not fails:
            fc = np.fromfile(yf_c, np.float32)
            fv = np.fromfile(yf_v, np.float32)
            p, _ = psnr(fc, fv)
            metrics[f"yuvFull_parity_db_{tag}"] = round(p, 2)

    golden_path = os.path.join(HERE, "tests", "golden_metrics.json")
    if os.path.exists(golden_path):
        golden = json.load(open(golden_path))
        for k, v in metrics.items():
            if k in golden.get("reference", {}):
                g = golden["reference"][k]
                denom = max(abs(g), 1e-9)
                if abs(v - g) / denom > 0.25:
                    warns.append(f"{k}: {v} drifts >25% from golden {g}")

    json.dump(metrics, open(os.path.join(TMP, "parity_metrics.json"), "w"), indent=1)
    for w in warns:
        print(f"WARN: {w}")
    if fails:
        print("PARITY: FAIL")
        for f in fails:
            print(f"  {f}")
        return 1
    print("PARITY: PASS")
    for k, v in sorted(metrics.items()):
        print(f"  {k}={v}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
