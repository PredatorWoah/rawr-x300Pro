#pragma once
#include <vulkan/vulkan.h>

#include <mutex>
namespace rawrcam::vulkan {
// One mutex per actual VkQueue. Callers borrowing the mutex for backend adapters
// share this authority; commands are always allocated by their own resource owner.
class QueueSubmission final {
   public:
    QueueSubmission() = default;
    QueueSubmission(const QueueSubmission&) = delete;
    QueueSubmission& operator=(const QueueSubmission&) = delete;
    void bind(VkQueue queue) noexcept { queue_ = queue; }
    VkQueue handle() const noexcept { return queue_; }
    std::mutex& mutex() const noexcept { return mutex_; }
    VkResult submit(const VkSubmitInfo& info, VkFence fence) const {
        std::lock_guard<std::mutex> lock(mutex_);
        return vkQueueSubmit(queue_, 1, &info, fence);
    }
    VkResult present(const VkPresentInfoKHR& info) const {
        std::lock_guard<std::mutex> lock(mutex_);
        return vkQueuePresentKHR(queue_, &info);
    }
    VkResult waitIdle() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return vkQueueWaitIdle(queue_);
    }

   private:
    VkQueue queue_ = VK_NULL_HANDLE;
    mutable std::mutex mutex_;
};
}  // namespace rawrcam::vulkan
