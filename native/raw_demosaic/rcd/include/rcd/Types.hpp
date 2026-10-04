#pragma once
#include <vulkan/vulkan.h>
#include <cstddef>
#include <cstdint>
#include <string>

namespace rcd {

enum class BayerPattern : uint32_t { RGGB=0, GRBG=1, GBRG=2, BGGR=3 };

struct VulkanContext {
    VkPhysicalDevice physicalDevice=VK_NULL_HANDLE;
    VkDevice device=VK_NULL_HANDLE;
    uint32_t queueFamilyIndex=0;
    const VkAllocationCallbacks* allocator=nullptr;
    // Optional caller-owned pipeline cache shared across rebuilds.
    VkPipelineCache pipelineCache=VK_NULL_HANDLE;
};

struct BufferView {
    VkBuffer buffer=VK_NULL_HANDLE;
    VkDeviceSize offset=0;
    VkDeviceSize range=VK_WHOLE_SIZE;
};

// One float per RAW pixel in the established 0..255 linear Bayer working domain.
struct NormalizedBayerBufferView {
    BufferView buffer{};
    uint32_t width=0;
    uint32_t height=0;
    BayerPattern pattern=BayerPattern::BGGR;
};

struct RawCfaImageView {
    VkImage image=VK_NULL_HANDLE;
    VkImageView view=VK_NULL_HANDLE;
    VkFormat format=VK_FORMAT_UNDEFINED;
    VkImageLayout layout=VK_IMAGE_LAYOUT_GENERAL;
    uint32_t width=0;
    uint32_t height=0;
    BayerPattern pattern=BayerPattern::BGGR;
    float blackLevel[4]{0,0,0,0};
    float whiteLevel=65535.0f;
};

// Half-resolution canonical physical {R,G1,G2,B}. Values are already
// black-subtracted/white-normalized and may be <0 or >1.
struct PackedCfaImageView {
    VkImage image=VK_NULL_HANDLE;
    VkImageView view=VK_NULL_HANDLE;
    VkFormat format=VK_FORMAT_R16G16B16A16_SFLOAT;
    VkImageLayout layout=VK_IMAGE_LAYOUT_GENERAL;
    uint32_t width=0;
    uint32_t height=0;
    uint32_t rawWidth=0;
    uint32_t rawHeight=0;
    BayerPattern pattern=BayerPattern::BGGR;
};

struct LinearRgbImage {
    VkImage image=VK_NULL_HANDLE;
    VkImageView view=VK_NULL_HANDLE;
    VkFormat format=VK_FORMAT_R16G16B16A16_SFLOAT;
    VkImageLayout layout=VK_IMAGE_LAYOUT_GENERAL;
    uint32_t width=0;
    uint32_t height=0;
};

} // namespace rcd
