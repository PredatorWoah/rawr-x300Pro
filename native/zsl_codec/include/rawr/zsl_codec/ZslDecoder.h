#pragma once
#include <vulkan/vulkan.h>

#include <cstdint>

namespace rawr::zsl_codec {

struct DecodePacket {
    VkBuffer buffer = VK_NULL_HANDLE;
    VkDeviceSize packedBytes = 0;
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    std::uint32_t tilesX = 0;
    std::uint32_t tilesY = 0;
    std::uint32_t streams = 0;
    std::uint64_t tableBytes = 0;
    std::uint64_t payloadBytes = 0;
};

// Stateless-at-dispatch GPU decoder for zsl_ring's contiguous packet contract.
// The caller owns command-buffer lifetime and synchronization. Descriptor updates
// must not race an in-flight decode using the same decoder instance.
class VulkanDecoder final {
   public:
    VulkanDecoder() = default;
    ~VulkanDecoder() { reset(); }
    VulkanDecoder(const VulkanDecoder&) = delete;
    VulkanDecoder& operator=(const VulkanDecoder&) = delete;

    void initialize(VkDevice device);
    void reset() noexcept;
    // Reset transient descriptor allocations after the previous command batch has completed.
    void beginBatch();
    void record(VkCommandBuffer command, const DecodePacket& packet, VkImageView dstRawR16Uint);

   private:
    VkDevice device_ = VK_NULL_HANDLE;
    VkDescriptorSetLayout dsl_ = VK_NULL_HANDLE;
    VkPipelineLayout layout_ = VK_NULL_HANDLE;
    VkPipeline pipeline_ = VK_NULL_HANDLE;
    VkDescriptorPool pool_ = VK_NULL_HANDLE;
};

}  // namespace rawr::zsl_codec
