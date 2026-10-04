#pragma once

#include "raw_sharpness/raw_sharpness_types.hpp"

#include <vulkan/vulkan.h>

#include <cstdint>
#include <functional>
#include <vector>

namespace raw_sharpness {

// Borrowed view of one burst frame. Images stay in GENERAL layout with prior
// writes visible; the scorer only reads and never transitions layouts.
struct SharpnessFrame {
    VkImage image = VK_NULL_HANDLE;
    VkImageView view = VK_NULL_HANDLE;
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    BayerPattern pattern = BayerPattern::RGGB;
    float whiteLevel = 65535.0f;
};

struct RawSharpnessCreateInfo {
    VkPhysicalDevice physicalDevice = VK_NULL_HANDLE;
    VkDevice device = VK_NULL_HANDLE;
    std::uint32_t queueFamily = 0;
};

// Production sharpest-reference scorer: green-channel Laplacian variance per
// frame, computed on the GPU. One command buffer, one submit, one fence per
// score() call; the only host transfer is the small partial buffer, never
// the bulk RAW payloads. Single-threaded use; not thread-safe.
class RawSharpness {
   public:
    using Submit = std::function<void(const VkSubmitInfo&, VkFence)>;

    static constexpr std::uint32_t kMaxFrames = 30;
    static constexpr std::uint32_t kWorkgroups = 64;

    RawSharpness() = default;
    ~RawSharpness() { reset(); }
    RawSharpness(const RawSharpness&) = delete;
    RawSharpness& operator=(const RawSharpness&) = delete;

    void initialize(const RawSharpnessCreateInfo& info, Submit submit);
    [[nodiscard]] bool ready() const noexcept { return device_ != VK_NULL_HANDLE; }

    // Mean-normalized Laplacian variance per frame, index-aligned with
    // `frames`. Throws on Vulkan failure or invalid input; the caller keeps
    // its fallback reference.
    std::vector<float> score(const std::vector<SharpnessFrame>& frames);

    void reset() noexcept;

   private:
    std::uint32_t memoryType(std::uint32_t bits, VkMemoryPropertyFlags flags) const;

    VkPhysicalDevice physical_ = VK_NULL_HANDLE;
    VkDevice device_ = VK_NULL_HANDLE;
    std::uint32_t queueFamily_ = 0;
    Submit submit_{};
    VkDescriptorSetLayout descriptorLayout_ = VK_NULL_HANDLE;
    VkPipelineLayout pipelineLayout_ = VK_NULL_HANDLE;
    VkPipeline pipeline_ = VK_NULL_HANDLE;
    VkDescriptorPool descriptorPool_ = VK_NULL_HANDLE;
    VkDescriptorSet descriptorSets_[kMaxFrames]{};
    VkCommandPool commandPool_ = VK_NULL_HANDLE;
    VkCommandBuffer command_ = VK_NULL_HANDLE;
    VkFence fence_ = VK_NULL_HANDLE;
    VkBuffer partials_ = VK_NULL_HANDLE;
    VkDeviceMemory partialsMemory_ = VK_NULL_HANDLE;
    void* mapped_ = nullptr;
};

}  // namespace raw_sharpness
