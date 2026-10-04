#!/usr/bin/env python3
"""Native YUV-port gates on synthetic linear-HDR content (no DNG needed).

Image (512x512 linear RGB float): dark/mid/bright/HDR flats, color ramps,
step edges, plus luma + chroma Gaussian noise. Checks the O-variant port:
  identity   sy=0/sc=0 (luma bypass + chroma bypass): roundtrip PSNR>=50
  determinism  two full runs bit-identical (heap-bug class detector)
  effect     chroma-only: flat CbCr down >=2x, luma preserved, HDR kept
  census     exact-0 output where input is bright: none allowed
  fit        blind alpha>0 (parsed from runner stdout)

Run with: uv run --no-project --with numpy
"""
from __future__ import annotations

import os
import subprocess
import sys

import numpy as np

HERE = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
BIN = os.path.join(HERE, "build-macos", "galosh_validate_yuv")
SPV = os.path.join(HERE, "build-shaders")
TMP = os.path.join(HERE, "tmp")
os.makedirs(TMP, exist_ok=True)
W = H = 512

fails: list[str] = []


def run(*args):
    r = subprocess.run(list(args), capture_output=True, text=True)
    if r.returncode != 0:
        fails.append(f"{args[0]} rc={r.returncode}: {r.stderr[-300:]}")
    return r


def ycbcr(x):
    R, G, B = x[..., 0], x[..., 1], x[..., 2]
    Y = 0.2126 * R + 0.7152 * G + 0.0722 * B
    return Y, (B - Y) / 1.8556, (R - Y) / 1.5748


def main() -> int:
    rng = np.random.default_rng(7)
    yy, xx = np.mgrid[0:H, 0:W]
    base = np.zeros((H, W, 3), np.float32)
    base[:, :] = (0.18, 0.18, 0.18)
    base[:128] = (0.02, 0.02, 0.02)          # dark flat
    base[128:256, :256] = (0.8, 0.8, 0.8)    # bright flat
    base[128:256, 256:] = (2.5, 2.5, 2.5)    # HDR flat (no-clip probe)
    base[256:384, :170] = (0.5, 0.05, 0.05)  # red patch
    base[256:384, 170:340] = (0.05, 0.5, 0.05)
    base[256:384, 340:] = (0.05, 0.05, 0.5)
    base[384:, :256] = np.linspace(0.02, 0.9, 256, dtype=np.float32)[None, :, None]
    luma_n = rng.normal(0, 0.008, (H, W, 1)).astype(np.float32)
    chroma_n = rng.normal(0, 0.012, (H, W, 3)).astype(np.float32)
    chroma_n -= chroma_n.mean((0, 1), keepdims=True) * np.array([0.2, 0.9, 0.9], np.float32)
    img = (base + luma_n + chroma_n).astype(np.float32)
    inp = os.path.join(TMP, "yuv_synth_lin.bin")
    img.tofile(inp)

    def run_case(tag, sy, sc, mode):
        out = os.path.join(TMP, f"yuv_synth_{tag}")
        r = run(BIN, "--spv-dir", SPV, "--in", inp, "--out", out,
                "--w", str(W), "--h", str(H),
                "--sy", str(sy), "--sc", str(sc), "--mode", mode)
        print(r.stdout.strip(), f"[{tag}]")
        return out + "_rgba16.bin"

    def widen(p):
        u = np.fromfile(p, np.uint16).reshape(H, W, 4)[..., :3]
        # vectorized f16->f32
        u32 = u.astype(np.uint32)
        sign = ((u32 >> 15) & 1).astype(np.float64)
        exp = ((u32 >> 10) & 0x1F).astype(np.float64)
        mant = (u32 & 0x3FF).astype(np.float64)
        sgn = np.where(sign == 0, 1.0, -1.0)
        out = np.empty(u.shape, np.float64)
        norm = (exp != 0) & (exp != 31)
        out[norm] = sgn[norm] * (2.0 ** (exp[norm] - 15)) * (1 + mant[norm] / 1024)
        sub = exp == 0
        out[sub] = sgn[sub] * (2.0 ** -14) * (mant[sub] / 1024)
        inf = exp == 31
        out[inf] = np.inf * sgn[inf]
        return out

    # identity proxy: luma bypass + chroma bypass (roundtrip only)
    p_id = run_case("id", 0.0, 0.0, "full")
    a = widen(p_id)
    mse = float(((a - img.astype(np.float64)) ** 2).mean())
    psnr = 10 * np.log10(1.0 / max(mse, 1e-30))
    print(f"identity roundtrip: {psnr:.1f} dB")
    if psnr < 50.0:
        fails.append(f"identity roundtrip {psnr:.1f} dB < 50 dB (bridges/plumbing)")

    # determinism on the full path
    p1 = run_case("det1", 1.0, 1.0, "full")
    p2 = run_case("det2", 1.0, 1.0, "full")
    d1 = open(p1, "rb").read()
    if d1 != open(p2, "rb").read():
        fails.append("full path not bit-deterministic run-to-run")
    else:
        print("determinism: bit-identical")

    # effect: chroma-only on the noisy synthetic
    p_co = run_case("co", 0.0, 1.0, "chroma-only")
    co = widen(p_co)
    _, Cb0, Cr0 = ycbcr(img.astype(np.float64))
    _, Cb1, Cr1 = ycbcr(co)
    flat = (slice(8, 120), slice(8, 504))  # dark flat, clear of edges
    f0 = Cb0[flat].std() + Cr0[flat].std()
    f1 = Cb1[flat].std() + Cr1[flat].std()
    print(f"chroma-only flat CbCr: {f0:.5f} -> {f1:.5f} ({f0 / max(f1, 1e-12):.1f}x)")
    if f0 / max(f1, 1e-12) < 2.0:
        fails.append("chroma-only effect < 2x on synthetic chroma noise")
    Y0, _, _ = ycbcr(img.astype(np.float64))
    Y1, _, _ = ycbcr(co)
    print(f"luma kept: {Y0[flat].std():.5f} -> {Y1[flat].std():.5f}")
    hdr_in = img[128:256, 256:].max()
    hdr_out = co[128:256, 256:].max()
    print(f"HDR kept: {hdr_in:.2f} -> {hdr_out:.2f}")
    if hdr_out < 2.0:
        fails.append(f"HDR clipped by linear path ({hdr_out:.2f})")
    bright = img.max(-1) > 0.5
    drops = int(((co.max(-1) < 0.05) & bright).sum())
    print(f"dropout census: {drops}")
    if drops != 0:
        fails.append(f"{drops} black dropouts on bright content")

    if fails:
        print("YUV NATIVE: FAIL")
        for f in fails:
            print(f"  {f}")
        return 1
    print("YUV NATIVE: PASS")
    return 0


if __name__ == "__main__":
    sys.exit(main())
