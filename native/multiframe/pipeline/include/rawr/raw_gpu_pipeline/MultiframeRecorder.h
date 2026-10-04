#pragma once
#include <rawr/raw_alignment_gpu/RawAlignmentGpu.h>
#include <rawr/raw_gpu_pipeline/ResourceArena.h>
#include <rawr/raw_gpu_pipeline/VulkanExecutor.h>
#include <rawr/raw_merge_wronski_gpu/RawMergeWronskiGpu.h>

#include <array>
#include <cstdint>

namespace rawr::raw_gpu_pipeline {

struct RawNormalization {
    std::array<float, 4> blackByPhase{{0, 0, 0, 0}};
    float whiteLevel = 1.0f;
};

struct MultiframeFrameParameters {
    RawNormalization normalization{};
    std::array<float, 3> whiteBalance{{1.0f, 1.0f, 1.0f}};
};

// Records the multiframe merge stages against ResourceArena. RAW16 inputs are
// borrowed directly from zsl_ring::RawImageRing; no live-path codec/decode is involved.
class MultiframeRecorder final {
   public:
    MultiframeRecorder(ResourceArena& arena, VulkanExecutor& executor, rawr::raw_alignment_gpu::Config alignment,
                      rawr::raw_merge_wronski_gpu::Config merge, std::uint32_t reconstructionStripeHeight);

    void recordUploadNoiseCurves(VkCommandBuffer command, const float* stdCurveRgb3003,
                                 const float* diffCurveRgb3003) const;
    // Hot-pixel list (ivec2 per entry) concealed after every normalize.
    void setHotPixels(VkBuffer buffer, std::uint32_t count) noexcept {
        hotPixelBuffer_ = buffer;
        hotPixelCount_ = count;
    }

    // Submission-safe stage boundaries. Each method leaves all produced
    // arena state persistent so the next method may execute in a later command buffer.
    void recordReferencePrepare(VkCommandBuffer command, VkImageView rawU16, const MultiframeFrameParameters& frame,
                                const TraceTarget* trace = nullptr) const;
    void recordReferenceStats(VkCommandBuffer command, const MultiframeFrameParameters& frame,
                              const TraceTarget* trace = nullptr) const;
    void recordCompanionPrepare(VkCommandBuffer command, VkImageView rawU16, const MultiframeFrameParameters& frame,
                                const TraceTarget* trace = nullptr) const;
    // Records a single coarse-to-fine alignment level (0..3); callers record
    // levels 0..3 in order. Splitting levels across submissions bounds the
    // worst-case GPU stall that viewfinder work queued behind a companion's
    // alignment must absorb.
    void recordCompanionAlignmentLevel(VkCommandBuffer command, std::uint32_t level,
                                       const TraceTarget* trace = nullptr) const;
    void recordCompanionStats(VkCommandBuffer command, const MultiframeFrameParameters& frame,
                              const TraceTarget* trace = nullptr) const;
    void recordCompanionRobustness(VkCommandBuffer command, const TraceTarget* trace = nullptr) const;
    void recordCompanionAccumulate(VkCommandBuffer command, const TraceTarget* trace = nullptr) const;
    void recordFinalize(VkCommandBuffer command, std::uint32_t companionCount,
                        const TraceTarget* trace = nullptr) const;

   private:
    ResourceArena& arena_;
    VulkanExecutor& executor_;
    rawr::raw_alignment_gpu::Config alignment_{};
    rawr::raw_merge_wronski_gpu::Config merge_{};
    std::uint32_t stripeHeight_ = 0;
    VkBuffer hotPixelBuffer_ = VK_NULL_HANDLE;
    std::uint32_t hotPixelCount_ = 0;

    void recordNormalize(VkCommandBuffer, bool reference, VkImageView rawU16, const MultiframeFrameParameters&,
                         const TraceTarget*) const;
    void recordAlignmentInput(VkCommandBuffer, bool reference, const TraceTarget*) const;
    void recordPyramid(VkCommandBuffer, bool reference, const TraceTarget*) const;
    void recordAlignmentLevel(VkCommandBuffer, std::uint32_t level, const TraceTarget*) const;
    void recordKernelAndStats(VkCommandBuffer, bool reference, const MultiframeFrameParameters&,
                              const TraceTarget*) const;
};

}  // namespace rawr::raw_gpu_pipeline
