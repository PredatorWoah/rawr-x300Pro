#pragma once

// Shared Vulkan host helpers for the galosh engines (DRY: Raw owns the
// implementation now, Yuv reuses it in P1b). Header-only; module-local,
// no app dependency (raw vk* calls + std::runtime_error labels, mirroring
// SharedHighlightRuntime/DenoisePipeline conventions).

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>
#include <vulkan/vulkan.h>

namespace galosh::vk {

inline void check(VkResult result, const std::string& what) {
    if (result != VK_SUCCESS) throw std::runtime_error("galosh: " + what + " result=" + std::to_string(result));
}

inline uint32_t memoryIndex(VkPhysicalDevice physical, uint32_t bits,
                            VkMemoryPropertyFlags wanted, const std::string& what) {
    VkPhysicalDeviceMemoryProperties props{};
    vkGetPhysicalDeviceMemoryProperties(physical, &props);
    for (uint32_t i = 0; i < props.memoryTypeCount; ++i) {
        if ((bits & (1u << i)) && (props.memoryTypes[i].propertyFlags & wanted) == wanted) return i;
    }
    throw std::runtime_error("galosh: no memory type (" + what + ")");
}

struct Buffer {
    VkBuffer buf = VK_NULL_HANDLE;
    VkDeviceMemory mem = VK_NULL_HANDLE;
    VkDeviceSize size = 0;
};

inline Buffer makeBuffer(VkPhysicalDevice physical, VkDevice device, VkDeviceSize size,
                         VkBufferUsageFlags usage, VkMemoryPropertyFlags props,
                         const std::string& what) {
    Buffer b{};
    b.size = size;
    VkBufferCreateInfo bi{};
    bi.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bi.size = size;
    bi.usage = usage;
    bi.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    check(vkCreateBuffer(device, &bi, nullptr, &b.buf), what + " create buffer");
    VkMemoryRequirements req{};
    vkGetBufferMemoryRequirements(device, b.buf, &req);
    VkMemoryAllocateInfo ai{};
    ai.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    ai.allocationSize = req.size;
    ai.memoryTypeIndex = memoryIndex(physical, req.memoryTypeBits, props, what);
    check(vkAllocateMemory(device, &ai, nullptr, &b.mem), what + " allocate memory");
    check(vkBindBufferMemory(device, b.buf, b.mem, 0), what + " bind memory");
    return b;
}

inline void freeBuffer(VkDevice device, Buffer& b) noexcept {
    if (b.buf != VK_NULL_HANDLE) vkDestroyBuffer(device, b.buf, nullptr);
    if (b.mem != VK_NULL_HANDLE) vkFreeMemory(device, b.mem, nullptr);
    b = Buffer{};
}

struct Image {
    VkImage image = VK_NULL_HANDLE;
    VkDeviceMemory mem = VK_NULL_HANDLE;
    VkImageView view = VK_NULL_HANDLE;
    VkFormat format = VK_FORMAT_UNDEFINED;
};

inline Image makeImage(VkPhysicalDevice physical, VkDevice device, uint32_t width, uint32_t height,
                       VkFormat format, VkImageUsageFlags usage, const std::string& what) {
    Image im{};
    im.format = format;
    VkImageCreateInfo ci{};
    ci.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    ci.imageType = VK_IMAGE_TYPE_2D;
    ci.format = format;
    ci.extent = {width, height, 1};
    ci.mipLevels = 1;
    ci.arrayLayers = 1;
    ci.samples = VK_SAMPLE_COUNT_1_BIT;
    ci.tiling = VK_IMAGE_TILING_OPTIMAL;
    ci.usage = usage;
    ci.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    ci.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    check(vkCreateImage(device, &ci, nullptr, &im.image), what + " create image");
    VkMemoryRequirements req{};
    vkGetImageMemoryRequirements(device, im.image, &req);
    VkMemoryAllocateInfo ai{};
    ai.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    ai.allocationSize = req.size;
    ai.memoryTypeIndex = memoryIndex(physical, req.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, what);
    check(vkAllocateMemory(device, &ai, nullptr, &im.mem), what + " allocate memory");
    check(vkBindImageMemory(device, im.image, im.mem, 0), what + " bind memory");
    VkImageViewCreateInfo vi{};
    vi.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    vi.image = im.image;
    vi.viewType = VK_IMAGE_VIEW_TYPE_2D;
    vi.format = format;
    vi.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    check(vkCreateImageView(device, &vi, nullptr, &im.view), what + " create view");
    return im;
}

inline void freeImage(VkDevice device, Image& im) noexcept {
    if (im.view != VK_NULL_HANDLE) vkDestroyImageView(device, im.view, nullptr);
    if (im.image != VK_NULL_HANDLE) vkDestroyImage(device, im.image, nullptr);
    if (im.mem != VK_NULL_HANDLE) vkFreeMemory(device, im.mem, nullptr);
    im = Image{};
}

// Full execution+visibility barrier between our own submissions after a
// fence wait (fence guarantees completion; this makes writes visible).
inline void fullBarrier(VkCommandBuffer command) {
    VkMemoryBarrier mb{};
    mb.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
    mb.srcAccessMask = VK_ACCESS_MEMORY_WRITE_BIT;
    mb.dstAccessMask = VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT;
    vkCmdPipelineBarrier(command, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
                         0, 1, &mb, 0, nullptr, 0, nullptr);
}

inline void computeBarrier(VkCommandBuffer command) {
    VkMemoryBarrier mb{};
    mb.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
    mb.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT | VK_ACCESS_TRANSFER_WRITE_BIT;
    mb.dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT |
                       VK_ACCESS_TRANSFER_READ_BIT | VK_ACCESS_TRANSFER_WRITE_BIT;
    vkCmdPipelineBarrier(command, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_TRANSFER_BIT,
                         VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 1, &mb, 0,
                         nullptr, 0, nullptr);
}

// IEEE binary16 -> binary32 widen (exact; handles subnormal/inf/NaN).
// Host-side only (phase dumps + validation runners).
inline float halfToFloat(uint16_t h) {
    const uint32_t s = static_cast<uint32_t>(h & 0x8000u) << 16;
    uint32_t e = (h >> 10) & 0x1Fu;
    uint32_t m = h & 0x3FFu;
    uint32_t bits;
    if (e == 0) {
        if (m == 0) {
            bits = s;
        } else {
            e = 127 - 15 + 1;
            while (!(m & 0x400u)) {
                m <<= 1;
                e--;
            }
            m &= 0x3FFu;
            bits = s | (e << 23) | (m << 13);
        }
    } else if (e == 31) {
        bits = s | 0x7F800000u | (m << 13);
    } else {
        bits = s | ((e - 15 + 127) << 23) | (m << 13);
    }
    float f;
    std::memcpy(&f, &bits, 4);
    return f;
}

// Debug-only phase download (env GALOSH_PORT_DUMP=dir; zero-cost unset).
// Flushes the caller's in-progress recording if any, copies the plane out
// over chunked submits (staging is 4 KiB), then begins a fresh CB so the
// caller continues seamlessly. Works on either engine's Call (im/queue/
// recording members + staging pair); caller holds the queue mutex.
template <typename Call>
void debugDumpPhase(Call& c, Buffer& staging, void* stagingMap, Buffer& src, size_t nvals,
                    bool f16, const char* name) {
    const char* dir = std::getenv("GALOSH_PORT_DUMP");
    if (dir == nullptr || dir[0] == '\0') return;
    auto begin = [&] {
        VkCommandBufferBeginInfo bi{};
        bi.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        check(vkResetCommandBuffer(c.im->cmd, 0), "dump reset");
        check(vkBeginCommandBuffer(c.im->cmd, &bi), "dump begin");
        c.recording = true;
    };
    auto submit = [&] {
        check(vkEndCommandBuffer(c.im->cmd), "dump end");
        VkSubmitInfo si{};
        si.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        si.commandBufferCount = 1;
        si.pCommandBuffers = &c.im->cmd;
        check(vkResetFences(c.im->device, 1, &c.im->fence), "dump fence");
        check(vkQueueSubmit(c.queue, 1, &si, c.im->fence), "dump submit");
        check(vkWaitForFences(c.im->device, 1, &c.im->fence, VK_TRUE, UINT64_MAX), "dump wait");
        c.recording = false;
    };
    if (c.recording) submit();  // Flush everything recorded so far.
    std::vector<float> out(nvals);
    const size_t chunk = 4096;
    size_t done = 0;
    while (done < nvals) {
        const size_t nv = std::min(nvals - done, chunk / (f16 ? 2 : 4));
        begin();
        VkBufferCopy cc{};
        cc.srcOffset = done * (f16 ? 2 : 4);
        cc.dstOffset = 0;
        cc.size = nv * (f16 ? 2 : 4);
        vkCmdCopyBuffer(c.im->cmd, src.buf, staging.buf, 1, &cc);
        submit();
        if (f16) {
            const auto* h16 = static_cast<const uint16_t*>(stagingMap);
            for (size_t i = 0; i < nv; ++i) out[done + i] = halfToFloat(h16[i]);
        } else {
            std::memcpy(&out[done], stagingMap, nv * 4);
        }
        done += nv;
    }
    begin();  // Fresh CB for the caller to continue recording.
    char path[1024];
    snprintf(path, sizeof(path), "%s/%s.bin", dir, name);
    FILE* f = fopen(path, "wb");
    if (f != nullptr) {
        fwrite(out.data(), 4, nvals, f);
        fclose(f);
    }
}

}  // namespace galosh::vk
