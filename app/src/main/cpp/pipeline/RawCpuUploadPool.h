#pragma once

#include <android/hardware_buffer.h>
#include <media/NdkImage.h>
#include <vulkan/vulkan.h>

#include <array>
#include <cstdint>

#include "imaging/FrameLimits.h"
#include "vulkan/ImageResources.h"
#include "vulkan/RawAhbImporter.h"

namespace rawrcam::pipeline {

// CPU ingress for camera frames the GPU can't import directly: RAW10 streams,
// and RAW16 AHBs whose Vulkan import the driver rejects. The camera frame is
// locked for CPU read and copied (RAW10 unpacked) into a per-slot host-visible
// buffer laid out exactly like the imported RAW buffer (stride = width), so
// the realtime recorders consume it unchanged through ImportedRaw. An owned
// R16 image is filled from that buffer on GPU when a recorder acquires it.
class RawCpuUploadPool final {
   public:
    RawCpuUploadPool() = default;
    ~RawCpuUploadPool();
    RawCpuUploadPool(const RawCpuUploadPool&) = delete;
    RawCpuUploadPool& operator=(const RawCpuUploadPool&) = delete;

    void initialize(VkPhysicalDevice physical, VkDevice device) noexcept;
    // Resources are allocated lazily per slot on first upload.
    void configure(uint32_t width, uint32_t height) noexcept;
    void destroy() noexcept;

    // Copies the camera frame into the slot's buffer. The caller must have
    // retired the slot's previous GPU work. acquireFenceFd stays owned by the caller.
    rawrcam::vulkan::ImportedRaw& upload(uint32_t slotIndex, AImage* image, AHardwareBuffer* ahb, int acquireFenceFd);

   private:
    struct Slot {
        VkBuffer buffer = VK_NULL_HANDLE;
        VkDeviceMemory bufferMemory = VK_NULL_HANDLE;
        void* mapped = nullptr;
        rawrcam::vulkan::OwnedImage image;
        rawrcam::vulkan::ImportedRaw raw;
    };
    void allocate(Slot& slot);
    void release(Slot& slot) noexcept;

    VkPhysicalDevice physical_ = VK_NULL_HANDLE;
    VkDevice device_ = VK_NULL_HANDLE;
    uint32_t width_ = 0;
    uint32_t height_ = 0;
    std::array<Slot, rawrcam::imaging::kRealtimeFramesInFlight> slots_{};
};

// Barriers for the frame's RAW image. Camera AHB imports are foreign-owned and
// use queue-family ownership transfers; CPU-uploaded frames are app-owned and
// are filled from their buffer instead.
void acquireRawInputImage(VkCommandBuffer command, const rawrcam::vulkan::ImportedRaw& raw, uint32_t queueFamily,
                          VkPipelineStageFlags dstStage, VkAccessFlags dstAccess);
void releaseRawInputImage(VkCommandBuffer command, const rawrcam::vulkan::ImportedRaw& raw, uint32_t queueFamily,
                          VkPipelineStageFlags srcStage, VkAccessFlags srcAccess);

}  // namespace rawrcam::pipeline
