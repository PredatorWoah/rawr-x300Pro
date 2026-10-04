#pragma once
#include <vulkan/vulkan.h>
#include <cstdint>
namespace fcc {
struct VulkanContext {
    VkPhysicalDevice physicalDevice=VK_NULL_HANDLE;
    VkDevice device=VK_NULL_HANDLE;
    uint32_t queueFamilyIndex=0;
    const VkAllocationCallbacks* allocator=nullptr;
};
struct LinearRgbImage {
    VkImage image=VK_NULL_HANDLE;
    VkImageView view=VK_NULL_HANDLE;
    VkFormat format=VK_FORMAT_R16G16B16A16_SFLOAT;
    VkImageLayout layout=VK_IMAGE_LAYOUT_GENERAL;
    uint32_t width=0,height=0;
};
}
