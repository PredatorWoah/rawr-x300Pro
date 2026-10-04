#pragma once
// Small ROI copies from full-frame production film buffers; no crop-sized rendering.
#include <vulkan/vulkan.h>

#include <array>
#include <fstream>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>
namespace rawrcam::diagnostics::replay {
class FilmReplayReadback {
    struct Entry {
        VkDevice device{};
        VkBuffer buffer{};
        VkDeviceMemory memory{};
        void* mapped = nullptr;
        size_t bytes = 0;
        std::string name;
        ~Entry() {
            if (mapped) vkUnmapMemory(device, memory);
            if (buffer) vkDestroyBuffer(device, buffer, nullptr);
            if (memory) vkFreeMemory(device, memory, nullptr);
        }
    };
    VkDevice device_;
    VkPhysicalDevice physical_;
    std::array<uint32_t, 4> roi_;
    std::vector<std::unique_ptr<Entry>> entries_;
    static void check(VkResult r) {
        if (r != VK_SUCCESS) throw std::runtime_error("film diagnostic allocation failed");
    }

   public:
    FilmReplayReadback(VkPhysicalDevice p, VkDevice d, std::array<uint32_t, 4> roi)
        : device_(d), physical_(p), roi_(roi) {}
    static void record(void* self, VkCommandBuffer cmd, const char* name, VkBuffer source, uint32_t w, uint32_t h) {
        static_cast<FilmReplayReadback*>(self)->record(cmd, name, source, w, h);
    }
    void record(VkCommandBuffer cmd, const char* name, VkBuffer source, uint32_t w, uint32_t h) {
        const auto [x, y, rw, rh] = roi_;
        if (!rw || !rh || x >= w || y >= h || rw > w - x || rh > h - y)
            throw std::runtime_error("film diagnostic ROI out of bounds");
        auto e = std::make_unique<Entry>();
        e->device = device_;
        e->bytes = size_t(rw) * rh * 16;
        e->name = name;
        VkBufferCreateInfo bi{};
        bi.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        bi.size = e->bytes;
        bi.usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT;
        check(vkCreateBuffer(device_, &bi, nullptr, &e->buffer));
        VkMemoryRequirements mr{};
        vkGetBufferMemoryRequirements(device_, e->buffer, &mr);
        VkPhysicalDeviceMemoryProperties mp{};
        vkGetPhysicalDeviceMemoryProperties(physical_, &mp);
        uint32_t type = mp.memoryTypeCount;
        for (uint32_t i = 0; i < mp.memoryTypeCount; ++i) {
            const auto flags = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
            if ((mr.memoryTypeBits & (1u << i)) && (mp.memoryTypes[i].propertyFlags & flags) == flags) {
                type = i;
                break;
            }
        }
        if (type == mp.memoryTypeCount) throw std::runtime_error("film diagnostic coherent memory unavailable");
        VkMemoryAllocateInfo ai{};
        ai.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        ai.allocationSize = mr.size;
        ai.memoryTypeIndex = type;
        check(vkAllocateMemory(device_, &ai, nullptr, &e->memory));
        check(vkBindBufferMemory(device_, e->buffer, e->memory, 0));
        check(vkMapMemory(device_, e->memory, 0, e->bytes, 0, &e->mapped));
        std::vector<VkBufferCopy> copies;
        for (uint32_t row = 0; row < rh; ++row)
            copies.push_back(
                {(VkDeviceSize(y + row) * w + x) * 16, VkDeviceSize(row) * rw * 16, VkDeviceSize(rw) * 16});
        vkCmdCopyBuffer(cmd, source, e->buffer, uint32_t(copies.size()), copies.data());
        entries_.push_back(std::move(e));
    }
    // Only after the production renderer's fence has completed.
    void write(const std::string& prefix) {
        for (const auto& e : entries_) {
            std::ofstream out(prefix + "_" + e->name + ".rgba32f", std::ios::binary);
            out.write(static_cast<const char*>(e->mapped), e->bytes);
            if (!out) throw std::runtime_error("film diagnostic write failed");
        }
    }
};
}  // namespace rawrcam::diagnostics::replay
