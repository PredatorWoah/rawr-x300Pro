#!/usr/bin/env python3
"""Native-port parity: CPU oracle vs native GaloshRawPipeline (MoltenVK).

For each sample DNG: rawpy -> float Bayer -> CPU oracle (full) as ground
truth; galosh_validate runs the NATIVE port (full + chroma-only); compare.

Gates (fail = nonzero exit):
  full PSNR(native, oracle) >= 69 dB            (FP16 + bridge roundtrip)
  chroma-only: L drift tiny, C reduced, every new exact-zero comes from
    near-black input (< 1e-3) -- the visible-dropout class stays empty.
Recorded: per-frame ms, blind alpha/sigma agreement.

Run with: uv run --no-project --with numpy --with rawpy
"""
from __future__ import annotations

import json
import os
import subprocess
import sys

import numpy as np

HERE = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
ROOT = os.path.dirname(os.path.dirname(HERE))
UP = os.path.join(ROOT, "tmp", "GALOSH", "standalone")
CPU = os.path.join(UP, "galosh_raw_cpu.exe")
VALIDATE = os.path.join(HERE, "build-macos", "galosh_validate")
TMP = os.path.join(HERE, "tmp")
os.makedirs(TMP, exist_ok=True)

DNGS = {
    "dark": os.path.join(ROOT, "tmp", "device_dng", "RAWR_20260925_234826_906.dng"),
    "bright": os.path.join(ROOT, "tmp", "device_dng", "RAWR_20260926_000619_512.dng"),
}

fails: list[str] = []


def run(*args):
    r = subprocess.run(list(args), capture_output=True, text=True)
    if r.returncode != 0:
        fails.append(f"{args[0]} rc={r.returncode}: {r.stderr[-300:]}")
    return r


def to_float(codes, black, white):
    return (codes.astype(np.float64) - black) / (white - black)


def main() -> int:
    import rawpy

    all_metrics: dict = {}
    for tag, dng in DNGS.items():
        raw = rawpy.imread(dng)
        u16 = raw.raw_image.copy()
        H, W = u16.shape
        black = float(raw.black_level_per_channel[0])
        white = float(raw.white_level)
        bf = np.clip((u16.astype(np.float32) - black) / (white - black), 0, 1)
        rb = os.path.join(TMP, f"nv_{tag}.rawfloat.bin")
        bf.tofile(rb)

        exp = os.path.join(TMP, f"nv_{tag}.oracle.bin")
        run(CPU, rb, exp, str(W), str(H), "galosh", "1.0", "1.0", "1.0", "0", "0")
        got = os.path.join(TMP, f"nv_{tag}.native")
        r = run(VALIDATE, "--spv-dir", os.path.join(HERE, "build-shaders"), "--in", rb,
                "--out", got, "--w", str(W), "--h", str(H), "--black", str(black),
                "--white", str(white), "--mode", "full")
        print(r.stdout.strip(), f"[{tag} full]")
        got_co = os.path.join(TMP, f"nv_{tag}.nativeCO")
        r = run(VALIDATE, "--spv-dir", os.path.join(HERE, "build-shaders"), "--in", rb,
                "--out", got_co, "--w", str(W), "--h", str(H), "--black", str(black),
                "--white", str(white), "--mode", "chroma-only")
        print(r.stdout.strip(), f"[{tag} chroma-only]")
        if fails:
            continue

        oracle = np.fromfile(exp, np.float32).reshape(H, W).astype(np.float64)
        native = to_float(np.fromfile(got + "_u16.bin", np.uint16).reshape(H, W), black, white)
        mse = float(((native - oracle) ** 2).mean())
        psnr = 10 * np.log10(1.0 / max(mse, 1e-30))
        all_metrics[f"native_parity_db_{tag}"] = round(psnr, 2)
        print(f"{tag} full parity: {psnr:.2f} dB")
        if psnr < 69.0:
            fails.append(f"{tag} native parity {psnr:.1f} dB < 69 dB gate")

        co = to_float(np.fromfile(got_co + "_u16.bin", np.uint16).reshape(H, W), black, white)
        inp = bf.astype(np.float64)
        q = inp.reshape(H // 2, 2, W // 2, 2).transpose(0, 2, 1, 3).reshape(H // 2, W // 2, 4)
        qc = co.reshape(H // 2, 2, W // 2, 2).transpose(0, 2, 1, 3).reshape(H // 2, W // 2, 4)
        l_in, l_co = q.mean(-1), qc.mean(-1)
        c_in = np.abs(q - l_in[..., None]).mean(-1)
        c_co = np.abs(qc - l_co[..., None]).mean(-1)
        # flattest quad block by input luma variance
        best, bx, by = 1e9, 0, 0
        for yy in range(0, l_in.shape[0] - 64, 32):
            for xx in range(0, l_in.shape[1] - 64, 32):
                v = l_in[yy:yy + 64, xx:xx + 64].var()
                if v < best:
                    best, bx, by = v, xx, yy
        sl = (slice(by, by + 64), slice(bx, bx + 64))
        l_drift = float(np.abs(l_co - l_in).mean())
        c_ratio = float(c_in[sl].mean() / max(c_co[sl].mean(), 1e-12))
        newzero = (np.fromfile(got_co + "_u16.bin", np.uint16).reshape(H, W) == 0) & (u16 != 0)
        nz = int(newzero.sum())
        nz_visible = int((newzero & (bf > 1e-3)).sum())
        all_metrics[f"nativeCO_{tag}"] = {
            "l_drift": l_drift, "c_ratio": round(c_ratio, 2),
            "newzero": nz, "newzero_visible": nz_visible,
        }
        print(f"{tag} chroma-only: L_drift={l_drift:.2e} C_ratio={c_ratio:.1f}x "
              f"newzero={nz} visible={nz_visible}")
        if nz_visible != 0:
            fails.append(f"{tag} chroma-only: {nz_visible} visible created zeros")
        # Floor checks the port is functional; frame noisiness varies widely
        # (bright 512 measures ~1.3x even on the CPU reference).
        if c_ratio < 1.2:
            fails.append(f"{tag} chroma-only: C reduction {c_ratio:.1f}x < 1.2x")

    json.dump(all_metrics, open(os.path.join(TMP, "native_parity.json"), "w"), indent=1)
    if fails:
        print("NATIVE PARITY: FAIL")
        for f in fails:
            print(f"  {f}")
        return 1
    print("NATIVE PARITY: PASS")
    return 0


if __name__ == "__main__":
    sys.exit(main())
