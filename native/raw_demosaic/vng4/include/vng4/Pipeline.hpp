#pragma once
#include "Config.hpp"
#include "ShaderProvider.hpp"
#include "Telemetry.hpp"
#include "Types.hpp"
#include <functional>
#include <memory>
namespace vng4 {
class Vng4Pipeline {
public:
 static bool validateConfig(const PipelineConfig&, const PipelineAssets& = {}, const char** reason=nullptr) noexcept;
 Vng4Pipeline(VulkanContext, ShaderProvider, PipelineConfig, PipelineAssets={}); ~Vng4Pipeline();
 Vng4Pipeline(const Vng4Pipeline&)=delete; Vng4Pipeline& operator=(const Vng4Pipeline&)=delete;
 void record(VkCommandBuffer,const RawCfaImageView&,const LinearRgbImage&);
 void record(VkCommandBuffer,const NormalizedBayerBufferView&,const LinearRgbImage&);
 void record(VkCommandBuffer,const PackedCfaImageView&,const LinearRgbImage&);
 void recordGreenDiagnostic(VkCommandBuffer,uint32_t x,uint32_t y,VkBuffer diagnosticBuffer,VkDeviceSize diagnosticRange);
 void recordBlended(VkCommandBuffer,const RawCfaImageView&,const LinearRgbImage&,VkImageView blendMask,float finalOutputFactor,float finalOutputAlpha);
 void recordBlended(VkCommandBuffer,const NormalizedBayerBufferView&,const LinearRgbImage&,VkImageView blendMask,float finalOutputFactor,float finalOutputAlpha);
 void recordBlended(VkCommandBuffer,const PackedCfaImageView&,const LinearRgbImage&,VkImageView blendMask,float finalOutputFactor,float finalOutputAlpha);
 // Optional hook run between recorded passes (after each pass's barrier); the caller may submit
 // and re-begin the same command buffer to bound one GPU submission. See rcd::RcdPipeline.
 void setPassBoundary(std::function<void()> boundary);
 const PipelineConfig& config() const; bool collectTelemetry(FrameTelemetry&); uint64_t currentAllocatedBytes() const; uint64_t peakAllocatedBytes() const;
private: struct Impl; std::unique_ptr<Impl> impl_;
};
}
