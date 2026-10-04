#pragma once

#include <vulkan/vulkan.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>

#include "imaging/FrameLimits.h"

namespace rawrcam::diagnostics {

class PipelineDiagnostics final {
   public:
    using Diagnostic = std::function<void(const std::string&)>;

    explicit PipelineDiagnostics(Diagnostic diagnostic);
    ~PipelineDiagnostics();

    PipelineDiagnostics(const PipelineDiagnostics&) = delete;
    PipelineDiagnostics& operator=(const PipelineDiagnostics&) = delete;

    void initialize(VkDevice device, uint32_t rawWidth, uint32_t rawHeight, uint32_t previewWidth,
                    uint32_t previewHeight,
                    const std::array<VkImageView, rawrcam::imaging::kRealtimeFramesInFlight>& rawCopyViews,
                    const std::array<VkImageView, rawrcam::imaging::kRealtimeFramesInFlight>& linearViews,
                    const std::array<VkImageView, rawrcam::imaging::kRealtimeFramesInFlight>& tonemappedViews);
    void reset();

    void recordRawCopy(VkCommandBuffer command, uint32_t slotIndex, VkImageView sourceView);
    void recordRawVisualization(VkCommandBuffer command, uint32_t slotIndex, VkImageView sourceView, bool sampledSource,
                                const std::array<float, 4>& blackLevels, float whiteLevel);
    void recordLinearPattern(VkCommandBuffer command, uint32_t slotIndex) const;
    void recordTonePattern(VkCommandBuffer command, uint32_t slotIndex) const;

   private:
    struct RawCopyPipeline {
        VkDescriptorSetLayout dsl = VK_NULL_HANDLE;
        VkPipelineLayout layout = VK_NULL_HANDLE;
        VkPipeline pipeline = VK_NULL_HANDLE;
        VkDescriptorPool pool = VK_NULL_HANDLE;
        std::array<VkDescriptorSet, rawrcam::imaging::kRealtimeFramesInFlight> sets{};
    };
    struct RawVizPipeline {
        VkDescriptorSetLayout dsl = VK_NULL_HANDLE;
        VkPipelineLayout layout = VK_NULL_HANDLE;
        VkPipeline pipeline = VK_NULL_HANDLE;
        VkDescriptorPool pool = VK_NULL_HANDLE;
        std::array<VkDescriptorSet, rawrcam::imaging::kRealtimeFramesInFlight> sets{};
        VkDescriptorType sourceType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    };
    struct PatternPipeline {
        VkDescriptorSetLayout dsl = VK_NULL_HANDLE;
        VkPipelineLayout layout = VK_NULL_HANDLE;
        VkPipeline pipeline = VK_NULL_HANDLE;
        VkDescriptorPool pool = VK_NULL_HANDLE;
        std::array<VkDescriptorSet, rawrcam::imaging::kRealtimeFramesInFlight> sets{};
    };
    struct RawVizPush {
        uint32_t width;
        uint32_t height;
        float black;
        float white;
    };
    struct PatternPush {
        uint32_t width;
        uint32_t height;
    };

    VkShaderModule createShaderModule(const unsigned char* bytes, size_t size) const;
    void createRawCopyPipeline();
    RawVizPipeline createRawVizPipeline(const unsigned char* bytes, size_t size, bool sampled);
    PatternPipeline createPatternPipeline(
        const unsigned char* bytes, size_t size,
        const std::array<VkImageView, rawrcam::imaging::kRealtimeFramesInFlight>& targetViews);
    void destroyRawVizPipeline(RawVizPipeline& pipeline);
    void destroyPatternPipeline(PatternPipeline& pipeline);
    void recordPattern(VkCommandBuffer command, const PatternPipeline& pipeline, uint32_t slotIndex) const;

    Diagnostic diagnostic_;
    VkDevice device_ = VK_NULL_HANDLE;
    uint32_t rawWidth_ = 0;
    uint32_t rawHeight_ = 0;
    uint32_t previewWidth_ = 0;
    uint32_t previewHeight_ = 0;
    std::array<VkImageView, rawrcam::imaging::kRealtimeFramesInFlight> rawCopyViews_{};
    std::array<VkImageView, rawrcam::imaging::kRealtimeFramesInFlight> tonemappedViews_{};
    RawCopyPipeline rawCopy_{};
    RawVizPipeline rawStorageViz_{};
    RawVizPipeline rawSampledViz_{};
    PatternPipeline linearPattern_{};
    PatternPipeline tonePattern_{};
    VkSampler nearestSampler_ = VK_NULL_HANDLE;
};

}  // namespace rawrcam::diagnostics
