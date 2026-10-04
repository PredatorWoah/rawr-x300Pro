#pragma once
#include <vulkan/vulkan.h>

#include <cstdint>

namespace tonemap {

struct VulkanContext {
    VkPhysicalDevice physicalDevice = VK_NULL_HANDLE;
    VkDevice device = VK_NULL_HANDLE;
    const VkAllocationCallbacks* allocator = nullptr;
};

// Scene-linear RGB input only. This type must not be used to pass RAW/CFA or
// packed {R,G1,G2,B} data directly to TonemapEngine. Values >1.0 are valid.
struct LinearRgbImageView {
    VkImageView view = VK_NULL_HANDLE;
    VkFormat format = VK_FORMAT_R16G16B16A16_SFLOAT;
    VkImageLayout layout = VK_IMAGE_LAYOUT_GENERAL;
    uint32_t width = 0;
    uint32_t height = 0;
};

struct SrgbImageView {
    VkImageView view = VK_NULL_HANDLE;
    // Encoded SDR output: RGBA8 preview or RGBA16F recording intermediate.
    VkFormat format = VK_FORMAT_R8G8B8A8_UNORM;
    VkImageLayout layout = VK_IMAGE_LAYOUT_GENERAL;
    uint32_t width = 0;
    uint32_t height = 0;
};

}  // namespace tonemap
