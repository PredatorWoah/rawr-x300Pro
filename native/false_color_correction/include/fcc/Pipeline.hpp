#pragma once
#include "Config.hpp"
#include "ShaderProvider.hpp"
#include "Types.hpp"
#include <memory>
namespace fcc {
class FalseColorCorrectionPipeline {
public:
    static bool validateConfig(const PipelineConfig&,const char** reason=nullptr) noexcept;
    FalseColorCorrectionPipeline(VulkanContext,ShaderProvider,PipelineConfig);
    ~FalseColorCorrectionPipeline();
    FalseColorCorrectionPipeline(const FalseColorCorrectionPipeline&)=delete;
    FalseColorCorrectionPipeline& operator=(const FalseColorCorrectionPipeline&)=delete;

    // Input is post-white-balance camera-linear RGB, matching the RawTherapee FCC domain.
    // Output may be a different RGBA16F image. steps must be 1..config.maxSteps.
    // The caller owns input/output image layout transitions to GENERAL.
    void record(VkCommandBuffer,const LinearRgbImage& input,const LinearRgbImage& output,uint32_t steps=1);

    const PipelineConfig& config() const noexcept;
    uint64_t currentAllocatedBytes() const noexcept;
    uint64_t peakAllocatedBytes() const noexcept;
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
}
