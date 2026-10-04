#pragma once
#include <cstddef>
#include <cstdint>
#define VK_NULL_HANDLE nullptr
#define VK_WHOLE_SIZE (~0ull)
using VkDeviceSize = uint64_t;
using VkAccessFlags = uint32_t;
using VkMemoryPropertyFlags = uint32_t;
using VkFormat = uint32_t;
using VkImageLayout = uint32_t;
using VkResult = int32_t;
#define VK_SUCCESS 0
#define VK_FORMAT_UNDEFINED 0
#define VK_FORMAT_R16_UINT 1
#define VK_FORMAT_R32_SFLOAT 2
#define VK_FORMAT_R16G16B16A16_SFLOAT 3
#define VK_IMAGE_LAYOUT_UNDEFINED 0
#define VK_IMAGE_LAYOUT_GENERAL 1
#define VK_ACCESS_SHADER_READ_BIT 1u
#define VK_ACCESS_SHADER_WRITE_BIT 2u
#define VK_ACCESS_HOST_READ_BIT 4u
#define VK_ACCESS_TRANSFER_READ_BIT 0x00000800u
#define VK_ACCESS_TRANSFER_WRITE_BIT 0x00001000u
#define VK_IMAGE_ASPECT_COLOR_BIT 1u
#define VK_IMAGE_USAGE_STORAGE_BIT 1u
#define VK_IMAGE_USAGE_TRANSFER_SRC_BIT 0x00000001u
#define VK_IMAGE_USAGE_TRANSFER_DST_BIT 0x00000002u
#define VK_BUFFER_USAGE_STORAGE_BUFFER_BIT 1u
#define VK_IMAGE_TILING_OPTIMAL 0u
#define VK_IMAGE_TYPE_2D 0u
#define VK_SAMPLE_COUNT_1_BIT 1u
#define VK_SHARING_MODE_EXCLUSIVE 0u
#define VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT 1u
#define VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT 2u
#define VK_MEMORY_PROPERTY_HOST_COHERENT_BIT 4u
#define VK_IMAGE_VIEW_TYPE_2D 0u
#define VK_DESCRIPTOR_TYPE_STORAGE_IMAGE 0u
#define VK_DESCRIPTOR_TYPE_STORAGE_BUFFER 1u
#define VK_SHADER_STAGE_COMPUTE_BIT 1u
#define VK_PIPELINE_BIND_POINT_COMPUTE 0u
#define VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT 1u
#define VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT 2u
#define VK_PIPELINE_STAGE_HOST_BIT 4u
#define VK_PIPELINE_STAGE_TRANSFER_BIT 0x00001000u
#define VK_PIPELINE_STAGE_ALL_COMMANDS_BIT 0x00010000u
#define VK_QUEUE_FAMILY_IGNORED (~0u)
#define VK_QUERY_TYPE_TIMESTAMP 0u
#define VK_QUERY_RESULT_64_BIT 1u
#define VK_QUERY_RESULT_WAIT_BIT 2u
#define VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO 1u
#define VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO 2u
#define VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO 3u
#define VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO 4u
#define VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO 5u
#define VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO 6u
#define VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO 7u
#define VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO 8u
#define VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO 9u
#define VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO 10u
#define VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO 11u
#define VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER 12u
#define VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET 13u
#define VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO 14u
#define VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER 15u
struct VkAllocationCallbacks {};
#define H(name)         \
    struct name##_T {}; \
    using name = name##_T*;
H(VkPhysicalDevice)
H(VkDevice)
H(VkBuffer) H(VkCommandBuffer) H(VkImage) H(VkImageView) H(VkDeviceMemory) H(VkDescriptorSetLayout) H(VkPipelineLayout)
    H(VkDescriptorPool) H(VkDescriptorSet) H(VkShaderModule) H(VkPipeline) H(VkQueryPool) H(VkPipelineCache)
#undef H
        struct VkExtent3D {
    uint32_t width, height, depth;
};
struct VkOffset3D {
    int32_t x, y, z;
};
struct VkImageSubresourceLayers {
    uint32_t aspectMask, mipLevel, baseArrayLayer, layerCount;
};
struct VkImageCopy {
    VkImageSubresourceLayers srcSubresource{};
    VkOffset3D srcOffset{};
    VkImageSubresourceLayers dstSubresource{};
    VkOffset3D dstOffset{};
    VkExtent3D extent{};
};
struct VkImageSubresourceRange {
    uint32_t aspectMask, baseMipLevel, levelCount, baseArrayLayer, layerCount;
};
struct VkBufferCreateInfo {
    uint32_t sType;
    const void* pNext = nullptr;
    uint32_t flags = 0;
    VkDeviceSize size = 0;
    uint32_t usage = 0, sharingMode = 0;
    uint32_t queueFamilyIndexCount = 0;
    const uint32_t* pQueueFamilyIndices = nullptr;
};
struct VkImageCreateInfo {
    uint32_t sType;
    const void* pNext = nullptr;
    uint32_t flags = 0, imageType = 0;
    VkFormat format = 0;
    VkExtent3D extent{};
    uint32_t mipLevels = 0, arrayLayers = 0, samples = 0, tiling = 0, usage = 0, sharingMode = 0;
};
struct VkMemoryRequirements {
    VkDeviceSize size = 0, alignment = 0;
    uint32_t memoryTypeBits = 0;
};
struct VkMemoryAllocateInfo {
    uint32_t sType;
    const void* pNext = nullptr;
    VkDeviceSize allocationSize = 0;
    uint32_t memoryTypeIndex = 0;
};
struct VkImageViewCreateInfo {
    uint32_t sType;
    const void* pNext = nullptr;
    uint32_t flags = 0;
    VkImage image = nullptr;
    uint32_t viewType = 0;
    VkFormat format = 0;
    struct {
        uint32_t r, g, b, a;
    } components{};
    VkImageSubresourceRange subresourceRange{};
};
struct VkMemoryType {
    uint32_t propertyFlags = 0, heapIndex = 0;
};
struct VkPhysicalDeviceMemoryProperties {
    uint32_t memoryTypeCount = 0;
    VkMemoryType memoryTypes[32]{};
};
struct VkDescriptorSetLayoutBinding {
    uint32_t binding = 0, descriptorType = 0, descriptorCount = 0, stageFlags = 0;
    const void* pImmutableSamplers = nullptr;
};
struct VkDescriptorSetLayoutCreateInfo {
    uint32_t sType;
    const void* pNext = nullptr;
    uint32_t flags = 0, bindingCount = 0;
    const VkDescriptorSetLayoutBinding* pBindings = nullptr;
};
struct VkPushConstantRange {
    uint32_t stageFlags = 0, offset = 0, size = 0;
};
struct VkPipelineLayoutCreateInfo {
    uint32_t sType;
    const void* pNext = nullptr;
    uint32_t flags = 0, setLayoutCount = 0;
    const VkDescriptorSetLayout* pSetLayouts = nullptr;
    uint32_t pushConstantRangeCount = 0;
    const VkPushConstantRange* pPushConstantRanges = nullptr;
};
struct VkDescriptorPoolSize {
    uint32_t type = 0, descriptorCount = 0;
};
struct VkDescriptorPoolCreateInfo {
    uint32_t sType;
    const void* pNext = nullptr;
    uint32_t flags = 0, maxSets = 0, poolSizeCount = 0;
    const VkDescriptorPoolSize* pPoolSizes = nullptr;
};
struct VkDescriptorSetAllocateInfo {
    uint32_t sType;
    const void* pNext = nullptr;
    VkDescriptorPool descriptorPool = nullptr;
    uint32_t descriptorSetCount = 0;
    const VkDescriptorSetLayout* pSetLayouts = nullptr;
};
struct VkShaderModuleCreateInfo {
    uint32_t sType;
    const void* pNext = nullptr;
    uint32_t flags = 0;
    size_t codeSize = 0;
    const uint32_t* pCode = nullptr;
};
struct VkPipelineShaderStageCreateInfo {
    uint32_t sType;
    const void* pNext = nullptr;
    uint32_t flags = 0, stage = 0;
    VkShaderModule module = nullptr;
    const char* pName = nullptr;
    const void* pSpecializationInfo = nullptr;
};
struct VkComputePipelineCreateInfo {
    uint32_t sType;
    const void* pNext = nullptr;
    uint32_t flags = 0;
    VkPipelineShaderStageCreateInfo stage{};
    VkPipelineLayout layout = nullptr;
    void* basePipelineHandle = nullptr;
    int32_t basePipelineIndex = 0;
};
struct VkPhysicalDeviceLimits {
    float timestampPeriod = 1;
};
struct VkPhysicalDeviceProperties {
    uint32_t apiVersion = 0, driverVersion = 0, vendorID = 0, deviceID = 0;
    uint32_t deviceType = 0;
    char deviceName[256]{};
    uint8_t pipelineCacheUUID[16]{};
    VkPhysicalDeviceLimits limits{};
};
struct VkQueueFamilyProperties {
    uint32_t queueFlags = 0, queueCount = 0, timestampValidBits = 64;
};
struct VkQueryPoolCreateInfo {
    uint32_t sType;
    const void* pNext = nullptr;
    uint32_t flags = 0, queryType = 0, queryCount = 0;
};
struct VkBufferMemoryBarrier {
    uint32_t sType;
    const void* pNext = nullptr;
    VkAccessFlags srcAccessMask = 0, dstAccessMask = 0;
    uint32_t srcQueueFamilyIndex = 0, dstQueueFamilyIndex = 0;
    VkBuffer buffer = nullptr;
    VkDeviceSize offset = 0, size = 0;
};
struct VkImageMemoryBarrier {
    uint32_t sType;
    const void* pNext = nullptr;
    VkAccessFlags srcAccessMask = 0, dstAccessMask = 0;
    VkImageLayout oldLayout = 0, newLayout = 0;
    uint32_t srcQueueFamilyIndex = 0, dstQueueFamilyIndex = 0;
    VkImage image = nullptr;
    VkImageSubresourceRange subresourceRange{};
};
struct VkDescriptorBufferInfo {
    VkBuffer buffer = nullptr;
    VkDeviceSize offset = 0, range = 0;
};
struct VkDescriptorImageInfo {
    void* sampler = nullptr;
    VkImageView imageView = nullptr;
    VkImageLayout imageLayout = 0;
};
struct VkWriteDescriptorSet {
    uint32_t sType;
    const void* pNext = nullptr;
    VkDescriptorSet dstSet = nullptr;
    uint32_t dstBinding = 0, dstArrayElement = 0, descriptorCount = 0, descriptorType = 0;
    const VkDescriptorImageInfo* pImageInfo = nullptr;
    const VkDescriptorBufferInfo* pBufferInfo = nullptr;
    const void* pTexelBufferView = nullptr;
};
extern "C" {
VkResult vkCreateBuffer(VkDevice, const VkBufferCreateInfo*, const VkAllocationCallbacks*, VkBuffer*);
void vkGetBufferMemoryRequirements(VkDevice, VkBuffer, VkMemoryRequirements*);
VkResult vkBindBufferMemory(VkDevice, VkBuffer, VkDeviceMemory, VkDeviceSize);
void vkDestroyBuffer(VkDevice, VkBuffer, const VkAllocationCallbacks*);
VkResult vkMapMemory(VkDevice, VkDeviceMemory, VkDeviceSize, VkDeviceSize, uint32_t, void**);
void vkUnmapMemory(VkDevice, VkDeviceMemory);
void vkGetPhysicalDeviceMemoryProperties(VkPhysicalDevice, VkPhysicalDeviceMemoryProperties*);
VkResult vkCreateImage(VkDevice, const VkImageCreateInfo*, const VkAllocationCallbacks*, VkImage*);
void vkGetImageMemoryRequirements(VkDevice, VkImage, VkMemoryRequirements*);
VkResult vkAllocateMemory(VkDevice, const VkMemoryAllocateInfo*, const VkAllocationCallbacks*, VkDeviceMemory*);
VkResult vkBindImageMemory(VkDevice, VkImage, VkDeviceMemory, VkDeviceSize);
VkResult vkCreateImageView(VkDevice, const VkImageViewCreateInfo*, const VkAllocationCallbacks*, VkImageView*);
void vkDestroyImageView(VkDevice, VkImageView, const VkAllocationCallbacks*);
void vkDestroyImage(VkDevice, VkImage, const VkAllocationCallbacks*);
void vkFreeMemory(VkDevice, VkDeviceMemory, const VkAllocationCallbacks*);
VkResult vkCreateDescriptorSetLayout(VkDevice, const VkDescriptorSetLayoutCreateInfo*, const VkAllocationCallbacks*,
                                     VkDescriptorSetLayout*);
VkResult vkCreatePipelineLayout(VkDevice, const VkPipelineLayoutCreateInfo*, const VkAllocationCallbacks*,
                                VkPipelineLayout*);
VkResult vkCreateDescriptorPool(VkDevice, const VkDescriptorPoolCreateInfo*, const VkAllocationCallbacks*,
                                VkDescriptorPool*);
VkResult vkAllocateDescriptorSets(VkDevice, const VkDescriptorSetAllocateInfo*, VkDescriptorSet*);
VkResult vkCreateShaderModule(VkDevice, const VkShaderModuleCreateInfo*, const VkAllocationCallbacks*, VkShaderModule*);
VkResult vkCreateComputePipelines(VkDevice, VkPipelineCache, uint32_t, const VkComputePipelineCreateInfo*,
                                  const VkAllocationCallbacks*, VkPipeline*);
void vkDestroyPipeline(VkDevice, VkPipeline, const VkAllocationCallbacks*);
void vkDestroyShaderModule(VkDevice, VkShaderModule, const VkAllocationCallbacks*);
void vkDestroyDescriptorPool(VkDevice, VkDescriptorPool, const VkAllocationCallbacks*);
void vkDestroyPipelineLayout(VkDevice, VkPipelineLayout, const VkAllocationCallbacks*);
void vkDestroyDescriptorSetLayout(VkDevice, VkDescriptorSetLayout, const VkAllocationCallbacks*);
void vkGetPhysicalDeviceProperties(VkPhysicalDevice, VkPhysicalDeviceProperties*);
void vkGetPhysicalDeviceQueueFamilyProperties(VkPhysicalDevice, uint32_t*, VkQueueFamilyProperties*);
VkResult vkCreateQueryPool(VkDevice, const VkQueryPoolCreateInfo*, const VkAllocationCallbacks*, VkQueryPool*);
void vkDestroyQueryPool(VkDevice, VkQueryPool, const VkAllocationCallbacks*);
void vkCmdPipelineBarrier(VkCommandBuffer, uint32_t, uint32_t, uint32_t, uint32_t, const void*, uint32_t,
                          const VkBufferMemoryBarrier*, uint32_t, const VkImageMemoryBarrier*);
void vkCmdCopyImage(VkCommandBuffer, VkImage, VkImageLayout, VkImage, VkImageLayout, uint32_t, const VkImageCopy*);
void vkUpdateDescriptorSets(VkDevice, uint32_t, const VkWriteDescriptorSet*, uint32_t, const void*);
void vkCmdWriteTimestamp(VkCommandBuffer, uint32_t, VkQueryPool, uint32_t);
void vkCmdBindPipeline(VkCommandBuffer, uint32_t, VkPipeline);
void vkCmdBindDescriptorSets(VkCommandBuffer, uint32_t, VkPipelineLayout, uint32_t, uint32_t, const VkDescriptorSet*,
                             uint32_t, const uint32_t*);
void vkCmdPushConstants(VkCommandBuffer, VkPipelineLayout, uint32_t, uint32_t, uint32_t, const void*);
void vkCmdDispatch(VkCommandBuffer, uint32_t, uint32_t, uint32_t);
void vkCmdResetQueryPool(VkCommandBuffer, VkQueryPool, uint32_t, uint32_t);
VkResult vkGetQueryPoolResults(VkDevice, VkQueryPool, uint32_t, uint32_t, size_t, void*, VkDeviceSize, uint32_t);
}
