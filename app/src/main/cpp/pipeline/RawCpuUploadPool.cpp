#include "pipeline/RawCpuUploadPool.h"

#include <unistd.h>

#include <stdexcept>
#include <string>

#include "imaging/Raw16CpuSnapshot.h"
#include "imaging/RawPixelSource.h"
#include "vulkan/Synchronization.h"

namespace rawrcam::pipeline {
namespace {
void vkCheck(VkResult r, const char* what) {
    if (r != VK_SUCCESS) throw std::runtime_error(std::string(what) + " VkResult=" + std::to_string(r));
}
uint32_t hostVisibleType(VkPhysicalDevice physical, uint32_t bits) {
    VkPhysicalDeviceMemoryProperties mp{};
    vkGetPhysicalDeviceMemoryProperties(physical, &mp);
    constexpr VkMemoryPropertyFlags wanted = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
    for (uint32_t i = 0; i < mp.memoryTypeCount; ++i)
        if ((bits & (1u << i)) && (mp.memoryTypes[i].propertyFlags & wanted) == wanted) return i;
    throw std::runtime_error("RAW CPU upload: no host-visible coherent memory type");
}
}  // namespace

RawCpuUploadPool::~RawCpuUploadPool() { destroy(); }

void RawCpuUploadPool::initialize(VkPhysicalDevice physical, VkDevice device) noexcept {
    physical_ = physical;
    device_ = device;
}

void RawCpuUploadPool::configure(uint32_t width, uint32_t height) noexcept {
    if (width == width_ && height == height_) return;
    destroy();
    width_ = width;
    height_ = height;
}

void RawCpuUploadPool::destroy() noexcept {
    for (auto& slot : slots_) release(slot);
}

void RawCpuUploadPool::release(Slot& slot) noexcept {
    if (!device_) return;
    if (slot.mapped) vkUnmapMemory(device_, slot.bufferMemory);
    if (slot.buffer) vkDestroyBuffer(device_, slot.buffer, nullptr);
    if (slot.bufferMemory) vkFreeMemory(device_, slot.bufferMemory, nullptr);
    rawrcam::vulkan::destroyOwnedImage(device_, slot.image);
    slot = Slot{};
}

void RawCpuUploadPool::allocate(Slot& slot) {
    const VkDeviceSize bytes = static_cast<VkDeviceSize>(width_) * height_ * sizeof(uint16_t);
    try {
        VkBufferCreateInfo bi{};
        bi.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        bi.size = bytes;
        bi.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
        bi.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        vkCheck(vkCreateBuffer(device_, &bi, nullptr, &slot.buffer), "RAW CPU upload buffer");
        VkMemoryRequirements req{};
        vkGetBufferMemoryRequirements(device_, slot.buffer, &req);
        VkMemoryAllocateInfo ai{};
        ai.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        ai.allocationSize = req.size;
        ai.memoryTypeIndex = hostVisibleType(physical_, req.memoryTypeBits);
        vkCheck(vkAllocateMemory(device_, &ai, nullptr, &slot.bufferMemory), "RAW CPU upload memory");
        vkCheck(vkBindBufferMemory(device_, slot.buffer, slot.bufferMemory, 0), "RAW CPU upload bind");
        vkCheck(vkMapMemory(device_, slot.bufferMemory, 0, VK_WHOLE_SIZE, 0, &slot.mapped), "RAW CPU upload map");
        slot.image =
            rawrcam::vulkan::createOwnedImage(physical_, device_, width_, height_, VK_FORMAT_R16_UINT,
                                              VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT |
                                                  VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT);
    } catch (...) {
        release(slot);
        throw;
    }
    slot.raw = {};
    slot.raw.cpuUploaded = true;
    slot.raw.image = slot.image.image;
    slot.raw.memory = slot.image.memory;
    slot.raw.view = slot.image.view;
    slot.raw.importBuffer = slot.buffer;
    slot.raw.importBufferStridePixels = width_;
    slot.raw.importBufferAvailable = true;
    slot.raw.importBufferAttempted = true;
}

rawrcam::vulkan::ImportedRaw& RawCpuUploadPool::upload(uint32_t slotIndex, AImage* image, AHardwareBuffer* ahb,
                                                       int acquireFenceFd) {
    if (!device_ || slotIndex >= slots_.size() || !width_ || !height_)
        throw std::runtime_error("RAW CPU upload: pool not configured");
    if (!image || !ahb) throw std::runtime_error("RAW CPU upload: missing camera image");
    Slot& slot = slots_[slotIndex];
    if (!slot.buffer) allocate(slot);

    const auto format = rawrcam::imaging::rawPixelFormatOf(image);
    int32_t rowStride = 0;
    if (!format || AImage_getPlaneRowStride(image, 0, &rowStride) != AMEDIA_OK || rowStride <= 0)
        throw std::runtime_error("RAW CPU upload: unsupported camera image layout");
    AHardwareBuffer_Desc desc{};
    AHardwareBuffer_describe(ahb, &desc);
    if (desc.width != width_ || desc.height != height_)
        throw std::runtime_error("RAW CPU upload: frame dimensions do not match configured RAW geometry");
    if (!(desc.usage & AHARDWAREBUFFER_USAGE_CPU_READ_MASK))
        throw std::runtime_error("RAW CPU upload: camera reader is not CPU-readable");

    const int fence = acquireFenceFd >= 0 ? dup(acquireFenceFd) : -1;
    void* address = nullptr;
    const int lockResult = AHardwareBuffer_lock(ahb, AHARDWAREBUFFER_USAGE_CPU_READ_OFTEN, fence, nullptr, &address);
    if (lockResult != 0 || !address)
        throw std::runtime_error("RAW CPU upload: AHB lock failed status=" + std::to_string(lockResult));
    const rawrcam::imaging::RawPixelSource source{static_cast<const uint8_t*>(address), *format, width_, height_,
                                                  static_cast<size_t>(rowStride)};
    const bool copied = rawrcam::imaging::copyRawToPackedRaw16(source, static_cast<uint16_t*>(slot.mapped));
    int releaseFence = -1;
    AHardwareBuffer_unlock(ahb, &releaseFence);
    if (releaseFence >= 0) close(releaseFence);
    if (!copied) throw std::runtime_error("RAW CPU upload: invalid source stride");

    slot.raw.ahb = ahb;
    slot.raw.desc = desc;
    return slot.raw;
}

void acquireRawInputImage(VkCommandBuffer command, const rawrcam::vulkan::ImportedRaw& raw, uint32_t queueFamily,
                          VkPipelineStageFlags dstStage, VkAccessFlags dstAccess) {
    if (!raw.cpuUploaded) {
        rawrcam::vulkan::acquireForeignImage(command, raw.image, queueFamily, dstStage, dstAccess);
        return;
    }
    // Fill the owned image from this frame's buffer. Contents are fully
    // replaced, so the previous layout can be discarded.
    VkImageMemoryBarrier toTransfer{};
    toTransfer.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    toTransfer.srcAccessMask = 0;
    toTransfer.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    toTransfer.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    toTransfer.newLayout = VK_IMAGE_LAYOUT_GENERAL;
    toTransfer.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    toTransfer.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    toTransfer.image = raw.image;
    toTransfer.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    vkCmdPipelineBarrier(command, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0,
                         nullptr, 1, &toTransfer);
    VkBufferImageCopy region{};
    region.bufferRowLength = raw.importBufferStridePixels;
    region.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
    region.imageExtent = {raw.desc.width, raw.desc.height, 1};
    vkCmdCopyBufferToImage(command, raw.importBuffer, raw.image, VK_IMAGE_LAYOUT_GENERAL, 1, &region);
    VkImageMemoryBarrier toRead = toTransfer;
    toRead.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    toRead.dstAccessMask = dstAccess;
    toRead.oldLayout = VK_IMAGE_LAYOUT_GENERAL;
    vkCmdPipelineBarrier(command, VK_PIPELINE_STAGE_TRANSFER_BIT, dstStage, 0, 0, nullptr, 0, nullptr, 1, &toRead);
}

void releaseRawInputImage(VkCommandBuffer command, const rawrcam::vulkan::ImportedRaw& raw, uint32_t queueFamily,
                          VkPipelineStageFlags srcStage, VkAccessFlags srcAccess) {
    // App-owned images stay with this queue; the slot fence guards reuse.
    if (!raw.cpuUploaded) rawrcam::vulkan::releaseForeignImage(command, raw.image, queueFamily, srcStage, srcAccess);
}

}  // namespace rawrcam::pipeline
