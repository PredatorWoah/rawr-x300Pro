#!/usr/bin/env python3
"""Golden vectors for the quadfix CPU test (tests/validate.cpp).

Implements the SHADER algorithm bit-faithfully in float64 with clamp
('nearest'/'edge') borders:
  guide (true 9x9 median + fast row-col variant), energy box9,
  Gabor coarse bank (8x8 block-mean, coarse gaussian sigma 39.7887/8 R20,
  repeat upsample), mask smooth (gaussian sigma 6 R24), blend.

Inputs are 128x128 (%16==0, like production tiles):
  lattice : synthetic per-phase lattice + noise (notch must kill it)
  step    : vertical edge (ripple probe)
  sky     : real sky excerpt, center 128x128 of the frozen sky crop

Outputs per input x {true,fast}: out_*, guide_*, mask_*.f32 + manifest.json.

Usage: uv run --no-project --with numpy --with scipy tests/golden_gen.py   (run from quadfix/ dir)
"""
import json
from pathlib import Path

import numpy as np
from scipy.ndimage import gaussian_filter

ROOT = Path(__file__).resolve().parent
DATA = ROOT / "data"
DATA.mkdir(exist_ok=True)

N = 128
SIGMA_S = 1.0 / (2 * np.pi * 0.004)
COARSE_SIGMA = SIGMA_S / 8.0
E_FLAT, E_TEX = 1.5, 5.0
REPS = [(0, 1), (1, 0), (1, 1), (1, 3), (1, 2), (2, 1)]
NYQ = [(0, 2), (2, 0), (2, 2)]
DS = 8


def pad_edge(a, r):
    return np.pad(a, r, mode="edge")


def guide_true(a):
    out = np.empty_like(a)
    for y in range(2):
        for x in range(2):
            c = a[y::2, x::2]
            p = pad_edge(c, 4)
            w = np.lib.stride_tricks.sliding_window_view(p, (9, 9))
            out[y::2, x::2] = np.median(w, axis=(-2, -1))
    return out


def guide_fast(a):
    out = np.empty_like(a)
    for y in range(2):
        for x in range(2):
            c = a[y::2, x::2]
            pr = np.pad(c, ((0, 0), (4, 4)), mode="edge")
            wr = np.lib.stride_tricks.sliding_window_view(pr, (1, 9)).reshape(c.shape[0], c.shape[1], 9)
            rm = np.median(wr, axis=-1)
            pc = np.pad(rm, ((4, 4), (0, 0)), mode="edge")
            wc = np.lib.stride_tricks.sliding_window_view(pc, (9, 1)).reshape(c.shape[0], c.shape[1], 9)
            out[y::2, x::2] = np.median(wc, axis=-1)
    return out


def box9_mean(a):
    from scipy.ndimage import uniform_filter
    out = np.empty_like(a)
    for y in range(2):
        for x in range(2):
            out[y::2, x::2] = uniform_filter(a[y::2, x::2], size=9, mode="nearest")
    return out


def gabor_notch_clamp(resid):
    """Coarse Gabor notch, clamp borders (== shader demod+gauss+blend math)."""
    nchan = []
    cplanes = []  # per-phase coarse fields, 15ch x 4ph
    for y in range(2):
        for x in range(2):
            p = resid[y::2, x::2]
            m = p.shape[0]
            jj, ii = np.mgrid[:m, :m].astype(np.float64)
            ch = []
            for qy, qx in REPS:
                ph = 2 * np.pi * ((qy / 4) * jj + (qx / 4) * ii)
                ch.append((2.0, np.cos(ph), np.sin(ph)))
            for qy, qx in NYQ:
                ph = 2 * np.pi * ((qy / 4) * jj + (qx / 4) * ii)
                ch.append((1.0, np.cos(ph), None))
            coarse = []
            for f, c_, s_ in ch:
                # 8x8 block-mean downsample (exact blocks; N%16==0)
                zc = p * c_ if s_ is None else (p * c_, p * s_)
                if s_ is None:
                    b = zc.reshape(m // DS, DS, m // DS, DS).mean(axis=(1, 3))
                    coarse.append(gaussian_filter(b, COARSE_SIGMA, mode="nearest"))
                else:
                    b0 = zc[0].reshape(m // DS, DS, m // DS, DS).mean(axis=(1, 3))
                    b1 = zc[1].reshape(m // DS, DS, m // DS, DS).mean(axis=(1, 3))
                    coarse.append((gaussian_filter(b0, COARSE_SIGMA, mode="nearest"),
                                   gaussian_filter(b1, COARSE_SIGMA, mode="nearest")))
            cplanes.append((ch, coarse))
    # remodulate + subtract per phase
    out = np.empty_like(resid)
    for (y, x), (ch, coarse) in zip([(0, 0), (0, 1), (1, 0), (1, 1)], cplanes):
        p = resid[y::2, x::2]
        m = p.shape[0]
        jj, ii = np.mgrid[:m, :m].astype(np.float64)
        acc = np.zeros_like(p)
        for (f, c_, s_), co in zip(ch, coarse):
            up = np.repeat(np.repeat(co if s_ is None else co[0], DS, 0), DS, 1) if s_ is None else None
            if s_ is None:
                acc += f * up * c_
            else:
                up0 = np.repeat(np.repeat(co[0], DS, 0), DS, 1)
                up1 = np.repeat(np.repeat(co[1], DS, 0), DS, 1)
                acc += f * (up0 * c_ + up1 * s_)
        out[y::2, x::2] = p - acc
    return out


def mask_smooth(e):
    t = np.clip((e - E_FLAT) / (E_TEX - E_FLAT), 0, 1)
    out = np.empty_like(t)
    for y in range(2):
        for x in range(2):
            out[y::2, x::2] = np.clip(gaussian_filter(t[y::2, x::2], 6.0, mode="nearest"), 0, 1)
    return out


def run_case(name, a, guide_fn):
    g = guide_fn(a)
    resid = a - g
    e = np.sqrt(box9_mean(resid * resid))
    t = mask_smooth(e)
    n = g + gabor_notch_clamp(resid)
    out = t * a + (1 - t) * n
    return {"guide": g, "mask": t, "out": out}


def main():
    rng = np.random.default_rng(7)
    # (a) synthetic per-phase lattice + noise
    lat = np.zeros((N, N))
    for py in range(2):
        for px in range(2):
            A, ph0 = 1.5 + rng.standard_normal() * 0.3, rng.uniform(0, 2 * np.pi)
            jj = np.arange(N // 2)[None, :].repeat(N // 2, 0)
            lat[py::2, px::2] = A * np.cos(2 * np.pi * jj / 4 + ph0)
    in_lat = (40.0 + lat + rng.standard_normal((N, N)) * 1.1).astype(np.float64)
    # (b) step edge
    in_step = np.zeros((N, N))
    in_step[:, N // 2:] = 50.0
    in_step += rng.standard_normal((N, N)) * 0.5
    # (c) real sky excerpt
    sky = np.fromfile(
        ROOT / "../../../../.cache/renderer-grid-20260921/host-comparison/adaptive/sky/raw.f32",
        np.float32).reshape(1536, 1536)
    c = (1536 - N) // 2
    in_sky = np.ascontiguousarray(sky[c:c + N, c:c + N]).astype(np.float64)

    manifest = {"n": N, "cases": {}}
    for in_name, a in [("lattice", in_lat), ("step", in_step), ("sky", in_sky)]:
        a.astype(np.float32).tofile(DATA / f"in_{in_name}.f32")

        def phase_std8(f):
            # 8x8 full-res phase means: the plane period-4 lattice has full-res
            # period 8, so 4x4 groups alias it to zero; 8x8 groups see it.
            yy_, xx_ = np.indices(f.shape)
            M_ = np.stack([np.ones(f.size), yy_.ravel(), xx_.ravel()], 1)
            r_ = (f.ravel() - M_ @ np.linalg.lstsq(M_, f.ravel(), rcond=None)[0]).reshape(f.shape)
            m_ = np.array([[r_[i::8, j::8].mean() for j in range(8)] for i in range(8)])
            return float(m_.std())

        manifest["cases"][in_name] = {"raw_phase_std8": phase_std8(a.astype(np.float64))}
        for tag, fn in [("true", guide_true), ("fast", guide_fast)]:
            r_ = run_case(in_name, a, fn)
            for field in ["guide", "mask", "out"]:
                r_[field].astype(np.float32).tofile(DATA / f"{field}_{in_name}_{tag}.f32")
            # lattice kill check on lattice input
            o = r_["out"]
            ps = phase_std8(o)
            manifest["cases"][in_name][f"out_phase_std8_{tag}"] = ps
            print(f"{in_name:8s} {tag:4s} raw8={manifest['cases'][in_name]['raw_phase_std8']:.4f} out8={ps:.4f}")
    (DATA / "manifest.json").write_text(json.dumps(manifest, indent=2))
    print("wrote", DATA)


if __name__ == "__main__":
    main()
