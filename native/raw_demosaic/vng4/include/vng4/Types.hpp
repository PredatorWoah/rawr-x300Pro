#pragma once
#include <vulkan/vulkan.h>
#include <cstdint>
#include <string>
namespace vng4 {
enum class BayerPattern : uint32_t { RGGB=0, GRBG=1, GBRG=2, BGGR=3 };
// pipelineCache: optional caller-owned cache shared across rebuilds.
struct VulkanContext { VkPhysicalDevice physicalDevice=VK_NULL_HANDLE; VkDevice device=VK_NULL_HANDLE; uint32_t queueFamilyIndex=0; const VkAllocationCallbacks* allocator=nullptr; VkPipelineCache pipelineCache=VK_NULL_HANDLE; };
struct BufferView { VkBuffer buffer=VK_NULL_HANDLE; VkDeviceSize offset=0; VkDeviceSize range=VK_WHOLE_SIZE; };
struct NormalizedBayerBufferView { BufferView buffer{}; uint32_t width=0,height=0; BayerPattern pattern=BayerPattern::BGGR; };
struct RawCfaImageView { VkImage image=VK_NULL_HANDLE; VkImageView view=VK_NULL_HANDLE; VkFormat format=VK_FORMAT_UNDEFINED; VkImageLayout layout=VK_IMAGE_LAYOUT_GENERAL; uint32_t width=0,height=0; BayerPattern pattern=BayerPattern::BGGR; float blackLevel[4]{0,0,0,0}; float whiteLevel=65535.0f; };
struct PackedCfaImageView { VkImage image=VK_NULL_HANDLE; VkImageView view=VK_NULL_HANDLE; VkFormat format=VK_FORMAT_R16G16B16A16_SFLOAT; VkImageLayout layout=VK_IMAGE_LAYOUT_GENERAL; uint32_t width=0,height=0,rawWidth=0,rawHeight=0; BayerPattern pattern=BayerPattern::BGGR; };
struct LinearRgbImage { VkImage image=VK_NULL_HANDLE; VkImageView view=VK_NULL_HANDLE; VkFormat format=VK_FORMAT_R16G16B16A16_SFLOAT; VkImageLayout layout=VK_IMAGE_LAYOUT_GENERAL; uint32_t width=0,height=0; };
}
