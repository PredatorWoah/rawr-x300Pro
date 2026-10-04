#pragma once
#include <atomic>
#include <cstdint>
#include <memory>
#include <vector>

#include "DngSource.h"
#include "develop/render/StillImageRenderer.h"
namespace rawrcam::renderer {
class RendererSurface;
struct RenderOptions {
    develop::rendered::RenderedStillContext context;
    uint32_t width = 0, height = 0;
    float temperature = 0, tint = 0;
    bool overview = false;
    bool bayerBin2x = false;
    bool surfacePreview = false;
    bool shading = true;
    bool distortion = true;
    std::string demosaic = "Rcd";
    bool dualAutoContrast = true;
    float dualContrastPercent = 20;
    // adapt004 quad-cell lattice pre-filter (after normalize+shading, before
    // demosaic). Default off: zero behavior change until device validation.
    // Auto-applies to the affected sensor even when false? No — explicit only.
    bool quadfix = false;
    bool quadfixFastMedian = false;
};
class RendererEngine {
   public:
    RendererEngine(const std::string& filesDir, const std::string& libraryDir, AAssetManager* assets);
    ~RendererEngine();
    void prepareProxy(DngSource& source, const std::string& cachePath, bool shading);
    void prepare(DngSource& source, const std::string& cachePath, const RenderOptions& options);
    std::vector<uint8_t> render(DngSource& source, const std::string& cachePath, const RenderOptions& options);
    void setSurface(ANativeWindow* window);
    bool hasSurface() const noexcept;
    // Last post-demosaic completion (GPU stage times, setup/gaps/readback).
    // Populated by render(); valid only when render() succeeded. Lets the
    // JPEG export path report the same TIMINGS stages as still captures.
    const develop::rendered::RenderedStillCompletion& lastCompletion() const { return lastCompletion_; }
    // UltraHDR gain map for the last render() (half-res RGBA8, A ignored).
    // Empty when the render ran without ultraHdrEnabled (preview, SDR
    // export, or map missing). Copied before releasePixels() so the JPEG
    // exporter holds stable memory until its writer completes, mirroring
    // SingleFrame/MFSR ownership (processor pixels stay mapped there).
    const std::vector<uint8_t>& lastGainmap() const { return lastGainmap_; }
    uint32_t lastGainmapWidth() const { return lastGainmapWidth_; }
    uint32_t lastGainmapHeight() const { return lastGainmapHeight_; }
    // Wall-clock demosaic cost for the current output: prepare() tiled
    // demosaic plus render() resample/distortion pre-work. Each stage
    // contributes only the work it actually performed (0 on cache hit).
    double lastDemosaicMs() const { return lastDemosaicMs_; }
    std::atomic<bool> cancelled{false};
    std::atomic<int> progress{0};

   private:
    void check();
    std::string filesDir_;
    vulkan::VulkanContext vulkan_;
    std::mutex& queueMutex_ = vulkan_.primaryQueue().mutex();
    develop::rendered::StillImageRenderer processor_;
    std::unique_ptr<RendererSurface> surface_;
    // Pristine linear overview. PostDemosaicProcessor writes its input, so
    // render() copies this image before handing it to the still pipeline.
    vulkan::OwnedImage overviewInput_{};
    std::string overviewInputKey_;
    develop::rendered::RenderedStillCompletion lastCompletion_{};
    std::vector<uint8_t> lastGainmap_;
    uint32_t lastGainmapWidth_ = 0;
    uint32_t lastGainmapHeight_ = 0;
    double lastDemosaicMs_ = 0.0;
};
}  // namespace rawrcam::renderer
