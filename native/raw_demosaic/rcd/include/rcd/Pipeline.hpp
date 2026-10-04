#pragma once
#include "Config.hpp"
#include "ShaderProvider.hpp"
#include "Telemetry.hpp"
#include "Types.hpp"
#include <functional>
#include <memory>

namespace rcd {

// Vulkan RCD recorder with the same caller/resource/synchronization contract as
// The pipeline only records into a caller-owned command buffer.
class RcdPipeline {
public:
    static bool validateConfig(const PipelineConfig& config,
                               const PipelineAssets& assets={},
                               const char** reason=nullptr) noexcept;

    RcdPipeline(VulkanContext context,
                ShaderProvider shaderProvider,
                PipelineConfig config,
                PipelineAssets assets={});
    ~RcdPipeline();

    RcdPipeline(const RcdPipeline&)=delete;
    RcdPipeline& operator=(const RcdPipeline&)=delete;

    void record(VkCommandBuffer commandBuffer,
                const RawCfaImageView& input,
                const LinearRgbImage& output);
    void record(VkCommandBuffer commandBuffer,
                const NormalizedBayerBufferView& input,
                const LinearRgbImage& output);
    void record(VkCommandBuffer commandBuffer,
                const PackedCfaImageView& input,
                const LinearRgbImage& output);

    // Optional hook run between recorded passes (after each pass's barrier).
    // The caller may submit what is recorded so far and re-begin the same
    // command buffer before returning, bounding the length of one GPU
    // submission so other queues (e.g. a live preview) are not starved.
    // Recording resumes on the same VkCommandBuffer handle.
    void setPassBoundary(std::function<void()> boundary);

    const PipelineConfig& config() const;
    bool collectTelemetry(FrameTelemetry& out);
    uint64_t currentAllocatedBytes() const;
    uint64_t peakAllocatedBytes() const;
    void forgetExternalImageView(VkImageView imageView);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace rcd
