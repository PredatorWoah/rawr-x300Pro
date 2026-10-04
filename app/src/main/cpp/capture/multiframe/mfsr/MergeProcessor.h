#pragma once
#include "capture/multiframe/FrozenBurst.h"
#include "capture/multiframe/MultiframeTuning.h"
#include "vulkan/VulkanContext.h"
namespace rawrcam::capture::multiframe {
class MergeProcessor final {
   public:
    static rawr::raw_gpu_pipeline::BurstRunResult run(
        vulkan::VulkanContext& context, rawr::raw_gpu_pipeline::AndroidBurstCoordinator& coordinator,
        FrozenBurst& burst, uint32_t width, uint32_t height, uint32_t cfa, const MultiframeTuning& tuning,
        bool dumpRzslRequested, const rawr::raw_gpu_pipeline::AndroidBurstCoordinator::Submit& queueSubmit,
        const rawr::raw_gpu_pipeline::AndroidBurstCoordinator::Submit& mergeSubmit);
};
}  // namespace rawrcam::capture::multiframe
