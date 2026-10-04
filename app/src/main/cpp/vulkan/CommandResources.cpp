#include "vulkan/CommandResources.h"

#include <stdexcept>
#include <utility>
namespace rawrcam::vulkan {
CommandResources::~CommandResources() { reset(); }
CommandResources::CommandResources(CommandResources&& other) noexcept { *this = std::move(other); }
CommandResources& CommandResources::operator=(CommandResources&& other) noexcept {
    if (this != &other) {
        reset();
        device_ = std::exchange(other.device_, VK_NULL_HANDLE);
        pool_ = std::exchange(other.pool_, VK_NULL_HANDLE);
        buffers_ = std::move(other.buffers_);
    }
    return *this;
}
void CommandResources::create(VkDevice device, uint32_t family, uint32_t count) {
    reset();
    device_ = device;
    VkCommandPoolCreateInfo pool{};
    pool.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    pool.queueFamilyIndex = family;
    pool.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    if (vkCreateCommandPool(device_, &pool, nullptr, &pool_) != VK_SUCCESS)
        throw std::runtime_error("create command pool failed");
    buffers_.resize(count);
    VkCommandBufferAllocateInfo allocation{};
    allocation.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    allocation.commandPool = pool_;
    allocation.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocation.commandBufferCount = count;
    if (vkAllocateCommandBuffers(device_, &allocation, buffers_.data()) != VK_SUCCESS)
        throw std::runtime_error("allocate command buffers failed");
}
void CommandResources::reset() noexcept {
    if (device_ && pool_) vkDestroyCommandPool(device_, pool_, nullptr);
    buffers_.clear();
    pool_ = VK_NULL_HANDLE;
    device_ = VK_NULL_HANDLE;
}
}  // namespace rawrcam::vulkan
