#pragma once
#include <algorithm>
#include <cstdint>

// Inpaint Opposed (RawTherapee Coloropp) push-constant layouts and the mapping
// from user settings to shader parameters. Shared by the still/video
// ColoroppProcessor and the preview so both render the same highlights.
namespace rawr::highlight {

// coloropp.comp
struct ColoroppPush {
    uint32_t width, height, gridWidth, gridHeight, cfaPattern;
    float clipval;
    uint32_t gridEnabled, pad;
    float wb[4];
    uint32_t sensorWidth, sensorHeight, cropX, cropY, sensorScale;
};
// coloropp.comp built with RAWR_COLOROPP_FUSED_WB=1: the vec4 gains follow at
// the next 16-byte boundary.
struct ColoroppFusedPush {
    ColoroppPush base;
    uint32_t pad[3];
    float gains[4];
};
static_assert(sizeof(ColoroppFusedPush) == 96);
static_assert(sizeof(ColoroppPush) == 68);

// coloropp_tone.comp
struct ColoroppTonePush {
    uint32_t width, height;
    float compression, exposureGain;
};
static_assert(sizeof(ColoroppTonePush) == 16);

// Clip level relative to the lens-shaded sensor ceiling for the user threshold.
inline float coloroppClipValue(float threshold) { return .987f / (1.2f * std::clamp(threshold, .5f, 2.0f)); }
inline float coloroppWhiteBalance(float gain) { return std::max(gain, 1e-6f); }
inline float coloroppToneCompression(float compression) { return std::clamp(compression, 0.0f, 300.0f) / 100.0f; }
inline float coloroppToneExposureGain(float gain) { return std::max(gain, 0.0f); }

}  // namespace rawr::highlight
