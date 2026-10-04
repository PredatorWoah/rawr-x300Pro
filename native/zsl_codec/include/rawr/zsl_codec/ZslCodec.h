#pragma once

#include <vulkan/vulkan.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

namespace rawr::zsl_codec {

struct GpuPacketSource {
    VkBuffer meta = VK_NULL_HANDLE;
    VkBuffer sizes = VK_NULL_HANDLE;
    VkBuffer offsets = VK_NULL_HANDLE;
    VkBuffer payload = VK_NULL_HANDLE;
    std::uint32_t streams = 0;
    std::uint64_t tableBytes = 0;
    std::uint64_t payloadBytes = 0;
};

struct FrameResult {
    std::uint64_t timestampNs = 0;
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    std::uint32_t tilesX = 0;
    std::uint32_t tilesY = 0;
    std::uint64_t rawBytes = 0;
    std::uint64_t compactBytes = 0;
    std::uint64_t payloadUsedBytes = 0;
    std::uint64_t payloadCapacityBytes = 0;
    bool wordCountPass = false;
    bool verified = false;
    double analyzeMs = 0.0;
    double packMs = 0.0;
    std::uint32_t mismatches = 0;
    std::uint32_t firstX = 0;
    std::uint32_t firstY = 0;
    std::uint32_t maxAbs = 0;
    GpuPacketSource gpuPacket{};

    bool lossless() const noexcept { return verified && mismatches == 0; }
    bool validPackedFrame() const noexcept {
        return wordCountPass && gpuPacket.meta != VK_NULL_HANDLE && gpuPacket.sizes != VK_NULL_HANDLE &&
               gpuPacket.offsets != VK_NULL_HANDLE && gpuPacket.payload != VK_NULL_HANDLE &&
               payloadUsedBytes <= payloadCapacityBytes;
    }
};

class VulkanCodec final {
   public:
    VulkanCodec() = default;
    ~VulkanCodec() { reset(); }
    VulkanCodec(const VulkanCodec&) = delete;
    VulkanCodec& operator=(const VulkanCodec&) = delete;

    void initialize(VkPhysicalDevice physical, VkDevice device, float timestampPeriod, std::uint32_t width,
                    std::uint32_t height, const std::vector<VkImageView>& rawViews, bool verifyEveryFrame = false);
    void reset() noexcept;

    // Poll only reads small mapped control/table buffers. The compressed payload
    // remains GPU-resident and is exposed by GpuPacketSource for zsl_ring.
    std::optional<FrameResult> record(VkCommandBuffer command, std::uint32_t slotIndex, std::uint64_t timestampNs);
    std::optional<FrameResult> poll(std::uint32_t slotIndex);

   private:
    struct Buffer {
        VkBuffer buffer = VK_NULL_HANDLE;
        VkDeviceMemory memory = VK_NULL_HANDLE;
        void* mapped = nullptr;
        VkDeviceSize bytes = 0;
    };
    struct Slot {
        Buffer meta, sizes, payload, verify, offsets, allocator;
        VkQueryPool query = VK_NULL_HANDLE;
        bool pending = false;
        std::uint64_t timestampNs = 0;
    };
    struct Push {
        std::uint32_t width, height, tilesX, tilesY, slotWords;
    };

    Buffer makeBuffer(VkDeviceSize bytes, VkBufferUsageFlags usage, VkMemoryPropertyFlags properties, bool map);
    void destroyBuffer(Buffer& buffer) noexcept;
    std::uint32_t memoryType(std::uint32_t bits, VkMemoryPropertyFlags flags) const;
    VkShaderModule module(const unsigned char* data, std::size_t size) const;
    VkPipeline makePipeline(const unsigned char* data, std::size_t size);

    VkPhysicalDevice physical_ = VK_NULL_HANDLE;
    VkDevice device_ = VK_NULL_HANDLE;
    float timestampPeriod_ = 1.0f;
    std::uint32_t width_ = 0, height_ = 0, tilesX_ = 0, tilesY_ = 0, streams_ = 0;
    bool verifyEveryFrame_ = false;
    static constexpr std::uint32_t kSlotWords = 128;
    VkDescriptorSetLayout dsl_ = VK_NULL_HANDLE;
    VkPipelineLayout layout_ = VK_NULL_HANDLE;
    VkPipeline analyze_ = VK_NULL_HANDLE, pack_ = VK_NULL_HANDLE, verify_ = VK_NULL_HANDLE;
    VkDescriptorPool pool_ = VK_NULL_HANDLE;
    std::vector<VkDescriptorSet> sets_;
    std::vector<Slot> slots_;
};

}  // namespace rawr::zsl_codec
