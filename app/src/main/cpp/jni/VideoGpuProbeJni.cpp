#include <android/hardware_buffer_jni.h>
#include <android/log.h>
#include <android/native_window_jni.h>
#include <jni.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <vector>

#include "vulkan/VulkanContext.h"
#include "vulkan/VulkanDispatch.h"

extern "C" JNIEXPORT jlongArray JNICALL
Java_com_rawr_camera_video_VideoGpuProbe_inspectHardwareBuffer(JNIEnv* env, jobject, jobject javaBuffer) {
    std::array<jlong, 9> out{};
    out.fill(-1);
    AHardwareBuffer* buffer = AHardwareBuffer_fromHardwareBuffer(env, javaBuffer);
    if (!buffer) return nullptr;
    try {
        rawrcam::vulkan::dispatch::configure("", "");
        rawrcam::vulkan::VulkanContext context;
        context.createInstance();
        context.createDeviceForSurface(VK_NULL_HANDLE);
        VkAndroidHardwareBufferFormatPropertiesANDROID format{};
        format.sType = VK_STRUCTURE_TYPE_ANDROID_HARDWARE_BUFFER_FORMAT_PROPERTIES_ANDROID;
        VkAndroidHardwareBufferPropertiesANDROID properties{};
        properties.sType = VK_STRUCTURE_TYPE_ANDROID_HARDWARE_BUFFER_PROPERTIES_ANDROID;
        properties.pNext = &format;
        if (context.getAhbProperties()(context.device(), buffer, &properties) == VK_SUCCESS) {
            out[0] = static_cast<jlong>(format.format);
            out[1] = static_cast<jlong>(format.externalFormat);
            out[2] = static_cast<jlong>(format.formatFeatures);
            const std::array<VkImageUsageFlags, 2> usages{VK_IMAGE_USAGE_TRANSFER_DST_BIT, VK_IMAGE_USAGE_STORAGE_BIT};
            for (std::size_t index = 0; index < usages.size(); ++index) {
                VkPhysicalDeviceExternalImageFormatInfo externalInfo{};
                externalInfo.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_EXTERNAL_IMAGE_FORMAT_INFO;
                externalInfo.handleType = VK_EXTERNAL_MEMORY_HANDLE_TYPE_ANDROID_HARDWARE_BUFFER_BIT_ANDROID;
                VkPhysicalDeviceImageFormatInfo2 imageInfo{};
                imageInfo.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_IMAGE_FORMAT_INFO_2;
                imageInfo.pNext = &externalInfo;
                imageInfo.format = format.format;
                imageInfo.type = VK_IMAGE_TYPE_2D;
                imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
                imageInfo.usage = usages[index];
                VkAndroidHardwareBufferUsageANDROID requiredUsage{};
                requiredUsage.sType = VK_STRUCTURE_TYPE_ANDROID_HARDWARE_BUFFER_USAGE_ANDROID;
                VkExternalImageFormatProperties externalProperties{};
                externalProperties.sType = VK_STRUCTURE_TYPE_EXTERNAL_IMAGE_FORMAT_PROPERTIES;
                externalProperties.pNext = &requiredUsage;
                VkImageFormatProperties2 result{};
                result.sType = VK_STRUCTURE_TYPE_IMAGE_FORMAT_PROPERTIES_2;
                result.pNext = &externalProperties;
                out[3 + index] =
                    vkGetPhysicalDeviceImageFormatProperties2(context.physicalDevice(), &imageInfo, &result);
                out[5 + index] = static_cast<jlong>(requiredUsage.androidHardwareBufferUsage);
                out[7 + index] = static_cast<jlong>(externalProperties.externalMemoryProperties.externalMemoryFeatures);
            }
        }
    } catch (...) {
        // -1 values report an unsupported query without crashing the camera.
    }
    jlongArray result = env->NewLongArray(static_cast<jsize>(out.size()));
    if (result) env->SetLongArrayRegion(result, 0, static_cast<jsize>(out.size()), out.data());
    return result;
}

extern "C" JNIEXPORT jlongArray JNICALL
Java_com_rawr_camera_video_VideoGpuProbe_inspectEncoderSurface(JNIEnv* env, jobject, jobject javaSurface) {
    std::vector<jlong> out;
    ANativeWindow* window = ANativeWindow_fromSurface(env, javaSurface);
    if (!window) return nullptr;
    try {
        rawrcam::vulkan::dispatch::configure("", "");
        rawrcam::vulkan::VulkanContext context;
        context.createInstance();
        VkAndroidSurfaceCreateInfoKHR surfaceInfo{};
        surfaceInfo.sType = VK_STRUCTURE_TYPE_ANDROID_SURFACE_CREATE_INFO_KHR;
        surfaceInfo.window = window;
        VkSurfaceKHR surface = VK_NULL_HANDLE;
        if (vkCreateAndroidSurfaceKHR(context.instance(), &surfaceInfo, nullptr, &surface) == VK_SUCCESS) {
            try {
                context.createDeviceForSurface(surface);
                VkSurfaceCapabilitiesKHR caps{};
                if (vkGetPhysicalDeviceSurfaceCapabilitiesKHR(context.physicalDevice(), surface, &caps) == VK_SUCCESS) {
                    out.push_back(static_cast<jlong>(caps.supportedUsageFlags));
                    out.push_back(static_cast<jlong>(caps.minImageCount));
                    out.push_back(static_cast<jlong>(caps.maxImageCount));
                    uint32_t count = 0;
                    if (vkGetPhysicalDeviceSurfaceFormatsKHR(context.physicalDevice(), surface, &count, nullptr) ==
                        VK_SUCCESS) {
                        std::vector<VkSurfaceFormatKHR> formats(count);
                        if (vkGetPhysicalDeviceSurfaceFormatsKHR(context.physicalDevice(), surface, &count,
                                                                 formats.data()) == VK_SUCCESS) {
                            for (const auto& format : formats) {
                                out.push_back(static_cast<jlong>(format.format));
                                out.push_back(static_cast<jlong>(format.colorSpace));
                            }
                        }
                    }
                }
            } catch (...) {
                out.push_back(-1);
            }
            context.destroyDevice();
            vkDestroySurfaceKHR(context.instance(), surface, nullptr);
        }
    } catch (...) {
        out.push_back(-2);
    }
    ANativeWindow_release(window);
    jlongArray result = env->NewLongArray(static_cast<jsize>(out.size()));
    if (result && !out.empty()) env->SetLongArrayRegion(result, 0, static_cast<jsize>(out.size()), out.data());
    return result;
}

extern "C" JNIEXPORT jint JNICALL Java_com_rawr_camera_video_VideoGpuProbe_renderHalfFloatFrames(JNIEnv* env, jobject,
                                                                                                 jobject javaSurface) {
    ANativeWindow* window = ANativeWindow_fromSurface(env, javaSurface);
    if (!window) return -1;
    rawrcam::vulkan::VulkanContext context;
    VkSurfaceKHR surface = VK_NULL_HANDLE;
    VkSwapchainKHR swapchain = VK_NULL_HANDLE;
    VkCommandPool pool = VK_NULL_HANDLE;
    VkSemaphore available = VK_NULL_HANDLE;
    VkSemaphore rendered = VK_NULL_HANDLE;
    jint frames = 0;
    auto check = [](VkResult result, const char* operation) {
        if (result != VK_SUCCESS)
            throw std::runtime_error(std::string(operation) + " result=" + std::to_string(result));
    };
    try {
        rawrcam::vulkan::dispatch::configure("", "");
        context.createInstance();
        VkAndroidSurfaceCreateInfoKHR surfaceInfo{};
        surfaceInfo.sType = VK_STRUCTURE_TYPE_ANDROID_SURFACE_CREATE_INFO_KHR;
        surfaceInfo.window = window;
        check(vkCreateAndroidSurfaceKHR(context.instance(), &surfaceInfo, nullptr, &surface), "createSurface");
        context.createDeviceForSurface(surface);
        VkSurfaceCapabilitiesKHR caps{};
        check(vkGetPhysicalDeviceSurfaceCapabilitiesKHR(context.physicalDevice(), surface, &caps), "surfaceCaps");
        uint32_t formatCount = 0;
        check(vkGetPhysicalDeviceSurfaceFormatsKHR(context.physicalDevice(), surface, &formatCount, nullptr),
              "formatCount");
        std::vector<VkSurfaceFormatKHR> formats(formatCount);
        check(vkGetPhysicalDeviceSurfaceFormatsKHR(context.physicalDevice(), surface, &formatCount, formats.data()),
              "formats");
        const auto selected = std::find_if(formats.begin(), formats.end(), [](const VkSurfaceFormatKHR& format) {
            return format.format == VK_FORMAT_R16G16B16A16_SFLOAT;
        });
        constexpr VkImageUsageFlags kVideoUsage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_STORAGE_BIT;
        if (selected == formats.end() || (caps.supportedUsageFlags & kVideoUsage) != kVideoUsage)
            throw std::runtime_error("RGBA16F storage swapchain unsupported");
        VkSwapchainCreateInfoKHR swapInfo{};
        swapInfo.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
        swapInfo.surface = surface;
        swapInfo.minImageCount = caps.minImageCount;
        swapInfo.imageFormat = selected->format;
        swapInfo.imageColorSpace = selected->colorSpace;
        swapInfo.imageExtent = caps.currentExtent.width == UINT32_MAX ? VkExtent2D{1920, 1080} : caps.currentExtent;
        swapInfo.imageArrayLayers = 1;
        swapInfo.imageUsage = kVideoUsage;
        swapInfo.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
        swapInfo.preTransform = caps.currentTransform;
        swapInfo.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
        swapInfo.presentMode = VK_PRESENT_MODE_FIFO_KHR;
        swapInfo.clipped = VK_TRUE;
        check(vkCreateSwapchainKHR(context.device(), &swapInfo, nullptr, &swapchain), "createSwapchain");
        uint32_t imageCount = 0;
        check(vkGetSwapchainImagesKHR(context.device(), swapchain, &imageCount, nullptr), "imageCount");
        std::vector<VkImage> images(imageCount);
        check(vkGetSwapchainImagesKHR(context.device(), swapchain, &imageCount, images.data()), "images");
        std::vector<bool> initialized(imageCount, false);
        VkCommandPoolCreateInfo poolInfo{};
        poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
        poolInfo.queueFamilyIndex = context.queueFamily();
        poolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
        check(vkCreateCommandPool(context.device(), &poolInfo, nullptr, &pool), "commandPool");
        VkCommandBuffer command = VK_NULL_HANDLE;
        VkCommandBufferAllocateInfo alloc{};
        alloc.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        alloc.commandPool = pool;
        alloc.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        alloc.commandBufferCount = 1;
        check(vkAllocateCommandBuffers(context.device(), &alloc, &command), "commandBuffer");
        VkSemaphoreCreateInfo semInfo{};
        semInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
        check(vkCreateSemaphore(context.device(), &semInfo, nullptr, &available), "availableSemaphore");
        check(vkCreateSemaphore(context.device(), &semInfo, nullptr, &rendered), "renderedSemaphore");
        for (int i = 0; i < 4; ++i) {
            uint32_t index = 0;
            check(vkAcquireNextImageKHR(context.device(), swapchain, 1'000'000'000u, available, VK_NULL_HANDLE, &index),
                  "acquire");
            check(vkResetCommandBuffer(command, 0), "resetCommand");
            VkCommandBufferBeginInfo begin{};
            begin.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
            begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
            check(vkBeginCommandBuffer(command, &begin), "beginCommand");
            VkImageMemoryBarrier toTransfer{};
            toTransfer.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
            toTransfer.oldLayout = initialized[index] ? VK_IMAGE_LAYOUT_PRESENT_SRC_KHR : VK_IMAGE_LAYOUT_UNDEFINED;
            toTransfer.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
            toTransfer.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            toTransfer.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            toTransfer.image = images[index];
            toTransfer.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
            toTransfer.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
            vkCmdPipelineBarrier(command, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0,
                                 nullptr, 0, nullptr, 1, &toTransfer);
            VkClearColorValue color{};
            const float level = static_cast<float>(512 + i) / 1023.0f;
            color.float32[0] = level;
            color.float32[1] = level;
            color.float32[2] = level;
            color.float32[3] = 1.0f;
            vkCmdClearColorImage(command, images[index], VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &color, 1,
                                 &toTransfer.subresourceRange);
            VkImageMemoryBarrier toPresent = toTransfer;
            toPresent.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
            toPresent.newLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
            toPresent.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
            toPresent.dstAccessMask = 0;
            vkCmdPipelineBarrier(command, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, 0, 0,
                                 nullptr, 0, nullptr, 1, &toPresent);
            check(vkEndCommandBuffer(command), "endCommand");
            VkPipelineStageFlags waitStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
            VkSubmitInfo submit{};
            submit.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
            submit.waitSemaphoreCount = 1;
            submit.pWaitSemaphores = &available;
            submit.pWaitDstStageMask = &waitStage;
            submit.commandBufferCount = 1;
            submit.pCommandBuffers = &command;
            submit.signalSemaphoreCount = 1;
            submit.pSignalSemaphores = &rendered;
            check(vkQueueSubmit(context.queue(), 1, &submit, VK_NULL_HANDLE), "submit");
            VkPresentInfoKHR present{};
            present.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
            present.waitSemaphoreCount = 1;
            present.pWaitSemaphores = &rendered;
            present.swapchainCount = 1;
            present.pSwapchains = &swapchain;
            present.pImageIndices = &index;
            check(vkQueuePresentKHR(context.queue(), &present), "present");
            check(vkQueueWaitIdle(context.queue()), "queueIdle");
            initialized[index] = true;
            ++frames;
        }
    } catch (const std::exception& error) {
        __android_log_print(ANDROID_LOG_ERROR, "RawrVideoProbe", "Vulkan RGBA16F probe: %s", error.what());
        frames = frames == 0 ? -2 : frames;
    }
    if (context.device()) {
        context.waitIdle();
        if (available) vkDestroySemaphore(context.device(), available, nullptr);
        if (rendered) vkDestroySemaphore(context.device(), rendered, nullptr);
        if (pool) vkDestroyCommandPool(context.device(), pool, nullptr);
        if (swapchain) vkDestroySwapchainKHR(context.device(), swapchain, nullptr);
    }
    if (surface && context.instance()) vkDestroySurfaceKHR(context.instance(), surface, nullptr);
    context.shutdown();
    ANativeWindow_release(window);
    return frames;
}
