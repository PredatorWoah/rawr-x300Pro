#pragma once

#include <android/native_window.h>
#include <vulkan/vulkan.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include "imaging/FrameLimits.h"
#include "monitoring/MonitoringState.h"
#include "presentation/PresentRecorder.h"
#include "vulkan/VulkanContext.h"

namespace rawrcam::presentation {

class SwapchainRenderer final {
   public:
    using Diagnostic = std::function<void(const std::string&)>;

    explicit SwapchainRenderer(Diagnostic diagnostic);
    ~SwapchainRenderer();

    SwapchainRenderer(const SwapchainRenderer&) = delete;
    SwapchainRenderer& operator=(const SwapchainRenderer&) = delete;

    void initialize(const vulkan::VulkanContext& context);
    void createSwapchain(VkSurfaceKHR surface, ANativeWindow* window);
    void destroySwapchain();

    void setFrameSources(
        const std::array<VkImageView, rawrcam::imaging::kRealtimeFramesInFlight>& tonemappedViews,
        const std::array<VkImageView, rawrcam::imaging::kRealtimeFramesInFlight>& linearViews,
        const std::array<VkImageView, rawrcam::imaging::kRealtimeFramesInFlight>& overlayViews,
        const std::array<std::array<VkImageView, 3>, rawrcam::imaging::kRealtimeFramesInFlight>& scopeViews,
        float scopeSourceAspect);
    void setVideoFrameSources(const std::array<VkImageView, rawrcam::imaging::kRealtimeFramesInFlight>& views);
    void setScopePresentationState(const rawrcam::monitoring::ScopePresentationState& state) noexcept {
        scopeState_ = state;
    }
    void clearFrameSources() noexcept;

    [[nodiscard]] bool ready() const noexcept { return swapchain_ != VK_NULL_HANDLE; }
    [[nodiscard]] VkExtent2D extent() const noexcept { return swapExtent_; }

    VkResult acquireNextImage(VkSemaphore imageAvailable, uint32_t* imageIndex) const;
    VkResult present(VkQueue queue, VkSemaphore renderFinished, uint32_t imageIndex) const;

    void record(VkCommandBuffer command, uint32_t frameSlot, uint32_t imageIndex, uint32_t previewWidth,
                uint32_t previewHeight, int sensorOrientationDegrees, int displayRotationDegrees,
                uint32_t diagnosticMode, bool monitoringOverlayEnabled, uint32_t exifOrientation = 0,
                bool videoSource = false, uint32_t cropX = 0, uint32_t cropY = 0, uint32_t cropW = 0,
                uint32_t cropH = 0);

    void resetLogging() noexcept { recorder_.resetLogging(); }

   private:
    struct PresentPush {
        uint32_t quarterTurns;
        float srcAspect;
        float dstAspect;
        uint32_t diagnosticMode;
        uint32_t overlayEnabled;
        float cornerRadius;
        uint32_t exifOrientation;
        uint32_t cropEnabled;
        float cropX0;
        float cropY0;
        float cropX1;
        float cropY1;
    };

    void createPipeline();
    void createFramebuffers();
    void rebuildDescriptors();
    VkShaderModule createShaderModule(const unsigned char* bytes, size_t size) const;

    Diagnostic diagnostic_;
    PresentRecorder recorder_;
    const vulkan::VulkanContext* context_ = nullptr;
    VkPhysicalDevice physicalDevice_ = VK_NULL_HANDLE;
    VkDevice device_ = VK_NULL_HANDLE;
    VkSwapchainKHR swapchain_ = VK_NULL_HANDLE;
    VkSurfaceFormatKHR surfaceFormat_{};
    VkExtent2D swapExtent_{};
    std::vector<VkImage> swapImages_;
    std::vector<VkImageView> swapViews_;
    std::vector<VkFramebuffer> framebuffers_;
    std::vector<bool> swapInitialized_;
    VkRenderPass renderPass_ = VK_NULL_HANDLE;
    VkDescriptorSetLayout descriptorSetLayout_ = VK_NULL_HANDLE;
    VkPipelineLayout pipelineLayout_ = VK_NULL_HANDLE;
    VkPipeline pipeline_ = VK_NULL_HANDLE;
    VkSampler sampler_ = VK_NULL_HANDLE;
    VkDescriptorPool descriptorPool_ = VK_NULL_HANDLE;
    std::array<VkDescriptorSet, rawrcam::imaging::kRealtimeFramesInFlight> tonemappedSets_{};
    std::array<VkDescriptorSet, rawrcam::imaging::kRealtimeFramesInFlight> linearSets_{};
    std::array<VkDescriptorSet, rawrcam::imaging::kRealtimeFramesInFlight> videoSets_{};
    std::array<std::array<VkDescriptorSet, 3>, rawrcam::imaging::kRealtimeFramesInFlight> scopeSets_{};
    std::array<VkImageView, rawrcam::imaging::kRealtimeFramesInFlight> tonemappedViews_{};
    std::array<VkImageView, rawrcam::imaging::kRealtimeFramesInFlight> linearViews_{};
    std::array<VkImageView, rawrcam::imaging::kRealtimeFramesInFlight> videoViews_{};
    std::array<VkImageView, rawrcam::imaging::kRealtimeFramesInFlight> overlayViews_{};
    std::array<std::array<VkImageView, 3>, rawrcam::imaging::kRealtimeFramesInFlight> scopeViews_{};
    rawrcam::monitoring::ScopePresentationState scopeState_{};
    float scopeSourceAspect_ = 1.0f;
    bool hasFrameSources_ = false;
    bool hasVideoFrameSources_ = false;
};

}  // namespace rawrcam::presentation
