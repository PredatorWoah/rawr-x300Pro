#pragma once

#include <cstdint>
#include <mutex>
#include <vulkan/vulkan.h>

#include "galosh/GaloshCommon.hpp"

namespace galosh {

// Kernel names are native/galosh/shaders/SHADER_LIST (o32_* for RAW).
// Missing entries are validated in the P1 constructor (throws).

// CFA pattern, app-wide convention (geometry/CfaPattern.h):
// 0=RGGB, 1=GRBG, 2=GBRG, 3=BGGR. P1a supports Rggb only (upstream bakes
// quad phase 0 into the shaders); other codes throw in process() and are
// a P1b extension (phase-aware pushes).
enum class GaloshCfa : std::uint32_t { Rggb = 0, Grbg = 1, Gbrg = 2, Bggr = 3 };

enum class GaloshRawMode : std::uint8_t {
    Off = 0,        // Caller skips process() (legacy bit-identical).
    Full = 1,       // Luma + chroma lanes.
    ChromaOnly = 2  // Luma LOSH lane bypassed (noisy L guides downstream).
};

struct GaloshRawParams {
    // Global multiplier (1.0 = calibrated). Scales luma + chroma thresholds.
    float strength = 1.0f;
    // Luma WHT-shrinkage strength. <= 0 selects the luma policy below.
    float lumaStrength = 1.0f;
    // Chroma LOESS strength. 1.0 = calibrated estimate.
    float chromaStrength = 1.0f;
    // Blind Poisson-Gaussian model (variance = alpha*x + sigmaSq) in [0,1]
    // normalized Bayer domain. alpha <= 0 && sigmaSq <= 0 = fully blind
    // (per-frame fit, default). Positive pair = shared external estimate
    // (P1: single estimate pushed to both engines).
    float alpha = 0.0f;
    float sigmaSq = 0.0f;
    // Sensor CFA layout. P1a supports Rggb only (upstream bakes quad phase
    // 0 into the shaders); other codes throw in process() (P1b extension).
    GaloshCfa cfa = GaloshCfa::Rggb;
    // Luma WHT block size. 8 = paper configuration; 4 = video/fast mode
    // (measured -1.2dB on high noise, neutral on low ISO).
    int whtBlock = 8;
    // false = EWA-jinc chroma upsample (quality, default);
    // true = fast upsample (quality-neutral on stills, ~-20% CPU time).
    bool upsampleFast = false;
    // R16U <-> [0,1] bridge mapping (still-profiled black/white convention:
    // normalized x = (x_raw - black) / (white - black), green-black mean
    // like resolveDenoiseNoise). Drives the app-authored bridge kernels.
    float black = 0.0f;
    float white = 1.0f;
    // Full = luma + chroma lanes. ChromaOnly = luma LOSH lane bypassed
    // (noisy L guides downstream), chroma pyramid runs fully; luma detail
    // preserved by construction. Requires the Wiener-NaN guard (ported
    // from UPSTREAM_PATCHES.patch): strength 0 through shrinkage is
    // 0/0 = NaN on flat blocks.
    GaloshRawMode mode = GaloshRawMode::Off;
};

// Pre-demosaic Bayer denoiser (GALOSH-RAW, blind, training-free).
// Synchronous stage (own submits + fence waits, like the demosaic workers):
// upstream needs two mid-pipe host syncs (blind-fit readback, LUT/state),
// so record-only single-submit is a P2 optimization, not P1a.
// Operates on the still-render Bayer copy (R16_UINT in/out); DNG save path
// must bypass (raw sensor data stays raw). Caller must guarantee GPU idle
// across geometry changes (transient buffers are realloc'd per epoch).
class GaloshRawPipeline {
   public:
    GaloshRawPipeline(VkPhysicalDevice physicalDevice, VkDevice device, uint32_t queueFamily,
                      GaloshShaderMap shaders);
    ~GaloshRawPipeline();
    GaloshRawPipeline(const GaloshRawPipeline&) = delete;
    GaloshRawPipeline& operator=(const GaloshRawPipeline&) = delete;

    // Runs the full graph on queue (mutex held for the whole call):
    // normalize -> P0 fit -> GAT/LUT/IRLS -> luma (+bypass) -> chroma
    // pyramid -> inverse -> quantize to dstView (GENERAL, SHADER_WRITE on
    // return; caller barriers to SHADER_READ). srcView must be GENERAL.
    // timingPool (nullable): writes slots 12->13 (app expands pool 12->16
    // in P1c; P1a tap passes null).
    void process(VkQueue queue, std::mutex& queueMutex, VkImageView srcView, VkImageView dstView,
                 uint32_t width, uint32_t height, const GaloshRawParams& params,
                 VkQueryPool timingPool = VK_NULL_HANDLE);

    // Transient footprint for budgeting (buffers + galosh scratch image).
    uint64_t scratchBytes(uint32_t width, uint32_t height) const noexcept;

    // Last blind-fit results from process() (SYNC#1 readback; ext-model
    // override when params carry it). Validation logging only.
    float lastAlpha() const noexcept;
    float lastSigmaSq() const noexcept;

   private:
    struct Impl;
    Impl* impl_ = nullptr;
};

}  // namespace galosh
