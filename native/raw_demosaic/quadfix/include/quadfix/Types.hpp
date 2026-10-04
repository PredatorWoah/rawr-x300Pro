#pragma once
#include <vulkan/vulkan.h>
#include <cstddef>
#include <cstdint>

namespace quadfix {

// One float per Bayer pixel in the established 0..255 linear working domain
// (same domain as rcd::NormalizedBayerBufferView). Unlike demosaic, the
// adapt004 filter is identical for all four CFA phases, so views carry no
// Bayer pattern: every 2x2 phase is processed the same way.
struct VulkanContext {
    VkPhysicalDevice physicalDevice = VK_NULL_HANDLE;
    VkDevice device = VK_NULL_HANDLE;
    uint32_t queueFamilyIndex = 0;
    const VkAllocationCallbacks* allocator = nullptr;
};

struct BufferView {
    VkBuffer buffer = VK_NULL_HANDLE;
    VkDeviceSize offset = 0;
    VkDeviceSize range = VK_WHOLE_SIZE;
};

// Full-resolution Bayer tile INCLUDING the host-side halo. Width and height
// must be multiples of 16 (plane 8x8 demod blocks) and match the pipeline
// config exactly. offset must be 0 (mirrors the RCD contract).
struct BayerBufferView {
    BufferView buffer{};
    uint32_t width = 0;
    uint32_t height = 0;
};

}  // namespace quadfix
