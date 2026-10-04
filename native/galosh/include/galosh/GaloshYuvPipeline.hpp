#pragma once

#include <cstdint>
#include <mutex>
#include <vulkan/vulkan.h>

#include "galosh/GaloshCommon.hpp"

namespace galosh {

enum class GaloshYuvMode : std::uint8_t {
    Off = 0,        // Caller skips process() (legacy bit-identical).
    Full = 1,       // Luma + chroma lanes (O-variant, single-scale LOESS).
    ChromaOnly = 2  // Luma LOSH lane bypassed (noisy Y guides downstream).
};

struct GaloshYuvParams {
    GaloshYuvMode mode = GaloshYuvMode::Off;
    // Luma LOSH strength (threshold scale). 1.0 = calibrated.
    float strengthY = 1.0f;
    // Chroma dial (2026-08 monotone semantics): 0 = true bypass (dry/wet
    // mix weight 0 in-shader), 0..1 = dry/wet mix, 1 = calibrated,
    // >1 = stiffer MAP-ridge regression (to 3.0).
    float strengthC = 1.0f;
    // O-variant only in P1b (single-scale full-res LOESS, mirrors the vk
    // engine). The multi-scale Q pyramid is CPU-only upstream and a P2
    // candidate (needs CPU-oracle validation, no GPU reference exists).
    // HDR knee (linear Y of the noisy input): at kneeLo the core output
    // rules; at kneeHi the input passes through exactly (smoothstep ramp).
    // The inverse-GAT LUT covers x in [0,1], so 1.0->1.5 keeps highlights
    // exact while everything below 1.0 is fully denoised.
    float hdrKneeLo = 1.0f;
    float hdrKneeHi = 1.5f;
};

// Scene-linear RGB denoiser (GALOSH-YUV core, O-variant, blind,
// training-free). Linear entry: the app tap sits after FCC/defringe and
// before tonemap (hence before film sim and its grain). The sRGB gamma
// wrap of the reference CLI is replaced by app-authored linear bridges
// (BT.709 matrix only, no gamma, no clip — HDR headroom preserved for
// the tonemap; upstream clips only because sRGB demands [0,1]).
// Synchronous stage (own submits + fence waits, like GaloshRawPipeline:
// the blind fit is read back after the frame for logging).
//
// Image contract: RGBA16F GENERAL views in/out (linear RGB, alpha ignored
// on input, written 1.0 on output). Caller barriers dst to SHADER_READ.
class GaloshYuvPipeline {
   public:
    GaloshYuvPipeline(VkPhysicalDevice physicalDevice, VkDevice device, uint32_t queueFamily,
                      GaloshShaderMap shaders);
    ~GaloshYuvPipeline();
    GaloshYuvPipeline(const GaloshYuvPipeline&) = delete;
    GaloshYuvPipeline& operator=(const GaloshYuvPipeline&) = delete;

    // timingPool (nullable): writes slots 14->15 (app expands pool 12->16
    // in P1c; validation passes a 16-query pool).
    void process(VkQueue queue, std::mutex& queueMutex, VkImageView srcView, VkImageView dstView,
                 uint32_t width, uint32_t height, const GaloshYuvParams& params,
                 VkQueryPool timingPool = VK_NULL_HANDLE);

    uint64_t scratchBytes(uint32_t width, uint32_t height) const noexcept;

    // Last blind-fit results (params readback after the frame).
    float lastAlpha() const noexcept;
    float lastSigmaSq() const noexcept;
    float lastSigmaGat() const noexcept;
    // 1.0 when the envelope estimator hit its floor pair and the MAD
    // fallback was adopted (upstream P_ENV_DEGEN slot).
    float lastDegen() const noexcept;

   private:
    struct Impl;
    Impl* impl_ = nullptr;
};

}  // namespace galosh
