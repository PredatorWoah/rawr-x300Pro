#pragma once

#include <android/native_window.h>


#include "presentation/SwapchainRenderer.h"
#include "vulkan/VulkanContext.h"

namespace rawrcam::renderer {

// Owns only the editor display. Image processing remains in RendererEngine;
// the camera swapchain presenter is reused for aspect fit and EXIF mapping.
class RendererSurface final {
   public:
    explicit RendererSurface(const vulkan::VulkanContext& vulkan);
    ~RendererSurface();
    RendererSurface(const RendererSurface&) = delete;
    RendererSurface& operator=(const RendererSurface&) = delete;

    // Takes ownership of one ANativeWindow reference; nullptr detaches.
    void attach(ANativeWindow* window);
    bool attached() const noexcept { return window_ != nullptr; }
    void present(VkImageView output, uint32_t width, uint32_t height, uint32_t exifOrientation);

   private:
    void detach() noexcept;
    const vulkan::VulkanContext& vulkan_;
    presentation::SwapchainRenderer swapchain_;
    ANativeWindow* window_ = nullptr;
    VkSurfaceKHR surface_ = VK_NULL_HANDLE;
    VkCommandPool pool_ = VK_NULL_HANDLE;
    VkCommandBuffer command_ = VK_NULL_HANDLE;
    VkFence fence_ = VK_NULL_HANDLE;
    VkSemaphore available_ = VK_NULL_HANDLE;
    VkSemaphore rendered_ = VK_NULL_HANDLE;
};

}  // namespace rawrcam::renderer
