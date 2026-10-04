#include <rawr/zsl_ring/RawImageRing.h>

#include <cassert>
#include <iostream>
#include <stdexcept>

// Fake Vulkan allocations let this test exercise production pin/capacity logic on
// the host without a GPU. GPU execution/synchronization still needs device testing.
namespace {
uintptr_t nextHandle = 1;
int images = 0, memories = 0, views = 0, allocations = 0;
int failAllocation = -1;
template <class T>
T handle() {
    return reinterpret_cast<T>(nextHandle++);
}
}  // namespace
extern "C" {
VKAPI_ATTR void VKAPI_CALL vkGetPhysicalDeviceMemoryProperties(VkPhysicalDevice, VkPhysicalDeviceMemoryProperties* p) {
    *p = {};
    p->memoryTypeCount = 1;
    p->memoryTypes[0].propertyFlags = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
}
VKAPI_ATTR VkResult VKAPI_CALL vkCreateImage(VkDevice, const VkImageCreateInfo*, const VkAllocationCallbacks*,
                                             VkImage* p) {
    *p = handle<VkImage>();
    ++images;
    return VK_SUCCESS;
}
VKAPI_ATTR void VKAPI_CALL vkGetImageMemoryRequirements(VkDevice, VkImage, VkMemoryRequirements* p) {
    *p = {};
    p->size = 1024;
    p->memoryTypeBits = 1;
}
VKAPI_ATTR VkResult VKAPI_CALL vkAllocateMemory(VkDevice, const VkMemoryAllocateInfo*, const VkAllocationCallbacks*,
                                                VkDeviceMemory* p) {
    if (++allocations == failAllocation) return VK_ERROR_OUT_OF_DEVICE_MEMORY;
    *p = handle<VkDeviceMemory>();
    ++memories;
    return VK_SUCCESS;
}
VKAPI_ATTR VkResult VKAPI_CALL vkBindImageMemory(VkDevice, VkImage, VkDeviceMemory, VkDeviceSize) { return VK_SUCCESS; }
VKAPI_ATTR VkResult VKAPI_CALL vkCreateImageView(VkDevice, const VkImageViewCreateInfo*, const VkAllocationCallbacks*,
                                                 VkImageView* p) {
    *p = handle<VkImageView>();
    ++views;
    return VK_SUCCESS;
}
VKAPI_ATTR void VKAPI_CALL vkDestroyImage(VkDevice, VkImage, const VkAllocationCallbacks*) { --images; }
VKAPI_ATTR void VKAPI_CALL vkFreeMemory(VkDevice, VkDeviceMemory, const VkAllocationCallbacks*) { --memories; }
VKAPI_ATTR void VKAPI_CALL vkDestroyImageView(VkDevice, VkImageView, const VkAllocationCallbacks*) { --views; }
VKAPI_ATTR void VKAPI_CALL vkCmdPipelineBarrier(VkCommandBuffer, VkPipelineStageFlags, VkPipelineStageFlags,
                                                VkDependencyFlags, uint32_t, const VkMemoryBarrier*, uint32_t,
                                                const VkBufferMemoryBarrier*, uint32_t, const VkImageMemoryBarrier*) {}
VKAPI_ATTR void VKAPI_CALL vkCmdCopyImage(VkCommandBuffer, VkImage, VkImageLayout, VkImage, VkImageLayout, uint32_t,
                                          const VkImageCopy*) {}
}
int main() {
    using rawr::zsl_ring::RawImageRing;
    auto physical = handle<VkPhysicalDevice>();
    auto device = handle<VkDevice>();
    auto command = handle<VkCommandBuffer>();
    auto source = handle<VkImage>();
    {
        RawImageRing ring(physical, device, 4, 4, 3);
        const auto bytes = ring.usedBytes();
        const auto initialAllocations = allocations;
        auto push = [&](uint64_t id) {
            return ring.recordPush(command, id, id * 1000, source, VK_IMAGE_LAYOUT_GENERAL);
        };
        for (uint64_t id = 1; id <= 3; ++id) assert(push(id));
        assert(!push(4));  // Never overwrite an unretired GPU copy.
        for (uint64_t id = 1; id <= 3; ++id) ring.markReady(id);
        std::vector<std::unique_ptr<RawImageRing::Snapshot>> shots;
        for (int i = 0; i < 8; ++i) shots.push_back(ring.snapshot(3));
        for (const auto& shot : shots) assert(shot->refs().size() == 3);
        assert(!push(4));  // Eight captures share three immutable allocations.
        shots[0]->release(1);
        shots[0]->release(1);  // Must not decrement another capture's pin.
        shots[0].reset();
        assert(shots[1]->gpuImage(1));
        assert(!push(4));
        for (size_t i = 1; i < shots.size(); ++i) shots[i]->release(1);
        assert(push(4));
        assert(!push(5));
        ring.markReady(4);
        shots.clear();
        auto latest = ring.snapshot(2);
        assert(latest->refs()[0].frameId == 3 && latest->refs()[1].frameId == 4);
        assert(push(5));  // Unselected frame remains reusable.
        assert(!push(6));
        latest.reset();
        assert(push(6));
        assert(ring.usedBytes() == bytes && allocations == initialAllocations);
    }
    assert(images == 0 && memories == 0 && views == 0);
    {
        auto ring = std::make_shared<RawImageRing>(physical, device, 4, 4, 1);
        assert(ring->recordPush(command, 1, 1000, source, VK_IMAGE_LAYOUT_GENERAL));
        ring->markReady(1);
        auto queued = ring->snapshot();
        ring.reset();  // Camera/ring reconfiguration cannot invalidate accepted captures.
        assert(queued->gpuImage(1));
        assert(images == 1);
        queued.reset();
        assert(images == 0 && memories == 0 && views == 0);
    }
    {
        RawImageRing ring(physical, device, 4, 4, 1);
        assert(ring.recordPush(command, 1, 1000, source, VK_IMAGE_LAYOUT_GENERAL));
        assert(!ring.recordPush(command, 2, 2000, source, VK_IMAGE_LAYOUT_GENERAL));
        ring.discardUnsubmitted(1);
        assert(ring.frameCount() == 0);
        assert(ring.snapshot()->refs().empty());
        assert(ring.recordPush(command, 2, 2000, source, VK_IMAGE_LAYOUT_GENERAL));
        ring.markReady(2);
        auto accepted = ring.snapshot();
        ring.discardUnsubmitted(2);  // A retired/pinned capture cannot be discarded.
        assert(accepted->gpuImage(2) && ring.frameCount() == 1);
    }
    assert(images == 0 && memories == 0 && views == 0);
    failAllocation = allocations + 2;
    try {
        RawImageRing ring(physical, device, 4, 4, 3);
        assert(false);
    } catch (const std::runtime_error&) {
    }
    assert(images == 0 && memories == 0 && views == 0);
    std::cout << "RAW_IMAGE_RING_QUEUE_TEST_PASS\n";
}
