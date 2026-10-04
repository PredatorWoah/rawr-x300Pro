#pragma once

#include <vulkan/vulkan.h>

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <vector>

namespace rawr::zsl_ring {

using FrameId = std::uint64_t;

struct PacketSource {
    VkBuffer meta = VK_NULL_HANDLE;
    VkBuffer sizes = VK_NULL_HANDLE;
    VkBuffer offsets = VK_NULL_HANDLE;
    VkBuffer payload = VK_NULL_HANDLE;
    std::uint32_t streams = 0;
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    std::uint32_t tilesX = 0;
    std::uint32_t tilesY = 0;
    std::uint64_t tableBytes = 0;
    std::uint64_t payloadBytes = 0;
};

struct PacketRef {
    FrameId frameId = 0;
    std::uint64_t timestampNs = 0;
    std::uint32_t streams = 0;
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    std::uint32_t tilesX = 0;
    std::uint32_t tilesY = 0;
    std::uint64_t tableBytes = 0;
    std::uint64_t payloadBytes = 0;
    std::uint64_t packedBytes = 0;
};

// Device-local rolling packet store. Payload/tables never transit the CPU during
// normal capture. The app supplies queue submission so its existing queue mutex
// remains the single synchronization authority.
class ZslRing final {
   public:
    using Submit = std::function<void(const VkSubmitInfo&, VkFence)>;

    ZslRing(VkPhysicalDevice physical, VkDevice device, std::uint32_t queueFamily, Submit submit,
            std::size_t maxFrames);
    ~ZslRing();
    ZslRing(const ZslRing&) = delete;
    ZslRing& operator=(const ZslRing&) = delete;

    std::size_t maxFrames() const noexcept { return maxFrames_; }

    // Copies encoder-owned GPU buffers into an exact-sized device-local packet.
    // The copy is asynchronous and ordered on the app's Vulkan queue.
    std::optional<PacketRef> push(FrameId frameId, std::uint64_t timestampNs, const PacketSource& source);

    std::vector<PacketRef> snapshot();

    void releaseSnapshot(const std::vector<PacketRef>& refs) noexcept;

    // Reuses one staging buffer, command pool/buffer, and fence for a pinned
    // snapshot. Intended for bounded-memory bundle persistence.
    class PacketReaderSession final {
       public:
        ~PacketReaderSession();
        PacketReaderSession(const PacketReaderSession&) = delete;
        PacketReaderSession& operator=(const PacketReaderSession&) = delete;
        bool read(FrameId frameId, std::vector<std::uint8_t>& out);

       private:
        friend class ZslRing;
        struct Impl;
        explicit PacketReaderSession(std::unique_ptr<Impl> impl);
        std::unique_ptr<Impl> impl_;
    };
    std::unique_ptr<PacketReaderSession> makePacketReaderSession(const std::vector<PacketRef>& refs);

    void clear() noexcept;

   private:
    struct Buffer {
        VkBuffer buffer = VK_NULL_HANDLE;
        VkDeviceMemory memory = VK_NULL_HANDLE;
        VkDeviceSize bytes = 0;
    };
    struct Entry {
        PacketRef ref{};
        Buffer packet{};
        VkCommandBuffer command = VK_NULL_HANDLE;
        VkFence fence = VK_NULL_HANDLE;
        std::uint32_t pins = 0;
        bool ready = false;
    };

    Buffer makeBuffer(VkDeviceSize bytes, VkBufferUsageFlags usage, VkMemoryPropertyFlags properties);
    void destroyBuffer(Buffer& b) noexcept;
    std::uint32_t memoryType(std::uint32_t bits, VkMemoryPropertyFlags flags) const;
    void retireCompleted();
    void destroyEntry(Entry& e) noexcept;

    VkPhysicalDevice physical_ = VK_NULL_HANDLE;
    VkDevice device_ = VK_NULL_HANDLE;
    std::uint32_t queueFamily_ = 0;
    Submit submit_;
    std::size_t maxFrames_ = 0;
    VkCommandPool commandPool_ = VK_NULL_HANDLE;
    std::vector<Entry> entries_;
    mutable std::mutex mutex_;
};

}  // namespace rawr::zsl_ring
