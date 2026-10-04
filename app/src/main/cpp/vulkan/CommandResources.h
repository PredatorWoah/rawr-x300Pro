#pragma once
#include <vulkan/vulkan.h>

#include <cstdint>
#include <vector>
namespace rawrcam::vulkan {
class CommandResources final {
   public:
    CommandResources() = default;
    ~CommandResources();
    CommandResources(const CommandResources&) = delete;
    CommandResources& operator=(const CommandResources&) = delete;
    CommandResources(CommandResources&& other) noexcept;
    CommandResources& operator=(CommandResources&& other) noexcept;
    void create(VkDevice device, uint32_t queueFamily, uint32_t count);
    void reset() noexcept;
    VkCommandPool pool() const noexcept { return pool_; }
    VkCommandBuffer command(uint32_t index = 0) const { return buffers_.at(index); }

   private:
    VkDevice device_ = VK_NULL_HANDLE;
    VkCommandPool pool_ = VK_NULL_HANDLE;
    std::vector<VkCommandBuffer> buffers_;
};
}  // namespace rawrcam::vulkan
