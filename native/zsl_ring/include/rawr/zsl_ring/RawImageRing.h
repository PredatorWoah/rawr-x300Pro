#pragma once
#include <vulkan/vulkan.h>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>
#include <optional>
#include <vector>

namespace rawr::zsl_ring {
using RawFrameId = std::uint64_t;
struct RawImageRef {
    RawFrameId frameId = 0;
    std::uint64_t timestampNs = 0;
    std::uint32_t width = 0, height = 0;
};
struct GpuRawImageView {
    RawImageRef ref{};
    VkImage image = VK_NULL_HANDLE;
    VkImageView view = VK_NULL_HANDLE;
    VkFormat format = VK_FORMAT_UNDEFINED;
    VkDeviceSize allocationBytes = 0;
};

// Rolling uncompressed device-local RAW16 image store for live multiframe capture.
// The caller records copies into its existing preview command buffer; this class
// owns image allocation/lifetime only and never submits to a VkQueue.
class RawImageRing final : public std::enable_shared_from_this<RawImageRing> {
   public:
    RawImageRing(VkPhysicalDevice physical, VkDevice device, std::uint32_t width, std::uint32_t height,
                 std::size_t maxFrames);
    ~RawImageRing();
    RawImageRing(const RawImageRing&) = delete;
    RawImageRing& operator=(const RawImageRing&) = delete;
    std::optional<RawImageRef> recordPush(VkCommandBuffer command, RawFrameId frameId, std::uint64_t timestampNs,
                                          VkImage source, VkImageLayout sourceLayout);
    void discardUnsubmitted(RawFrameId frameId) noexcept;
    void markReady(RawFrameId frameId) noexcept;
    std::size_t frameCount() const;
    std::uint64_t usedBytes() const noexcept { return usedBytes_; }
    class Snapshot final {
       public:
        ~Snapshot();
        Snapshot(const Snapshot&) = delete;
        Snapshot& operator=(const Snapshot&) = delete;
        Snapshot(Snapshot&&) = delete;
        Snapshot& operator=(Snapshot&&) = delete;
        std::vector<RawImageRef> refs() const;
        std::optional<GpuRawImageView> gpuImage(RawFrameId frameId) const;
        // Releases this snapshot's pin; other snapshots may still own the image. Call only
        // after the GPU fence for that frame's last consumer has signaled.
        void release(RawFrameId frameId) noexcept;

       private:
        friend class RawImageRing;
        struct Impl;
        explicit Snapshot(std::unique_ptr<Impl> p);
        std::unique_ptr<Impl> impl_;
    };
    // Snapshots share immutable pinned images; capacity never grows on recordPush.
    std::unique_ptr<Snapshot> snapshot(std::size_t maxFrames = 0);
    void clear() noexcept;

   private:
    struct Entry {
        RawImageRef ref{};
        VkImage image = VK_NULL_HANDLE;
        VkDeviceMemory memory = VK_NULL_HANDLE;
        VkImageView view = VK_NULL_HANDLE;
        VkDeviceSize bytes = 0;
        std::uint32_t pins = 0;
        bool occupied = false;
        bool ready = false;
    };
    std::uint32_t memoryType(std::uint32_t bits, VkMemoryPropertyFlags flags) const;
    void createEntry(Entry&);
    void destroyEntry(Entry&) noexcept;
    VkPhysicalDevice physical_ = VK_NULL_HANDLE;
    VkDevice device_ = VK_NULL_HANDLE;
    std::uint32_t width_ = 0, height_ = 0;
    std::size_t maxFrames_ = 0, next_ = 0;
    std::uint64_t usedBytes_ = 0;
    mutable std::mutex mutex_;
    std::vector<Entry> entries_;
};
}  // namespace rawr::zsl_ring
