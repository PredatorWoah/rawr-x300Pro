#pragma once

#include <rawr/raw_gpu_pipeline/AndroidBurstCoordinator.h>
#include <vulkan/vulkan.h>

#include "diagnostics/logging/RuntimeTraceRecorder.h"

namespace rawrcam::capture::multiframe {

// Full 8-stage mapping (kept from the legacy path); the newer path only
// mapped ChunkBegin/End. All bursts now report the same trace stages.
inline rawr::raw_gpu_pipeline::AndroidBurstCoordinator::StageSink makeTraceStageSink() {
    return [](rawr::raw_gpu_pipeline::AndroidBurstCoordinator::Stage stage, VkDeviceSize imageBytes,
              VkDeviceSize bufferBytes) {
        using B = rawr::raw_gpu_pipeline::AndroidBurstCoordinator;
        using R = rawrcam::diagnostics::RuntimeTraceStage;
        auto& trace = rawrcam::diagnostics::RuntimeTraceRecorder::instance();
        if (stage == B::Stage::ChunkBegin || stage == B::Stage::ChunkEnd) {
            trace.record(stage == B::Stage::ChunkBegin ? R::MultiframeChunkBegin : R::MultiframeChunkEnd, 0, 0, -1, 0,
                         0, static_cast<uint32_t>(imageBytes), static_cast<int64_t>(bufferBytes));
            return;
        }
        R traceStage = R::MultiframeFail;
        switch (stage) {
            case B::Stage::ArenaInitializeBegin:
                traceStage = R::MultiframeArenaBegin;
                break;
            case B::Stage::ArenaInitializeEnd:
                traceStage = R::MultiframeArenaEnd;
                break;
            case B::Stage::RecordBegin:
                traceStage = R::MultiframeRecordBegin;
                break;
            case B::Stage::RecordEnd:
                traceStage = R::MultiframeRecordEnd;
                break;
            case B::Stage::SubmitBegin:
                traceStage = R::MultiframeSubmitBegin;
                break;
            case B::Stage::SubmitEnd:
                traceStage = R::MultiframeSubmitEnd;
                break;
            case B::Stage::FenceWaitBegin:
                traceStage = R::MultiframeFenceBegin;
                break;
            case B::Stage::FenceWaitEnd:
                traceStage = R::MultiframeFenceEnd;
                break;
            case B::Stage::ChunkBegin:
            case B::Stage::ChunkEnd:
                break;
        }
        const uint32_t bufferMiBX1000 =
            static_cast<uint32_t>(std::min<VkDeviceSize>(UINT32_MAX, (bufferBytes * 1000ull) / (1024ull * 1024ull)));
        const int64_t imageMiBX1000 = static_cast<int64_t>((imageBytes * 1000ull) / (1024ull * 1024ull));
        trace.record(traceStage, 0, 0, -1, 0, 0, bufferMiBX1000, imageMiBX1000);
    };
}

// Exception-safe binary semaphore for the queue-0 -> multiframe-queue bridge.
class ScopedSemaphore {
   public:
    ScopedSemaphore(VkDevice device, bool armed) : device_(device) {
        if (!armed) return;
        VkSemaphoreCreateInfo sci{};
        sci.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
        if (vkCreateSemaphore(device_, &sci, nullptr, &sem_) != VK_SUCCESS) {
            throw std::runtime_error("multiframe bridge semaphore failed");
        }
    }
    ~ScopedSemaphore() {
        if (sem_ != VK_NULL_HANDLE) vkDestroySemaphore(device_, sem_, nullptr);
    }
    ScopedSemaphore(const ScopedSemaphore&) = delete;
    ScopedSemaphore& operator=(const ScopedSemaphore&) = delete;
    VkSemaphore get() const noexcept { return sem_; }

   private:
    VkDevice device_ = VK_NULL_HANDLE;
    VkSemaphore sem_ = VK_NULL_HANDLE;
};

}  // namespace rawrcam::capture::multiframe
