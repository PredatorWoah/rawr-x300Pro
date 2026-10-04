#!/usr/bin/env python3
"""Float64 CPU golden for native/gainmap/shaders/gainmap.comp.

Mirrors the UltraHDR spec encode exactly so MoltenVK parity has a stable oracle:
  gain = (luma(hdrLin) + epsH) / (luma(sdrLin) + epsS)
  norm = clamp((log2(gain) - minLog2) / (maxLog2 - minLog2), 0, 1)
  stored = round(pow(norm, gamma) * 255) / 255   (gray: R=G=B)
Single-channel luminance gain: luma is Rec.709 on linear sRGB.
SDR inputs are sRGB-encoded; HDR inputs are white-balanced camera RGB +
row-major camera-to-linear-sRGB matrix (identity default).
"""
import numpy as np

MIN_LOG2 = 0.0
MAX_LOG2 = 4.7090998
GAMMA = 1.0
OFFSET_SDR = 0.015625
OFFSET_HDR = 0.015625
HDR_EXPOSURE = 1.0
CLIP_BOOST = 8.0
# Shadow fade gates (SDR linear luma): below LO the boost is identity.
FADE_LO = 0.03
FADE_HI = 0.50


def srgb_eotf(c):
    c = np.asarray(c, dtype=np.float64)
    lo = c / 12.92
    hi = np.power(np.maximum((c + 0.055) / 1.055, 0.0), 2.4)
    return np.where(c <= 0.04045, lo, hi)


def luma709(c):
    c = np.asarray(c, dtype=np.float64)
    return c[..., 0] * 0.2126 + c[..., 1] * 0.7152 + c[..., 2] * 0.0722


def smoothstep(e0, e1, x):
    t = np.clip((np.asarray(x, dtype=np.float64) - e0) / (e1 - e0), 0.0, 1.0)
    return t * t * (3.0 - 2.0 * t)


def sanitize(c):
    c = np.asarray(c, dtype=np.float64)
    bad = ~np.isfinite(c).all(axis=-1, keepdims=True)
    return np.where(bad, 0.0, c)


def bilinear(sample_fn, w, h, uv):
    st = uv[..., :2] * np.array([w, h]) - 0.5
    base = np.floor(st)
    f = np.clip(st - base, 0.0, 1.0)
    x0 = np.clip(base[..., 0].astype(int), 0, w - 1)
    y0 = np.clip(base[..., 1].astype(int), 0, h - 1)
    x1 = np.clip(x0 + 1, 0, w - 1)
    y1 = np.clip(y0 + 1, 0, h - 1)
    c00 = sample_fn(y0, x0)[..., :3]
    c10 = sample_fn(y0, x1)[..., :3]
    c01 = sample_fn(y1, x0)[..., :3]
    c11 = sample_fn(y1, x1)[..., :3]
    top = c00 * (1 - f[..., 0:1]) + c10 * f[..., 0:1]
    bot = c01 * (1 - f[..., 0:1]) + c11 * f[..., 0:1]
    return top * (1 - f[..., 1:2]) + bot * f[..., 1:2]


SAT_PROTECT = 0.6
BLUR_SIGMA = 3.0


def encode_map(hdr_work, sdr_enc, map_w, map_h, hdr_to_linear_srgb=None,
               min_log2=MIN_LOG2, max_log2=MAX_LOG2, gamma=GAMMA,
               offset_sdr=OFFSET_SDR, offset_hdr=OFFSET_HDR, hdr_exposure=HDR_EXPOSURE,
               clip=None, clip_boost=CLIP_BOOST, sat_protect=SAT_PROTECT,
               rgb_map=False, blur_sigma=BLUR_SIGMA,
               glow=None, glow_strength=0.5, glow_max=2.0):
    hdr_work = np.asarray(hdr_work, dtype=np.float64)
    sdr_enc = np.asarray(sdr_enc, dtype=np.float64)
    hh, hw, _ = hdr_work.shape
    sh, sw, _ = sdr_enc.shape
    if hdr_to_linear_srgb is None:
        m = np.eye(3, dtype=np.float64)
    else:
        m = np.asarray(hdr_to_linear_srgb, dtype=np.float64).reshape(3, 3)
    ys, xs = np.mgrid[0:map_h, 0:map_w]
    uv = np.stack([(xs + 0.5) / map_w, (ys + 0.5) / map_h], axis=-1)
    hdr_s = sanitize(bilinear(lambda yy, xx: hdr_work[yy, xx], hw, hh, uv))
    sdr_s = sanitize(bilinear(lambda yy, xx: sdr_enc[yy, xx], sw, sh, uv))
    # Local-detail (flatness) probe, like the shader: per-channel spatial
    # spread over each texel's own 2x2 base block (exact half res), maxed
    # across channels. Truly pinned blocks (zero spread) take the boost;
    # recovered gradients and clip-edge boundary blocks keep the ratio.
    # Spread is per-channel, never across channels (WB gains spread channels
    # apart everywhere). Falls back to flat (1.0) when the geometry is not
    # exactly half res. Relative spread is exposure-scale-invariant.
    flat = np.ones((map_h, map_w), dtype=np.float64)
    if hw == 2 * map_w and hh == 2 * map_h:
        a3 = np.where(np.isfinite(hdr_work[..., :3]), hdr_work[..., :3], 0.0)
        b = a3.reshape(map_h, 2, map_w, 2, 3)
        fmn, fmx = b.min(axis=(1, 3)), b.max(axis=(1, 3))
        rel = ((fmx - fmn) / np.maximum(fmx, 1e-3)).max(axis=-1)
        t = np.clip((rel - 0.01) / (0.08 - 0.01), 0.0, 1.0)
        flat = 1.0 - t * t * (3.0 - 2.0 * t)
    # Scene-exposure match: SDR base contains the capture gain, HDR tap does not.
    hdr_s = hdr_s * max(hdr_exposure, 1e-6)
    # Film glow factor (agnostic post/pre quotient, ~1.0 = no glow): applied
    # pre-CST like the shader. None (or strength 0) keeps the pure scene tap.
    if glow is not None and glow_strength > 0.0:
        glow_a = np.asarray(glow, dtype=np.float64)
        gh, gw, _ = glow_a.shape
        glow_s = sanitize(bilinear(lambda yy, xx: glow_a[yy, xx], gw, gh, uv))
        glow_s = np.maximum(glow_s[..., :3], 0.0)
        glow_t = np.minimum(np.maximum(glow_s, 1.0), max(glow_max, 1.0))
        hdr_s = hdr_s * (1.0 + (glow_t - 1.0) * min(max(glow_strength, 0.0), 1.0))
    hdr_lin = np.maximum(hdr_s @ m.T, 0.0)
    sdr_lin = np.maximum(srgb_eotf(np.clip(sdr_s, 0.0, 1.0)), 0.0)
    # Chroma protection, like the shader: attenuate where the SDR base is
    # bright AND saturated so the multiplicative gain cannot push colored
    # lights into panel clip. Neutral highlights (sat ~0) keep full pop.
    smax_c = sdr_lin[..., :3].max(axis=-1)
    smin_c = sdr_lin[..., :3].min(axis=-1)
    sat_c = (smax_c - smin_c) / np.maximum(smax_c, 1e-3)
    tg = np.clip((smax_c - 0.5) / (0.9 - 0.5), 0.0, 1.0)
    sat_w = sat_protect * (tg * tg * (3.0 - 2.0 * tg)) * np.clip(sat_c, 0.0, 1.0)
    gain = (luma709(hdr_lin) + max(offset_hdr, 1e-6)) / (luma709(sdr_lin) + max(offset_sdr, 1e-6))
    log_gain = np.log2(np.maximum(gain, 1e-9))
    # Per-channel RGB mode (multiChannel): same math per channel (luma fade
    # and scalar satW shared), so hue survives a white SDR base. Selected at
    # the end like the shader's arithmetic select.
    gain_c = (hdr_lin + max(offset_hdr, 1e-6)) / (sdr_lin + max(offset_sdr, 1e-6))
    log_c = np.log2(np.maximum(gain_c, 1e-9))
    # Shadow fade: deep-shadow quotients encode curve toe, not headroom.
    # smoothstep matches the GLSL gate exactly.
    t = np.clip((luma709(sdr_lin) - FADE_LO) / (FADE_HI - FADE_LO), 0.0, 1.0)
    fade_t = t * t * (3.0 - 2.0 * t)
    log_gain = log_gain * fade_t
    log_c = log_c * fade_t[..., None]
    log_gain = log_gain * (1.0 - sat_w)
    log_c = log_c * (1.0 - sat_w)[..., None]
    # Clipped-core hue fallback, like the shader: recovery's full-core path
    # emits neutral white, so a white HDR tap + colored SDR base must use the
    # luma gain (SDR hue preserved) instead of per-channel (white rebuild).
    # Tap-only; white diffuse is neutral in both taps so the reroute is harmless.
    hdr_max_c = hdr_lin[..., :3].max(axis=-1)
    hdr_min_c = hdr_lin[..., :3].min(axis=-1)
    hdr_sat = (hdr_max_c - hdr_min_c) / np.maximum(hdr_max_c, 1e-3)
    hdr_white = (1.0 - smoothstep(0.05, 0.15, hdr_sat)) * smoothstep(0.5, 0.9, luma709(hdr_lin))
    log_c = log_c * (1.0 - hdr_white)[..., None] + log_gain[..., None] * hdr_white[..., None]
    # Specular boost for sensor-clipped texels (no detail there; dazzle).
    # Cover = feathered average of bitCount(mask & 0xF)/4 over the R,G1,G2,B
    # clip bits (fully blown -> full boost, partial clip keeps mostly the
    # measured ratio so surviving chroma is not nuked), like the shader,
    # maxed with the center texel. Coverage comes STRICTLY from the mask
    # (no tap-based detection: it false-fired on bright diffuse), AND from
    # local flatness: masked texels with recovered gradients keep the ratio.
    # Without a mask the boost is off and brights stay on the ratio path.
    cover = np.zeros((map_h, map_w), dtype=np.float64)
    if clip is not None and clip_boost > 0.0:
        m = np.asarray(clip)
        pop = np.vectorize(lambda v: bin(int(v) & 0xF).count("1"))(m).astype(np.float64) / 4.0
        if pop.ndim == 2:
            # 7x7 feathered average, maxed with the center texel (shader).
            mp = np.pad(pop, 3, mode="edge")
            s7 = np.zeros_like(pop)
            for dy in range(7):
                for dx in range(7):
                    s7 += mp[dy : dy + pop.shape[0], dx : dx + pop.shape[1]]
            cover = np.maximum(s7 / 49.0, pop)
        else:
            cover = pop
    if clip_boost > 0.0:
        # Gate on max channel, not luminance: saturated primaries hit 1.0 in
        # one channel at luma 0.07-0.21; a luma gate would exclude them ever.
        smax = sdr_lin[..., :3].max(axis=-1)
        t = np.clip((smax - 0.85) / (0.98 - 0.85), 0.0, 1.0)
        cover = cover * (t * t * (3.0 - 2.0 * t))
        # Detail gate: recovered gradients keep the ratio; only flat-pinned
        # masked cores render at clipBoost. The chroma term keeps the boost
        # from white-washing saturated pins.
        w = cover * flat * (1.0 - sat_w)
        log_gain = log_gain * (1.0 - w) + np.log2(max(clip_boost, 1e-9)) * w
        log_c = log_c * (1.0 - w)[..., None] + np.log2(max(clip_boost, 1e-9)) * w[..., None]
    norm = np.clip((log_gain - min_log2) / max(max_log2 - min_log2, 1e-6), 0.0, 1.0)
    norm_c = np.clip((log_c - min_log2) / max(max_log2 - min_log2, 1e-6), 0.0, 1.0)
    if rgb_map:
        recovery = np.power(norm_c, gamma)
        out = np.clip(np.floor(recovery * 255.0 + 0.5) / 255.0, 0.0, 1.0)
        return blur_separable_clamped(out, blur_sigma)
    recovery = np.power(norm, gamma)
    gray = np.clip(np.floor(recovery * 255.0 + 0.5) / 255.0, 0.0, 1.0)
    return blur_separable_clamped(np.stack([gray, gray, gray], axis=-1), blur_sigma)


def decode_gain(stored):
    # Inverse of the file curve: what an ISO/XMP decoder reconstructs.
    return 2.0 ** (MIN_LOG2 + (MAX_LOG2 - MIN_LOG2) * np.asarray(stored, dtype=np.float64))


def blur_separable_clamped(img, sigma):
    img = np.asarray(img, dtype=np.float64)
    if not sigma > 0.0:
        return img.copy()
    radius = min(int(np.ceil(3.0 * sigma)), 8)
    xs = np.arange(-radius, radius + 1, dtype=np.float64)
    k = np.exp(-0.5 * (xs / max(sigma, 1e-3)) ** 2)
    k = k / k.sum()
    out = img
    for axis in (1, 0):
        padded = np.pad(out, [(radius, radius) if a == axis else (0, 0) for a in range(out.ndim)],
                        mode="edge")
        res = np.zeros_like(out)
        for n, w in enumerate(k):
            sl = [slice(None)] * out.ndim
            sl[axis] = slice(n, n + out.shape[axis])
            res += padded[tuple(sl)] * w
        out = res
    return out
    # Inverse of the file curve: what an ISO/XMP decoder reconstructs.
    return 2.0 ** (MIN_LOG2 + (MAX_LOG2 - MIN_LOG2) * np.asarray(stored, dtype=np.float64))


def self_test():
    rng = np.random.default_rng(7)
    hdr = np.clip(rng.normal(0.6, 0.8, (8, 8, 4)), 0.0, 32.0)
    sdr = rng.uniform(0.0, 1.0, (8, 8, 4))
    out = encode_map(hdr, sdr, 4, 4)
    assert out.shape == (4, 4, 3)
    assert np.all(out >= 0.0) and np.all(out <= 1.0)
    # Single-channel: R == G == B everywhere.
    assert np.all(out[..., 0] == out[..., 1]) and np.all(out[..., 0] == out[..., 2])
    # Black HDR + mid SDR sits at the floor.
    lo = encode_map(np.zeros((4, 4, 4)), np.full((4, 4, 4), 0.5), 2, 2)
    assert np.all(lo < 0.2)
    # Bright HDR + bright SDR decodes back to the true headroom (~7.9x).
    hi = encode_map(np.full((4, 4, 4), 8.0), np.full((4, 4, 4), 1.0), 2, 2)
    assert np.all(np.abs(decode_gain(hi) - 7.9) < 1.0), decode_gain(hi).ravel()[0]
    # Non-finite HDR sanitizes instead of poisoning the map.
    nan_hdr = np.full((4, 4, 4), np.nan)
    assert np.all(np.isfinite(encode_map(nan_hdr, np.full((4, 4, 4), 0.5), 2, 2)))
    # Scene exposure lifts the HDR tap (ratio path, tap quiet at HDR 0.5).
    exp = encode_map(np.full((4, 4, 4), 0.5), np.full((4, 4, 4), 1.0), 2, 2, hdr_exposure=4.0)
    assert np.all(exp > 0.15) and np.all(exp < 0.3), exp.ravel()[0]
    # Shadow fade: black HDR + dark SDR stays identity (stored 0 = 1x).
    sh = encode_map(np.full((4, 4, 4), 0.02), np.full((4, 4, 4), 0.05), 2, 2)
    assert np.all(sh == 0.0), sh.ravel()[0]
    # Specular boost: fully clipped (all 4 bits) renders at clipBoost.
    # Unmasked brights stay on the ratio path even at tap 0.8.
    cb = encode_map(np.full((4, 4, 4), 0.8), np.full((4, 4, 4), 1.0), 2, 2,
                    clip=np.full((2, 2), 15), clip_boost=8.0)
    assert np.all(cb > 0.55) and np.all(cb < 0.7), cb.ravel()[0]
    cb1 = encode_map(np.full((4, 4, 4), 0.8), np.full((4, 4, 4), 1.0), 2, 2,
                     clip=np.full((2, 2), 1), clip_boost=8.0)
    assert np.all(cb1 > 0.05) and np.all(cb1 < 0.2), cb1.ravel()[0]
    # Detail gate: a masked gradient (recovered detail) keeps the ratio
    # instead of flattening to clipBoost like the uniform case above.
    grad = np.tile(np.linspace(0.5, 1.5, 4, dtype=np.float64)[None, :, None], (4, 1, 4))
    gd = encode_map(grad, np.full((4, 4, 4), 1.0), 2, 2,
                    clip=np.full((2, 2), 15), clip_boost=8.0)
    assert np.all(gd < cb - 0.2), (gd.ravel()[0], cb.ravel()[0])
    # No tap-based detection: bright diffuse WITHOUT a mask stays on the
    # ratio path. White-on-white is identity (no headroom over white);
    # mid-gray-on-mid-gray likewise. (This was the blotch bug: the old tap
    # band rendered any tap ~1.0 at flat clipBoost with a hard shoulder.)
    tap = encode_map(np.full((4, 4, 4), 1.0), np.full((4, 4, 4), 1.0), 2, 2,
                     clip=None, clip_boost=8.0)
    assert np.all(tap == 0.0), tap.ravel()[0]
    tap0 = encode_map(np.full((4, 4, 4), 0.5), np.full((4, 4, 4), 0.5), 2, 2,
                      clip=None, clip_boost=8.0)
    # Ratio path only (linear 0.5 vs sRGB-0.5-decoded 0.21): small, no boost.
    assert np.all(tap0 >= 0.0) and np.all(tap0 < 0.2), tap0.ravel()[0]
    tap1 = encode_map(np.full((4, 4, 4), 0.8), np.full((4, 4, 4), 1.0), 2, 2,
                      clip=None, clip_boost=8.0)
    assert np.all(tap1 == 0.0), tap1.ravel()[0]
    # Chroma protection: saturated orange keeps the full ratio with
    # sat_protect=0 but is attenuated by default, so the multiplicative
    # gain cannot push it into panel clip (lamp stays yellow, not white).
    # Gray inputs are unaffected either way.
    org_hdr = np.zeros((4, 4, 4))
    org_hdr[..., 0], org_hdr[..., 1], org_hdr[..., 2] = 6.0, 2.0, 0.5
    org_sdr = np.zeros((4, 4, 4))
    org_sdr[..., 0], org_sdr[..., 1], org_sdr[..., 2] = 1.0, 0.45, 0.12
    org0 = encode_map(org_hdr, org_sdr, 2, 2, clip=None, clip_boost=0.0, sat_protect=0.0)
    org6 = encode_map(org_hdr, org_sdr, 2, 2, clip=None, clip_boost=0.0, sat_protect=0.6)
    assert np.all(org0 > 0.4) and np.all(org0 < 0.5), org0.ravel()[0]
    assert np.all(org6 > 0.1) and np.all(org6 < 0.25), org6.ravel()[0]
    assert np.all(org6 < org0 - 0.15), (org6.ravel()[0], org0.ravel()[0])
    # Multi-channel: warm HDR over white SDR stores per-channel gains (R >
    # G > B following headroom); gray mode stays R == G == B. Decoding the
    # RGB map reconstructs the tap hue through the white base.
    warm_hdr = np.zeros((4, 4, 4))
    warm_hdr[..., 0], warm_hdr[..., 1], warm_hdr[..., 2] = 4.0, 2.0, 1.0
    rgb = encode_map(warm_hdr, np.full((4, 4, 4), 1.0), 2, 2,
                     clip=None, clip_boost=0.0, sat_protect=0.0, rgb_map=True)
    assert rgb.shape == (2, 2, 3)
    assert np.all(rgb[..., 0] > rgb[..., 1]) and np.all(rgb[..., 1] > rgb[..., 2]), rgb[0, 0]
    rec = decode_gain(rgb) * 1.0
    hue_in = warm_hdr[0, 0, :3] / warm_hdr[0, 0, :3].sum()
    hue_out = rec[0, 0] / rec[0, 0].sum()
    assert np.all(np.abs(hue_in - hue_out) < 0.05), (hue_in, hue_out)
    gry = encode_map(warm_hdr, np.full((4, 4, 4), 1.0), 2, 2,
                     clip=None, clip_boost=0.0, sat_protect=0.0, rgb_map=False)
    assert np.all(gry[..., 0] == gry[..., 1]) and np.all(gry[..., 0] == gry[..., 2])
    # Clipped-core hue fallback: white HDR tap (recovery full-core white) +
    # warm SDR base stores luma gain (near-gray) instead of divergent RGB
    # (white rebuild). Halo case above (warm tap) stays divergent.
    white_hdr = np.full((4, 4, 4), 8.0)
    core_sdr = np.zeros((4, 4, 4))
    core_sdr[..., 0], core_sdr[..., 1], core_sdr[..., 2] = 1.0, 0.45, 0.12
    core = encode_map(white_hdr, core_sdr, 2, 2,
                      clip=None, clip_boost=0.0, sat_protect=0.0, rgb_map=True, blur_sigma=0.0)
    assert core.shape == (2, 2, 3)
    assert np.all(np.abs(core[..., 0] - core[..., 1]) < 0.02)
    assert np.all(np.abs(core[..., 0] - core[..., 2]) < 0.02), core[0, 0]
    # White diffuse (neutral both taps) is unaffected: luma == per-channel.
    wd = encode_map(np.full((4, 4, 4), 4.0), np.full((4, 4, 4), 1.0), 2, 2,
                    clip=None, clip_boost=0.0, sat_protect=0.0, rgb_map=True, blur_sigma=0.0)
    wg = encode_map(np.full((4, 4, 4), 4.0), np.full((4, 4, 4), 1.0), 2, 2,
                    clip=None, clip_boost=0.0, sat_protect=0.0, rgb_map=False, blur_sigma=0.0)
    assert np.all(np.abs(wd[..., 0] - wg[..., 0]) < 0.02), (wd[0, 0], wg[0, 0])
    # Film glow factor: None/unity/strength-0 keep the scene tap; G=2 at full
    # strength doubles the HDR numerator (one extra stop in the map); G<1
    # never dims (core peak survives); the max clamp bounds dark-neighbor lift.
    base = encode_map(np.full((4, 4, 4), 1.0), np.full((4, 4, 4), 0.5), 2, 2,
                      clip=None, clip_boost=0.0, sat_protect=0.0, rgb_map=False, blur_sigma=0.0)
    g1 = encode_map(np.full((4, 4, 4), 1.0), np.full((4, 4, 4), 0.5), 2, 2,
                    clip=None, clip_boost=0.0, sat_protect=0.0, rgb_map=False, blur_sigma=0.0,
                    glow=np.full((4, 4, 4), 1.0), glow_strength=0.5, glow_max=2.0)
    assert np.all(g1 == base), (g1.ravel()[0], base.ravel()[0])
    g0 = encode_map(np.full((4, 4, 4), 1.0), np.full((4, 4, 4), 0.5), 2, 2,
                    clip=None, clip_boost=0.0, sat_protect=0.0, rgb_map=False, blur_sigma=0.0,
                    glow=np.full((4, 4, 4), 4.0), glow_strength=0.0, glow_max=4.0)
    assert np.all(g0 == base), (g0.ravel()[0], base.ravel()[0])
    g2 = encode_map(np.full((4, 4, 4), 1.0), np.full((4, 4, 4), 0.5), 2, 2,
                    clip=None, clip_boost=0.0, sat_protect=0.0, rgb_map=False, blur_sigma=0.0,
                    glow=np.full((4, 4, 4), 2.0), glow_strength=1.0, glow_max=8.0)
    assert np.all(g2 > base + 0.05), (g2.ravel()[0], base.ravel()[0])
    glo = encode_map(np.full((4, 4, 4), 1.0), np.full((4, 4, 4), 0.5), 2, 2,
                     clip=None, clip_boost=0.0, sat_protect=0.0, rgb_map=False, blur_sigma=0.0,
                     glow=np.full((4, 4, 4), 0.25), glow_strength=1.0, glow_max=8.0)
    assert np.all(glo == base), (glo.ravel()[0], base.ravel()[0])
    gcap = encode_map(np.full((4, 4, 4), 1.0), np.full((4, 4, 4), 0.5), 2, 2,
                      clip=None, clip_boost=0.0, sat_protect=0.0, rgb_map=False, blur_sigma=0.0,
                      glow=np.full((4, 4, 4), 8.0), glow_strength=1.0, glow_max=2.0)
    gref = encode_map(np.full((4, 4, 4), 1.0), np.full((4, 4, 4), 0.5), 2, 2,
                      clip=None, clip_boost=0.0, sat_protect=0.0, rgb_map=False, blur_sigma=0.0,
                      glow=np.full((4, 4, 4), 2.0), glow_strength=1.0, glow_max=8.0)
    assert np.all(gcap == gref), (gcap.ravel()[0], gref.ravel()[0])
    # Blur stage: a step edge is smoothed (max adjacent step shrinks), flat
    # fields pass through unchanged, sigma 0 is the exact legacy path.
    step = np.zeros((8, 8, 4))
    step[:, 4:, :] = 1.0
    s0 = encode_map(step, np.full((8, 8, 4), 0.5), 4, 4, blur_sigma=0.0)
    s3 = encode_map(step, np.full((8, 8, 4), 0.5), 4, 4, blur_sigma=3.0)
    d0 = np.abs(np.diff(s0[..., 0], axis=1)).max()
    d3 = np.abs(np.diff(s3[..., 0], axis=1)).max()
    assert d3 < d0, (d3, d0)
    assert d0 > 0.1, d0  # unblurred step is a cliff (~40 LSB here)
    uni = encode_map(np.full((8, 8, 4), 0.7), np.full((8, 8, 4), 1.0), 4, 4, blur_sigma=3.0)
    assert np.all(uni == uni[0, 0, 0]), uni.ravel()[:4]
    print("gainmap_ref self-test ok")
    cb0 = encode_map(np.full((4, 4, 4), 0.5), np.full((4, 4, 4), 1.0), 2, 2,
                     clip=np.zeros((2, 2)), clip_boost=8.0)
    assert np.all(cb0 == 0.0), cb0.ravel()[0]
    print("gainmap_ref self-test ok")


if __name__ == "__main__":
    self_test()
