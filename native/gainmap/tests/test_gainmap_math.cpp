// Host fp32 CPU mirror of shaders/gainmap.comp + python/gainmap_ref.py.
// Validates validateParams() and the per-pixel encode on ramps/patches.
#include <cmath>
#include <cstdio>
#include <cstring>

#include "gainmap/GainmapCompute.h"

namespace {
float srgbEotf(float c) {
    if (c <= 0.04045f) return c / 12.92f;
    const float hi = (c + 0.055f) / 1.055f;
    return std::pow(std::fmax(hi, 0.0f), 2.4f);
}

float luma709(float r, float g, float b) { return 0.2126f * r + 0.7152f * g + 0.0722f * b; }

float sanitizeF(float c) { return std::isfinite(c) ? c : 0.0f; }

float smoothstep(float e0, float e1, float x) {
    const float t = std::fmin(std::fmax((x - e0) / (e1 - e0), 0.0f), 1.0f);
    return t * t * (3.0f - 2.0f * t);
}

void encodePixel(const float hdr[3], const float sdr[3], const gainmap::GainmapParams& p, float out[3],
                   float clipCover = 0.0f, float flatness = 1.0f, const float* glow = nullptr) {
    // Scene-exposure match (mirrors the shader): the HDR tap is pre-exposure.
    const float e = std::fmax(p.hdrExposure, 1e-6f);
    const float bSat[3] = {std::fmax(srgbEotf(std::fmin(std::fmax(sanitizeF(sdr[0]), 0.0f), 1.0f)), 0.0f),
                           std::fmax(srgbEotf(std::fmin(std::fmax(sanitizeF(sdr[1]), 0.0f), 1.0f)), 0.0f),
                           std::fmax(srgbEotf(std::fmin(std::fmax(sanitizeF(sdr[2]), 0.0f), 1.0f)), 0.0f)};
    const float sdrMaxC = std::fmax(bSat[0], std::fmax(bSat[1], bSat[2]));
    const float sdrMinC = std::fmin(bSat[0], std::fmin(bSat[1], bSat[2]));
    const float sdrSatC = (sdrMaxC - sdrMinC) / std::fmax(sdrMaxC, 1e-3f);
    const float satW =
        p.satProtect * smoothstep(0.5f, 0.9f, sdrMaxC) * std::fmin(std::fmax(sdrSatC, 0.0f), 1.0f);
    float hdrLin[3] = {0, 0, 0};
    // Film glow factor (mirrors the shader): dimensionless post/pre
    // quotient applied pre-CST; G=1 never dims, clamped at glowMax, scaled
    // by strength. Null glow keeps the pure scene tap.
    float hw[3] = {sanitizeF(hdr[0]) * e, sanitizeF(hdr[1]) * e, sanitizeF(hdr[2]) * e};
    if (glow != nullptr && p.glowStrength > 0.0f) {
        const float gs = std::fmin(std::fmax(p.glowStrength, 0.0f), 1.0f);
        const float gm = std::fmax(p.glowMax, 1.0f);
        for (int c = 0; c < 3; ++c) {
            const float gc = std::isfinite(glow[c]) ? std::fmax(glow[c], 0.0f) : 0.0f;
            const float gt = std::fmin(std::fmax(gc, 1.0f), gm);
            hw[c] *= 1.0f + (gt - 1.0f) * gs;
        }
    }
    for (int r = 0; r < 3; ++r)
        hdrLin[r] = p.hdrToLinearSrgbRowMajor[r * 3 + 0] * hw[0] +
                    p.hdrToLinearSrgbRowMajor[r * 3 + 1] * hw[1] +
                    p.hdrToLinearSrgbRowMajor[r * 3 + 2] * hw[2];
    float hdrPos[3] = {std::fmax(hdrLin[0], 0.0f), std::fmax(hdrLin[1], 0.0f), std::fmax(hdrLin[2], 0.0f)};
    const float h = luma709(hdrPos[0], hdrPos[1], hdrPos[2]);
    const float s = luma709(std::fmax(srgbEotf(std::fmin(std::fmax(sanitizeF(sdr[0]), 0.0f), 1.0f)), 0.0f),
                            std::fmax(srgbEotf(std::fmin(std::fmax(sanitizeF(sdr[1]), 0.0f), 1.0f)), 0.0f),
                            std::fmax(srgbEotf(std::fmin(std::fmax(sanitizeF(sdr[2]), 0.0f), 1.0f)), 0.0f));
    // Single-channel luminance gain + per-channel RGB gains (matches the
    // shader): toneScale (shadow fade × chroma guard) is shared.
    const float gain = (h + std::fmax(p.offsetHdr, 1e-6f)) / (s + std::fmax(p.offsetSdr, 1e-6f));
    float logGain = std::log2(std::fmax(gain, 1e-9f));
    float logC[3];
    for (int c = 0; c < 3; ++c) {
        const float g = (hdrPos[c] + std::fmax(p.offsetHdr, 1e-6f)) / (bSat[c] + std::fmax(p.offsetSdr, 1e-6f));
        logC[c] = std::log2(std::fmax(g, 1e-9f));
    }
    // Shadow fade: deep-shadow quotients encode curve toe, not headroom.
    const float toneScale = smoothstep(0.03f, 0.50f, s) * (1.0f - satW);
    logGain *= toneScale;
    for (int c = 0; c < 3; ++c) logC[c] *= toneScale;
    // Clipped-core hue fallback (mirrors the shader): white HDR tap +
    // colored SDR base uses luma gain so decoded HDR keeps SDR hue.
    const float hdrMaxC = std::fmax(hdrPos[0], std::fmax(hdrPos[1], hdrPos[2]));
    const float hdrMinC = std::fmin(hdrPos[0], std::fmin(hdrPos[1], hdrPos[2]));
    const float hdrSatC = (hdrMaxC - hdrMinC) / std::fmax(hdrMaxC, 1e-3f);
    const float hdrWhiteW =
        (1.0f - smoothstep(0.05f, 0.15f, hdrSatC)) * smoothstep(0.5f, 0.9f, h);
    const float mcOn = p.multiChannelMap ? 1.0f : 0.0f;
    const float useLuma = std::fmax(1.0f - mcOn, hdrWhiteW * mcOn);
    float logF[3];
    for (int c = 0; c < 3; ++c) logF[c] = logC[c] * (1.0f - useLuma) + logGain * useLuma;
    // Specular boost for sensor-clipped texels. Cover comes STRICTLY from
    // the mask (no tap-based detection: it false-fired on bright diffuse),
    // AND from local flatness: masked texels whose HDR tap still carries a
    // gradient (highlight recovery) keep the ratio; only flat-pinned blocks
    // render at clipBoost. Gated on max SDR channel (saturated primaries
    // hit 1.0 in one channel at low luma) so near-white SDR holding
    // gradation keeps the measured ratio.
    if (clipCover != 0.0f && p.clipBoost > 0.0f) {
        const float b0 = std::fmax(srgbEotf(std::fmin(std::fmax(sanitizeF(sdr[0]), 0.0f), 1.0f)), 0.0f);
        const float b1 = std::fmax(srgbEotf(std::fmin(std::fmax(sanitizeF(sdr[1]), 0.0f), 1.0f)), 0.0f);
        const float b2 = std::fmax(srgbEotf(std::fmin(std::fmax(sanitizeF(sdr[2]), 0.0f), 1.0f)), 0.0f);
        const float smax = std::fmax(b0, std::fmax(b1, b2));
        const float gt = std::fmin(std::fmax((smax - 0.85f) / (0.98f - 0.85f), 0.0f), 1.0f);
        clipCover *= gt * gt * (3.0f - 2.0f * gt);
        clipCover *= std::fmin(std::fmax(flatness, 0.0f), 1.0f);
        clipCover *= 1.0f - satW;
        const float boostLog = std::log2(std::fmax(p.clipBoost, 1e-6f));
        logGain = logGain * (1.0f - clipCover) + boostLog * clipCover;
        for (int c = 0; c < 3; ++c) logF[c] = logF[c] * (1.0f - clipCover) + boostLog * clipCover;
    }
    for (int c = 0; c < 3; ++c) {
        const float norm = std::fmin(
            std::fmax((logF[c] - p.minLog2) / std::fmax(p.maxLog2 - p.minLog2, 1e-6f), 0.0f), 1.0f);
        out[c] = std::fmin(std::fmax(std::floor(std::pow(norm, p.gamma) * 255.0f + 0.5f) / 255.0f, 0.0f),
                           1.0f);
    }
}

float decodeGain(float stored, const gainmap::GainmapParams& p) {
    return std::exp2(p.minLog2 + (p.maxLog2 - p.minLog2) * stored);
}

int failures = 0;
void check(bool ok, const char* what) {
    if (!ok) {
        ++failures;
        std::printf("FAIL %s\n", what);
    }
}
}  // namespace

int main() {
    gainmap::GainmapParams p;
    check(gainmap::GainmapCompute::validateParams(p), "defaults valid");
    gainmap::GainmapParams bad = p;
    bad.maxLog2 = bad.minLog2;
    check(!gainmap::GainmapCompute::validateParams(bad), "max<=min rejected");
    bad = p;
    bad.gamma = 0.0f;
    check(!gainmap::GainmapCompute::validateParams(bad), "gamma<=0 rejected");
    bad = p;
    bad.offsetSdr = 0.0f;
    check(!gainmap::GainmapCompute::validateParams(bad), "offsetSdr==0 rejected (encode div-by-zero)");
    bad = p;
    bad.offsetHdr = 0.0f;
    check(!gainmap::GainmapCompute::validateParams(bad), "offsetHdr==0 rejected");
    bad = p;
    bad.hdrExposure = 0.0f;
    check(!gainmap::GainmapCompute::validateParams(bad), "hdrExposure==0 rejected");
    bad = p;
    bad.hdrExposure = -2.0f;
    check(!gainmap::GainmapCompute::validateParams(bad), "hdrExposure<0 rejected");
    bad = p;
    bad.glowStrength = 1.5f;
    check(!gainmap::GainmapCompute::validateParams(bad), "glowStrength>1 rejected");
    bad = p;
    bad.glowMax = 0.5f;
    check(!gainmap::GainmapCompute::validateParams(bad), "glowMax<1 rejected");

    // Black HDR + mid SDR -> floor; bright HDR + bright SDR decodes to the
    // true headroom (~7.9x).
    {
        const float hdr[3] = {0, 0, 0};
        const float sdr[3] = {0.5f, 0.5f, 0.5f};
        float out[3]{};
        encodePixel(hdr, sdr, p, out);
        check(out[0] < 0.2f && out[1] < 0.2f && out[2] < 0.2f, "black maps to floor");
    }
    {
        const float hdr[3] = {8, 8, 8};
        const float sdr[3] = {1, 1, 1};
        float out[3]{};
        encodePixel(hdr, sdr, p, out);
        const float g = decodeGain(out[0], p);
        check(g > 6.9f && g < 8.9f, "bright decodes to true headroom");
    }
    // Shadow fade: near-black stays identity (stored 0 = 1x).
    {
        const float hdr[3] = {0.02f, 0.02f, 0.02f};
        const float sdr[3] = {0.05f, 0.05f, 0.05f};
        float out[3]{};
        encodePixel(hdr, sdr, p, out);
        check(out[0] == 0.0f && out[1] == 0.0f && out[2] == 0.0f, "shadow fade holds identity");
    }
    // Specular boost: clipped cover renders at clipBoost, clear untouched.
    {
        gainmap::GainmapParams pe = p;
        pe.clipBoost = 8.0f;
        const float hdr[3] = {1, 1, 1};
        const float sdr[3] = {1, 1, 1};
        float out[3]{};
        encodePixel(hdr, sdr, pe, out, 1.0f);
        check(out[0] > 0.6f && out[0] < 0.75f, "clip boost hits 8x");
        const float hdrMid[3] = {0.5f, 0.5f, 0.5f};
        encodePixel(hdrMid, sdr, pe, out, 0.0f);
        check(out[0] == 0.0f && out[1] == 0.0f && out[2] == 0.0f, "quiet tap + clear mask keeps identity");
    }
    // Detail gate: full mask cover but recovered gradient (flatness 0)
    // keeps the ratio; flat-pinned (flatness 1) renders at clipBoost.
    {
        gainmap::GainmapParams pe = p;
        pe.clipBoost = 8.0f;
        const float hdr[3] = {1, 1, 1};
        const float sdr[3] = {1, 1, 1};
        float out[3]{};
        encodePixel(hdr, sdr, pe, out, 1.0f, 0.0f);
        check(out[0] == 0.0f && out[1] == 0.0f && out[2] == 0.0f, "detail keeps ratio despite mask");
        encodePixel(hdr, sdr, pe, out, 1.0f, 1.0f);
        check(out[0] > 0.6f && out[0] < 0.75f, "pinned mask boosts");
    }
    // No tap-based detection: bright diffuse WITHOUT a mask stays on the
    // ratio path. White-on-white is identity (no headroom over white), and
    // mid-gray HDR stays near-identity too. (This was the blotch bug: the
    // old tap band rendered any tap ~1.0 at flat clipBoost.)
    {
        gainmap::GainmapParams pe = p;
        pe.clipBoost = 8.0f;
        const float hdr[3] = {1, 1, 1};
        const float sdr[3] = {1, 1, 1};
        float out[3]{};
        encodePixel(hdr, sdr, pe, out);
        check(out[0] == 0.0f && out[1] == 0.0f && out[2] == 0.0f, "white without mask stays identity");
        const float hdr2[3] = {0.5f, 0.5f, 0.5f};
        encodePixel(hdr2, sdr, pe, out);
        check(out[0] == 0.0f, "tap quiet at mid gray");
    }
    // Saturated blue specular: B clips (mask bit only) at luma ~0.07, far
    // below any luminance gate. Max-channel gating must still lift it above
    // the pure-ratio floor (partial cover keeps mostly the ratio by design).
    // Chroma protection tempers it further so the blue survives instead of
    // washing white.
    {
        gainmap::GainmapParams pe = p;
        pe.clipBoost = 8.0f;
        const float hdr[3] = {0.1f, 0.1f, 1.0f};
        const float sdr[3] = {0.1f, 0.1f, 1.0f};
        float out[3]{};
        encodePixel(hdr, sdr, pe, out, 0.25f);
        check(out[0] > 0.04f && out[0] < 0.10f, "blue specular lifts above ratio floor, chroma kept");
    }
    // Chroma protection: saturated orange is attenuated by default but keeps
    // the full ratio with satProtect = 0. Gray inputs are unaffected either
    // way (covered above).
    {
        gainmap::GainmapParams pe = p;
        pe.clipBoost = 0.0f;
        const float hdr[3] = {6.0f, 2.0f, 0.5f};
        const float sdr[3] = {1.0f, 0.45f, 0.12f};
        float out[3]{};
        gainmap::GainmapParams pe0 = pe;
        pe0.satProtect = 0.0f;
        encodePixel(hdr, sdr, pe0, out);
        check(out[0] > 0.4f && out[0] < 0.5f, "orange unprotected keeps ratio");
        encodePixel(hdr, sdr, pe, out);
        check(out[0] > 0.1f && out[0] < 0.25f, "orange protected stays chromatic");
    }
    // Single-channel map: R == G == B even for chromatic input, so the
    // single-channel ISO/XMP signal in the file inverts it exactly.
    // (Bright SDR: SDR 0.5 sits in the shadow-fade zone by design.)
    {
        gainmap::GainmapParams pe = p;
        pe.multiChannelMap = false;
        const float hdr[3] = {16, 0.2f, 0.2f};
        const float sdr[3] = {1.0f, 1.0f, 1.0f};
        float out[3]{};
        encodePixel(hdr, sdr, pe, out);
        check(out[0] == out[1] && out[0] == out[2], "map is single-channel gray");
    }
    // Multi-channel: warm HDR tap over white SDR stores divergent gains
    // (hue survives); white tap over warm SDR falls back to luma (near-gray)
    // so clipped cores keep SDR hue instead of rebuilding white.
    {
        gainmap::GainmapParams pe = p;
        pe.clipBoost = 0.0f;
        pe.satProtect = 0.0f;
        const float warm[3] = {4.0f, 2.0f, 1.0f};
        const float white[3] = {1.0f, 1.0f, 1.0f};
        float out[3]{};
        encodePixel(warm, white, pe, out);
        check(out[0] > out[1] && out[1] > out[2], "warm tap stays per-channel");
        const float flat[3] = {8.0f, 8.0f, 8.0f};
        const float lamp[3] = {1.0f, 0.45f, 0.12f};
        encodePixel(flat, lamp, pe, out);
        check(std::fabs(out[0] - out[1]) < 0.02f && std::fabs(out[0] - out[2]) < 0.02f,
              "white tap falls back to luma");
    }
    // Non-finite HDR sanitizes instead of poisoning the map.
    {
        const float hdr[3] = {NAN, INFINITY, 0.2f};
        const float sdr[3] = {0.5f, 0.5f, 0.5f};
        float out[3]{};
        encodePixel(hdr, sdr, p, out);
        check(std::isfinite(out[0]) && out[0] >= 0.0f && out[0] <= 1.0f, "nan/inf hdr sanitized");
    }
    // Scene exposure: clipped-white HDR (1.0) at 4x exposure vs white SDR
    // Scene exposure lifts the HDR tap on the ratio path (tap quiet below
    // the 0.90 band so exposure alone is measured).
    {
        gainmap::GainmapParams pe = p;
        pe.hdrExposure = 4.0f;
        const float hdr[3] = {0.5f, 0.5f, 0.5f};
        const float sdr[3] = {1, 1, 1};
        float out[3]{};
        encodePixel(hdr, sdr, pe, out);
        check(out[0] > 0.15f && out[0] < 0.3f, "hdrExposure lifts tap on ratio path");
    }
    // Monotonic in HDR.
    {
        float prev = -1.0f;
        for (int i = 0; i <= 8; ++i) {
            const float hdr[3] = {0.25f * i, 0.25f * i, 0.25f * i};
            const float sdr[3] = {0.5f, 0.5f, 0.5f};
            float out[3]{};
            encodePixel(hdr, sdr, p, out);
            check(out[0] + 1e-6f >= prev, "monotonic in hdr");
            prev = out[0];
        }
    }
    // Film glow factor: null/unity/strength-0 keep the scene tap; G=2 at
    // full strength lifts the map; G<1 never dims the core; the max clamp
    // bounds dark-neighbor lift.
    {
        gainmap::GainmapParams pe = p;
        pe.multiChannelMap = false;
        pe.satProtect = 0.0f;
        const float hdr[3] = {1.0f, 1.0f, 1.0f};
        const float sdr[3] = {0.5f, 0.5f, 0.5f};
        float out[3]{}, ref[3]{};
        encodePixel(hdr, sdr, pe, ref);
        const float unity[3] = {1.0f, 1.0f, 1.0f};
        encodePixel(hdr, sdr, pe, out, 0.0f, 1.0f, unity);
        check(out[0] == ref[0] && out[1] == ref[1] && out[2] == ref[2], "unity glow keeps scene tap");
        const float hot[3] = {4.0f, 4.0f, 4.0f};
        gainmap::GainmapParams pe0 = pe;
        pe0.glowStrength = 0.0f;
        encodePixel(hdr, sdr, pe0, out, 0.0f, 1.0f, hot);
        check(out[0] == ref[0], "strength-0 glow ignored");
        gainmap::GainmapParams pe1 = pe;
        pe1.glowStrength = 1.0f;
        pe1.glowMax = 8.0f;
        const float two[3] = {2.0f, 2.0f, 2.0f};
        encodePixel(hdr, sdr, pe1, out, 0.0f, 1.0f, two);
        check(out[0] > ref[0] + 0.05f, "G=2 lifts the map");
        const float dim[3] = {0.25f, 0.25f, 0.25f};
        encodePixel(hdr, sdr, pe1, out, 0.0f, 1.0f, dim);
        check(out[0] == ref[0], "G<1 never dims");
        const float huge[3] = {8.0f, 8.0f, 8.0f};
        gainmap::GainmapParams pe2 = pe;
        pe2.glowStrength = 1.0f;
        pe2.glowMax = 2.0f;
        encodePixel(hdr, sdr, pe2, out, 0.0f, 1.0f, huge);
        float capped[3]{};
        encodePixel(hdr, sdr, pe1, capped, 0.0f, 1.0f, two);
        check(out[0] == capped[0], "glow clamp bounds lift");
    }
    if (failures == 0) std::printf("gainmap_math_test ok\n");
    return failures == 0 ? 0 : 1;
}
