#pragma once
#include <functional>
#include <string>

#include "vulkan/VulkanContext.h"
struct AAssetManager;
namespace rawrcam::diagnostics::replay {
class HighlightReplayRunner final {
   public:
    HighlightReplayRunner(vulkan::VulkanContext& context, std::mutex& queueMutex, std::string filesDir,
                          AAssetManager* assets, std::function<void(const std::string&)> diagnostic,
                          std::function<std::string()> prepare)
        : vulkanContext_(context),
          queueSubmitMutex_(queueMutex),
          filesDir_(std::move(filesDir)),
          replayAssetManager_(assets),
          diagnostic_(std::move(diagnostic)),
          prepare_(std::move(prepare)) {}
    std::string run(const std::string& inputPath);

   private:
    void appendDiagnostic(const std::string& line) const {
        if (diagnostic_) diagnostic_(line);
    }
    vulkan::VulkanContext& vulkanContext_;
    std::mutex& queueSubmitMutex_;
    std::string filesDir_;
    AAssetManager* replayAssetManager_;
    std::function<void(const std::string&)> diagnostic_;
    std::function<std::string()> prepare_;
};
}  // namespace rawrcam::diagnostics::replay
