#pragma once
#include <rawr/raw_gpu_pipeline/ScratchLayout.h>
#include <vulkan/vulkan.h>

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace rawr::raw_gpu_pipeline {
struct ArenaImage {
    VkImage image = VK_NULL_HANDLE;
    VkDeviceMemory memory = VK_NULL_HANDLE;
    VkImageView view = VK_NULL_HANDLE;
    VkFormat format = VK_FORMAT_UNDEFINED;
    Extent2D extent{};
    VkDeviceSize allocationBytes = 0;
};
struct ArenaBuffer {
    VkBuffer buffer = VK_NULL_HANDLE;
    VkDeviceMemory memory = VK_NULL_HANDLE;
    VkDeviceSize bytes = 0;
    VkDeviceSize allocationBytes = 0;
};

// Owns the concrete Vulkan resources described by ScratchLayout.  The arena is
// deliberately independent of AHB/ZSL ownership: compressed packets are borrowed
// from ZslRing, while decoded/algorithm scratch is owned here.
class ResourceArena final {
   public:
    ResourceArena() = default;
    ~ResourceArena() { reset(); }
    ResourceArena(const ResourceArena&) = delete;
    ResourceArena& operator=(const ResourceArena&) = delete;
    void initialize(VkPhysicalDevice physical, VkDevice device, const ScratchLayout& layout);
    void reset() noexcept;
    void recordInitializeLayouts(VkCommandBuffer command) const;
    void recordClearBurstState(VkCommandBuffer command) const;
    void recordPrepareCompanionAlignment(VkCommandBuffer command) const;
    const ArenaImage& image(const std::string& name) const;
    const ArenaBuffer& buffer(const std::string& name) const;
    VkDeviceSize physicalImageBytes() const noexcept { return physicalImageBytes_; }
    VkDeviceSize physicalBufferBytes() const noexcept { return physicalBufferBytes_; }

   private:
    VkPhysicalDevice physical_ = VK_NULL_HANDLE;
    VkDevice device_ = VK_NULL_HANDLE;
    std::unordered_map<std::string, ArenaImage> images_;
    std::unordered_map<std::string, ArenaBuffer> buffers_;
    std::vector<VkDeviceMemory> imageMemoryBlocks_;
    std::vector<VkDeviceMemory> bufferMemoryBlocks_;
    VkDeviceSize physicalImageBytes_ = 0, physicalBufferBytes_ = 0;
};
VkFormat pixelStorageFormat(PixelStorage storage);
}  // namespace rawr::raw_gpu_pipeline
