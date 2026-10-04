#!/usr/bin/env python3
"""CPU reference for still_defringe.comp (axial purple-fringe desaturation).

Mirrors the GLSL exactly (same Y weights, smoothstep, 9x9 box) so tuning
numbers transfer 1:1 to the shader. Used for threshold tuning + oracle tests.
"""
import numpy as np


def _smoothstep(a, b, x):
    t = np.clip((x - a) / (b - a), 0.0, 1.0)
    return t * t * (3.0 - 2.0 * t)


def _box(e, r=4):
    p = np.pad(e, r, mode="edge").astype(np.float64)
    H, W = e.shape
    k = 2 * r + 1
    ii = np.zeros((H + k, W + k))
    ii[1:, 1:] = np.cumsum(np.cumsum(p, 0), 1)
    return (ii[k:, k:] - ii[:-k, k:] - ii[k:, :-k] + ii[:-k, :-k]) / k / k


def defringe(rgb, strength=1.0, edge_threshold=0.02, luma_floor=0.08):
    """rgb: float32 HxWx3 linear (any range; non-finite passed through).

    Returns corrected rgb. strength=0 is bit-identical no-op.
    """
    rgb = np.asarray(rgb, dtype=np.float32)
    finite = np.isfinite(rgb).all(axis=-1)
    out = rgb.copy()
    if strength <= 0.0:
        return out
    Y = (0.299 * rgb[..., 0] + 0.587 * rgb[..., 1] + 0.114 * rgb[..., 2]).astype(np.float64)
    e = np.minimum(rgb[..., 0], rgb[..., 2]).astype(np.float64) - rgb[..., 1].astype(np.float64)
    E = _box(e)
    narrow = np.clip((e - E) / np.maximum(e, 1e-4), 0.0, 1.0)
    w = (strength * narrow
         * _smoothstep(edge_threshold, edge_threshold * 2.0, e)
         * _smoothstep(luma_floor, luma_floor * 2.0, Y)).astype(np.float32)
    w = w[..., None] * finite[..., None]
    Y3 = Y[..., None].astype(np.float32)
    return ((1.0 - w) * rgb + w * np.broadcast_to(Y3, rgb.shape)).astype(np.float32)


def fringe_count(gamma_rgb):
    """Purple-pixel detector (display-referred, for metrics only)."""
    x = (np.asarray(gamma_rgb) * 255).astype(int)
    r, g, b = x[..., 0], x[..., 1], x[..., 2]
    lum = 0.299 * r + 0.587 * g + 0.114 * b
    return int((((r > 100) & (b > 100) & (g < np.minimum(r, b) - 20) & (lum > 80))).sum())
