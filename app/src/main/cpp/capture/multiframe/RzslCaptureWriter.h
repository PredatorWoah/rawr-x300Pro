#pragma once
#include <rawr/raw_gpu_pipeline/AndroidBurstCoordinator.h>

#include <string>
#include <vector>

#include "metadata/FrameMetadataSnapshot.h"
#include "vulkan/VulkanContext.h"
namespace rawrcam::capture::multiframe {
void writePostShutterRzsl(rawrcam::vulkan::VulkanContext& context, std::mutex& queueSubmitMutex,
                          const std::string& filesDir, std::uint32_t cfa,
                          const std::vector<rawr::raw_gpu_pipeline::BurstFrame>& sourceFrames,
                          const std::vector<rawrcam::metadata::FrameMetadataSnapshot>& metadata,
                          const std::vector<float>& sharpnessScores, const std::string& replayMetadata);
}
