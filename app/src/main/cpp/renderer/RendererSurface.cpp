#include "RendererSurface.h"

#include <array>
#include <chrono>
#include <stdexcept>
#include <thread>

#include "imaging/FrameLimits.h"
#include "vulkan/VulkanDispatch.h"

namespace rawrcam::renderer {
namespace {
void vkOk(VkResult result, const char* operation) {
    if (result != VK_SUCCESS) throw std::runtime_error(std::string(operation) + ": " + std::to_string(result));
}
}  // namespace

RendererSurface::RendererSurface(const vulkan::VulkanContext& vulkan)
    : vulkan_(vulkan), swapchain_([](const std::string&) {}) {
    swapchain_.initialize(vulkan);
    try {
        VkCommandPoolCreateInfo pool{};
        pool.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
        pool.queueFamilyIndex = vulkan.queueFamily();
        pool.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
        vkOk(vkCreateCommandPool(vulkan.device(), &pool, nullptr, &pool_), "renderer surface command pool");
        VkCommandBufferAllocateInfo buffer{};
        buffer.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        buffer.commandPool = pool_;
        buffer.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        buffer.commandBufferCount = 1;
        vkOk(vkAllocateCommandBuffers(vulkan.device(), &buffer, &command_), "renderer surface command buffer");
        VkFenceCreateInfo fence{};
        fence.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
        vkOk(vkCreateFence(vulkan.device(), &fence, nullptr, &fence_), "renderer surface fence");
        VkSemaphoreCreateInfo semaphore{};
        semaphore.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
        vkOk(vkCreateSemaphore(vulkan.device(), &semaphore, nullptr, &available_),
             "renderer surface acquire semaphore");
        vkOk(vkCreateSemaphore(vulkan.device(), &semaphore, nullptr, &rendered_), "renderer surface render semaphore");
    } catch (...) {
        const auto device = vulkan_.device();
        if (rendered_) vkDestroySemaphore(device, rendered_, nullptr);
        if (available_) vkDestroySemaphore(device, available_, nullptr);
        if (fence_) vkDestroyFence(device, fence_, nullptr);
        if (pool_) vkDestroyCommandPool(device, pool_, nullptr);
        throw;
    }
}

RendererSurface::~RendererSurface() {
    detach();
    const auto device = vulkan_.device();
    if (rendered_) vkDestroySemaphore(device, rendered_, nullptr);
    if (available_) vkDestroySemaphore(device, available_, nullptr);
    if (fence_) vkDestroyFence(device, fence_, nullptr);
    if (pool_) vkDestroyCommandPool(device, pool_, nullptr);
}

void RendererSurface::detach() noexcept {
    if (!window_) return;
    vkQueueWaitIdle(vulkan_.queue());
    swapchain_.clearFrameSources();
    swapchain_.destroySwapchain();
    if (surface_) vkDestroySurfaceKHR(vulkan_.instance(), surface_, nullptr);
    surface_ = VK_NULL_HANDLE;
    ANativeWindow_release(window_);
    window_ = nullptr;
}

void RendererSurface::attach(ANativeWindow* window) {
    detach();
    if (!window) return;
    window_ = window;
    try {
        VkAndroidSurfaceCreateInfoKHR info{};
        info.sType = VK_STRUCTURE_TYPE_ANDROID_SURFACE_CREATE_INFO_KHR;
        info.window = window_;
        vkOk(vkCreateAndroidSurfaceKHR(vulkan_.instance(), &info, nullptr, &surface_), "renderer Android surface");
        VkBool32 supported = VK_FALSE;
        vkOk(
            vkGetPhysicalDeviceSurfaceSupportKHR(vulkan_.physicalDevice(), vulkan_.queueFamily(), surface_, &supported),
            "renderer present support");
        if (!supported) throw std::runtime_error("Renderer GPU queue cannot present");
        swapchain_.createSwapchain(surface_, window_);
    } catch (...) {
        detach();
        throw;
    }
}

void RendererSurface::present(VkImageView output, uint32_t width, uint32_t height, uint32_t exifOrientation) {
    if (!attached()) return;
    std::array<VkImageView, imaging::kRealtimeFramesInFlight> views{};
    views.fill(output);
    std::array<std::array<VkImageView, 3>, imaging::kRealtimeFramesInFlight> scopes{};
    for (auto& slot : scopes) slot.fill(output);
    swapchain_.setFrameSources(views, views, views, scopes, float(width) / height);

    uint32_t index = 0;
    VkResult acquired = VK_NOT_READY;
    for (int attempt = 0; attempt < 100 && (acquired == VK_NOT_READY || acquired == VK_TIMEOUT); ++attempt) {
        acquired = swapchain_.acquireNextImage(available_, &index);
        if (acquired == VK_NOT_READY || acquired == VK_TIMEOUT)
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    if (acquired == VK_ERROR_OUT_OF_DATE_KHR) {
        swapchain_.createSwapchain(surface_, window_);
        return;
    }
    if (acquired != VK_SUCCESS && acquired != VK_SUBOPTIMAL_KHR) vkOk(acquired, "renderer acquire image");
    vkOk(vkResetCommandBuffer(command_, 0), "renderer reset present commands");
    VkCommandBufferBeginInfo begin{};
    begin.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    vkOk(vkBeginCommandBuffer(command_, &begin), "renderer begin present commands");
    swapchain_.record(command_, 0, index, width, height, 0, 0, 0, false, exifOrientation);
    vkOk(vkEndCommandBuffer(command_), "renderer end present commands");

    vkOk(vkResetFences(vulkan_.device(), 1, &fence_), "renderer reset present fence");
    const VkPipelineStageFlags waitStage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    VkSubmitInfo submit{};
    submit.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submit.waitSemaphoreCount = 1;
    submit.pWaitSemaphores = &available_;
    submit.pWaitDstStageMask = &waitStage;
    submit.commandBufferCount = 1;
    submit.pCommandBuffers = &command_;
    submit.signalSemaphoreCount = 1;
    submit.pSignalSemaphores = &rendered_;
    vkOk(vkQueueSubmit(vulkan_.queue(), 1, &submit, fence_), "renderer submit present");
    const VkResult result = swapchain_.present(vulkan_.queue(), rendered_, index);
    if (result != VK_SUCCESS && result != VK_SUBOPTIMAL_KHR && result != VK_ERROR_OUT_OF_DATE_KHR)
        vkOk(result, "renderer present");
    vkOk(vkWaitForFences(vulkan_.device(), 1, &fence_, VK_TRUE, UINT64_MAX), "renderer present fence");
    // The next render can replace both semaphores only after presentation has
    // consumed its wait. This single-frame editor lane favors correctness.
    vkOk(vkQueueWaitIdle(vulkan_.queue()), "renderer present idle");
    if (result == VK_ERROR_OUT_OF_DATE_KHR) swapchain_.createSwapchain(surface_, window_);
}

}  // namespace rawrcam::renderer
