#pragma once
#include <gainmap/GainmapCompute.h>
namespace rawrcam::color {
struct UltraHdrParams {
    bool enabled = false;
    // Fixed at 95 until a UI setting exists (StillCaptureCoordinator never
    // forwards a quality; RawPreviewCoordinator defaults to 95). The map
    // carries smooth gradients (sky) at 8-bit: q95 keeps JPEG-introduced
    // contours down on top of the shader's ±0.5 LSB IGN dither.
    int gainmapQuality = 95;
    // Identity floor (1x): the file must never darken the base rendition.
    float gainMapMinLog2 = 0.0f;
    float gainMapMaxLog2 = 4.7090998f;
    float gamma = 1.0f;
    float offsetSdr = 0.015625f;
    float offsetHdr = 0.015625f;
    float hdrCapacityMinLog2 = 0.0f;
    float hdrCapacityMaxLog2 = 4.7090998f;
    // Linear gain for sensor-clipped texels (clip mask nonzero): the HDR tap
    // pins them at sensor white so the measured ratio can only reach the
    // capture exposure there. Positive values add specular dazzle beyond the
    // measured headroom, but flatten large clipped washes into hard-edged
    // discs. Default 0 (pure measured ratio: smooth natural rolloff; the
    // ratio still carries genuine recovered headroom above the exposure).
    float clipBoost = 0.0f;
    // Chroma protection (see gainmap::GainmapParams::satProtect): keeps
    // saturated brights (warm lamps, neon) from clipping white under the
    // multiplicative single-channel gain. Default 0.6.
    float satProtect = 0.6f;
    // Multi-channel (RGB) gain map (see gainmap::GainmapParams): per-channel
    // gains preserve hue through a white SDR base (warm lamp stays yellow).
    // Metadata stays channel-identical so single-channel readers keep
    // parsing; verified on Photos with the lamp/neon scenes.
    bool multiChannelMap = true;
    // Post-encode map blur sigma in map texels (see gainmap::GainmapParams).
    // Default 3.0 (B30 look); 0 disables the blur pass entirely.
    float mapBlurSigma = 3.0f;
    // Film glow factor for film+UltraHDR (see gainmap::GainmapParams): scales
    // the post/pre scatter quotient toward the HDR numerator so halation /
    // camera-diffusion glow keeps headroom with hue. 0 = pure scene tap.
    // Default 0.5; upper clamp 2.0 bounds dark-neighbor bleed. Encode-only.
    float glowStrength = 0.5f;
    float glowMax = 2.0f;

    [[nodiscard]] gainmap::GainmapParams toGainmapParams() const {
        gainmap::GainmapParams p;
        p.minLog2 = gainMapMinLog2;
        p.maxLog2 = gainMapMaxLog2;
        p.gamma = gamma;
        p.offsetSdr = offsetSdr;
        p.offsetHdr = offsetHdr;
        p.hdrCapacityMin = hdrCapacityMinLog2;
        p.hdrCapacityMax = hdrCapacityMaxLog2;
        p.clipBoost = clipBoost;
        p.satProtect = satProtect;
        p.multiChannelMap = multiChannelMap;
        p.mapBlurSigma = mapBlurSigma;
        p.glowStrength = glowStrength;
        p.glowMax = glowMax;
        return p;
    }
};
}  // namespace rawrcam::color
