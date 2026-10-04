#pragma once
#include "Config.hpp"
#include "ShaderProvider.hpp"
#include "Types.hpp"
#include <memory>

namespace quadfix {

// GPU port of the frozen adapt004 quad-cell lattice filter. Same
// caller/resource/synchronization contract as rcd::RcdPipeline: the pipeline
// only records into a caller-owned command buffer (single flight), owns its
// scratch images, and leaves the output buffer readable by subsequent
// shader stages (trailing buffer barrier in record()).
//
// Placement: after DngSource normalization + lens shading, before RCD, on
// the same tile the host already stages for upload. The host reads an
// expanded tile (halo covers the Gabor support, see QH in the caller),
// runs quadfix, compacts the core, and feeds RCD unchanged.
class QuadfixPipeline {
public:
    static bool validateConfig(const PipelineConfig& config,
                               const PipelineAssets& assets = {},
                               const char** reason = nullptr) noexcept;

    QuadfixPipeline(VulkanContext context,
                    ShaderProvider shaderProvider,
                    PipelineConfig config,
                    PipelineAssets assets = {});
    ~QuadfixPipeline();

    QuadfixPipeline(const QuadfixPipeline&) = delete;
    QuadfixPipeline& operator=(const QuadfixPipeline&) = delete;

    // Filters input -> output (distinct buffers; in-place is not supported).
    // Both views must match cfg width/height; offsets must be 0.
    void record(VkCommandBuffer commandBuffer,
                const BayerBufferView& input,
                const BayerBufferView& output);

    const PipelineConfig& config() const;
    uint64_t currentAllocatedBytes() const;
    uint64_t peakAllocatedBytes() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace quadfix
