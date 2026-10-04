#pragma once
#include <vulkan/vulkan.h>

#include <stdexcept>
namespace rawrcam::vulkan {
inline uint32_t findCoherentHostMemoryType(VkPhysicalDevice physical, uint32_t bits) {
    VkPhysicalDeviceMemoryProperties properties{};
    vkGetPhysicalDeviceMemoryProperties(physical, &properties);
    constexpr auto required = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
    for (uint32_t i = 0; i < properties.memoryTypeCount; ++i) {
        if ((bits & (1u << i)) && (properties.memoryTypes[i].propertyFlags & required) == required) return i;
    }
    throw std::runtime_error("rendered still requires HOST_VISIBLE|HOST_COHERENT readback memory");
}
}  // namespace rawrcam::vulkan
