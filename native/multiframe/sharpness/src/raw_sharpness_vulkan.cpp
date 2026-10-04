#include "raw_sharpness/raw_sharpness.hpp"

#include <cmath>
#include <limits>
#include <stdexcept>
#include <string>

#include "raw_sharpness_shaders_spv.hpp"

namespace raw_sharpness {
namespace {

void check(VkResult result, const char* operation) {
    if (result != VK_SUCCESS) {
        throw std::runtime_error(std::string(operation) + " VkResult=" + std::to_string(result));
    }
}

struct ScorePush {
    std::uint32_t width;
    std::uint32_t height;
    std::uint32_t cfa;
    float whiteLevel;
    std::uint32_t frameSlot;
    std::uint32_t workgroupCount;
};
static_assert(sizeof(ScorePush) == 24);

// NOTE: local size 256 is fixed in score_green_laplacian.comp; only the
// workgroup count is shared here because it sizes the partial buffer.
constexpr VkDeviceSize kPartialBytes =
    VkDeviceSize(RawSharpness::kMaxFrames) * RawSharpness::kWorkgroups * sizeof(float[4]);

}  // namespace

std::uint32_t RawSharpness::memoryType(std::uint32_t bits, VkMemoryPropertyFlags flags) const {
    VkPhysicalDeviceMemoryProperties properties{};
    vkGetPhysicalDeviceMemoryProperties(physical_, &properties);
    for (std::uint32_t i = 0; i < properties.memoryTypeCount; ++i) {
        if ((bits & (1u << i)) && (properties.memoryTypes[i].propertyFlags & flags) == flags) {
            return i;
        }
    }
    throw std::runtime_error("raw_sharpness: no matching memory type");
}

void RawSharpness::initialize(const RawSharpnessCreateInfo& info, Submit submit) {
    reset();
    if (!info.physicalDevice || !info.device || !submit) {
        throw std::invalid_argument("raw_sharpness: invalid initialize");
    }
    physical_ = info.physicalDevice;
    device_ = info.device;
    queueFamily_ = info.queueFamily;
    submit_ = std::move(submit);

    VkBufferCreateInfo bufferInfo{};
    bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufferInfo.size = kPartialBytes;
    bufferInfo.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
    bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    check(vkCreateBuffer(device_, &bufferInfo, nullptr, &partials_), "raw_sharpness create partial buffer");
    VkMemoryRequirements bufferRequirements{};
    vkGetBufferMemoryRequirements(device_, partials_, &bufferRequirements);
    VkMemoryAllocateInfo bufferAllocation{};
    bufferAllocation.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    bufferAllocation.allocationSize = bufferRequirements.size;
    bufferAllocation.memoryTypeIndex = memoryType(
        bufferRequirements.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    check(vkAllocateMemory(device_, &bufferAllocation, nullptr, &partialsMemory_),
          "raw_sharpness allocate partial buffer");
    check(vkBindBufferMemory(device_, partials_, partialsMemory_, 0), "raw_sharpness bind partial buffer");
    check(vkMapMemory(device_, partialsMemory_, 0, kPartialBytes, 0, &mapped_), "raw_sharpness map partials");

    VkDescriptorSetLayoutBinding bindings[2]{};
    bindings[0].binding = 0;
    bindings[0].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    bindings[0].descriptorCount = 1;
    bindings[0].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
    bindings[1].binding = 1;
    bindings[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    bindings[1].descriptorCount = 1;
    bindings[1].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
    VkDescriptorSetLayoutCreateInfo descriptorLayoutInfo{};
    descriptorLayoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    descriptorLayoutInfo.bindingCount = 2;
    descriptorLayoutInfo.pBindings = bindings;
    check(vkCreateDescriptorSetLayout(device_, &descriptorLayoutInfo, nullptr, &descriptorLayout_),
          "raw_sharpness descriptor layout");
    VkPushConstantRange pushRange{};
    pushRange.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
    pushRange.size = sizeof(ScorePush);
    VkPipelineLayoutCreateInfo pipelineLayoutInfo{};
    pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pipelineLayoutInfo.setLayoutCount = 1;
    pipelineLayoutInfo.pSetLayouts = &descriptorLayout_;
    pipelineLayoutInfo.pushConstantRangeCount = 1;
    pipelineLayoutInfo.pPushConstantRanges = &pushRange;
    check(vkCreatePipelineLayout(device_, &pipelineLayoutInfo, nullptr, &pipelineLayout_),
          "raw_sharpness pipeline layout");
    VkShaderModuleCreateInfo moduleInfo{};
    moduleInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    moduleInfo.codeSize = raw_sharpness_score_spv_size;
    moduleInfo.pCode = reinterpret_cast<const std::uint32_t*>(raw_sharpness_score_spv);
    VkShaderModule module = VK_NULL_HANDLE;
    check(vkCreateShaderModule(device_, &moduleInfo, nullptr, &module), "raw_sharpness shader module");
    VkPipelineShaderStageCreateInfo stage{};
    stage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stage.stage = VK_SHADER_STAGE_COMPUTE_BIT;
    stage.module = module;
    stage.pName = "main";
    VkComputePipelineCreateInfo pipelineInfo{};
    pipelineInfo.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
    pipelineInfo.stage = stage;
    pipelineInfo.layout = pipelineLayout_;
    const VkResult pipelineResult =
        vkCreateComputePipelines(device_, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &pipeline_);
    vkDestroyShaderModule(device_, module, nullptr);
    check(pipelineResult, "raw_sharpness compute pipeline");

    const VkDescriptorPoolSize poolSizes[] = {{VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, kMaxFrames},
                                              {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, kMaxFrames}};
    VkDescriptorPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    poolInfo.maxSets = kMaxFrames;
    poolInfo.poolSizeCount = 2;
    poolInfo.pPoolSizes = poolSizes;
    check(vkCreateDescriptorPool(device_, &poolInfo, nullptr, &descriptorPool_), "raw_sharpness descriptor pool");
    VkDescriptorSetAllocateInfo setInfo{};
    setInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    setInfo.descriptorPool = descriptorPool_;
    setInfo.descriptorSetCount = kMaxFrames;
    VkDescriptorSetLayout layouts[kMaxFrames];
    for (auto& layout : layouts) layout = descriptorLayout_;
    setInfo.pSetLayouts = layouts;
    check(vkAllocateDescriptorSets(device_, &setInfo, descriptorSets_), "raw_sharpness descriptor sets");

    VkCommandPoolCreateInfo commandPoolInfo{};
    commandPoolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    commandPoolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    commandPoolInfo.queueFamilyIndex = queueFamily_;
    check(vkCreateCommandPool(device_, &commandPoolInfo, nullptr, &commandPool_), "raw_sharpness command pool");
    VkCommandBufferAllocateInfo commandInfo{};
    commandInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    commandInfo.commandPool = commandPool_;
    commandInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    commandInfo.commandBufferCount = 1;
    check(vkAllocateCommandBuffers(device_, &commandInfo, &command_), "raw_sharpness command buffer");
    VkFenceCreateInfo fenceInfo{};
    fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    fenceInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;
    check(vkCreateFence(device_, &fenceInfo, nullptr, &fence_), "raw_sharpness fence");
}

std::vector<float> RawSharpness::score(const std::vector<SharpnessFrame>& frames) {
    if (!device_ || frames.size() < 2u || frames.size() > kMaxFrames) {
        throw std::invalid_argument("raw_sharpness: invalid burst");
    }
    const std::uint32_t width = frames.front().width;
    const std::uint32_t height = frames.front().height;
    if (width < 8u || height < 8u) {
        throw std::invalid_argument("raw_sharpness: invalid geometry");
    }
    for (const auto& frame : frames) {
        if (!frame.image || !frame.view || frame.width != width || frame.height != height) {
            throw std::invalid_argument("raw_sharpness: invalid frame");
        }
    }
    check(vkWaitForFences(device_, 1, &fence_, VK_TRUE, UINT64_MAX), "raw_sharpness prior fence");
    check(vkResetFences(device_, 1, &fence_), "raw_sharpness reset fence");
    check(vkResetCommandBuffer(command_, 0), "raw_sharpness reset command");
    VkCommandBufferBeginInfo begin{};
    begin.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    check(vkBeginCommandBuffer(command_, &begin), "raw_sharpness begin");

    // Source images rest in GENERAL with prior copies visible after this edge.
    std::vector<VkImageMemoryBarrier> imageBarriers;
    imageBarriers.reserve(frames.size());
    for (const auto& frame : frames) {
        VkImageMemoryBarrier barrier{};
        barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT | VK_ACCESS_SHADER_WRITE_BIT;
        barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
        barrier.oldLayout = VK_IMAGE_LAYOUT_GENERAL;
        barrier.newLayout = VK_IMAGE_LAYOUT_GENERAL;
        barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.image = frame.image;
        barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        barrier.subresourceRange.levelCount = 1;
        barrier.subresourceRange.layerCount = 1;
        imageBarriers.push_back(barrier);
    }
    vkCmdPipelineBarrier(command_, VK_PIPELINE_STAGE_TRANSFER_BIT | VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                         VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 0, nullptr, 0, nullptr,
                         static_cast<std::uint32_t>(imageBarriers.size()), imageBarriers.data());

    vkCmdBindPipeline(command_, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline_);
    for (std::uint32_t slot = 0; slot < frames.size(); ++slot) {
        VkDescriptorImageInfo imageInfo{};
        imageInfo.imageView = frames[slot].view;
        imageInfo.imageLayout = VK_IMAGE_LAYOUT_GENERAL;
        VkDescriptorBufferInfo bufferInfo{};
        bufferInfo.buffer = partials_;
        bufferInfo.offset = 0;
        bufferInfo.range = kPartialBytes;
        VkWriteDescriptorSet writes[2]{};
        writes[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[0].dstSet = descriptorSets_[slot];
        writes[0].dstBinding = 0;
        writes[0].descriptorCount = 1;
        writes[0].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
        writes[0].pImageInfo = &imageInfo;
        writes[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[1].dstSet = descriptorSets_[slot];
        writes[1].dstBinding = 1;
        writes[1].descriptorCount = 1;
        writes[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        writes[1].pBufferInfo = &bufferInfo;
        vkUpdateDescriptorSets(device_, 2, writes, 0, nullptr);
        vkCmdBindDescriptorSets(command_, VK_PIPELINE_BIND_POINT_COMPUTE, pipelineLayout_, 0, 1,
                                &descriptorSets_[slot], 0, nullptr);
        ScorePush push{};
        push.width = width;
        push.height = height;
        push.cfa = static_cast<std::uint32_t>(frames[slot].pattern);
        push.whiteLevel =
            std::isfinite(frames[slot].whiteLevel) && frames[slot].whiteLevel > 0.0f ? frames[slot].whiteLevel : 65535.0f;
        push.frameSlot = slot;
        push.workgroupCount = kWorkgroups;
        vkCmdPushConstants(command_, pipelineLayout_, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(push), &push);
        vkCmdDispatch(command_, kWorkgroups, 1, 1);
    }
    VkBufferMemoryBarrier hostBarrier{};
    hostBarrier.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
    hostBarrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
    hostBarrier.dstAccessMask = VK_ACCESS_HOST_READ_BIT;
    hostBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    hostBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    hostBarrier.buffer = partials_;
    hostBarrier.offset = 0;
    hostBarrier.size = kPartialBytes;
    vkCmdPipelineBarrier(command_, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_HOST_BIT, 0, 0, nullptr, 1,
                         &hostBarrier, 0, nullptr);
    check(vkEndCommandBuffer(command_), "raw_sharpness end");
    VkSubmitInfo submitInfo{};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &command_;
    submit_(submitInfo, fence_);
    check(vkWaitForFences(device_, 1, &fence_, VK_TRUE, UINT64_MAX), "raw_sharpness fence");

    const auto* partials = static_cast<const float*>(mapped_);
    std::vector<float> scores;
    scores.reserve(frames.size());
    for (std::uint32_t slot = 0; slot < frames.size(); ++slot) {
        double sum = 0.0, sumSq = 0.0, sigSum = 0.0, count = 0.0;
        const float* base = partials + (std::size_t(slot) * kWorkgroups * 4u);
        for (std::uint32_t wg = 0; wg < kWorkgroups; ++wg) {
            sum += base[wg * 4u + 0u];
            sumSq += base[wg * 4u + 1u];
            sigSum += base[wg * 4u + 2u];
            count += base[wg * 4u + 3u];
        }
        float score = 0.0f;
        if (count >= 1024.0) {
            const double mean = sum / count;
            double var = sumSq / count - mean * mean;
            const double sigMean = sigSum / count;
            if (var > 0.0 && sigMean > 1.0) {
                var /= (sigMean * sigMean);
                if (std::isfinite(var) && var > 0.0) score = static_cast<float>(var);
            }
        }
        scores.push_back(score);
    }
    return scores;
}

void RawSharpness::reset() noexcept {
    if (device_ && fence_) (void)vkWaitForFences(device_, 1, &fence_, VK_TRUE, UINT64_MAX);
    if (device_ && mapped_) vkUnmapMemory(device_, partialsMemory_);
    if (device_ && partials_) vkDestroyBuffer(device_, partials_, nullptr);
    if (device_ && partialsMemory_) vkFreeMemory(device_, partialsMemory_, nullptr);
    if (device_ && fence_) vkDestroyFence(device_, fence_, nullptr);
    if (device_ && commandPool_) vkDestroyCommandPool(device_, commandPool_, nullptr);
    if (device_ && descriptorPool_) vkDestroyDescriptorPool(device_, descriptorPool_, nullptr);
    if (device_ && pipeline_) vkDestroyPipeline(device_, pipeline_, nullptr);
    if (device_ && pipelineLayout_) vkDestroyPipelineLayout(device_, pipelineLayout_, nullptr);
    if (device_ && descriptorLayout_) vkDestroyDescriptorSetLayout(device_, descriptorLayout_, nullptr);
    physical_ = VK_NULL_HANDLE;
    device_ = VK_NULL_HANDLE;
    queueFamily_ = 0;
    submit_ = {};
    descriptorLayout_ = VK_NULL_HANDLE;
    pipelineLayout_ = VK_NULL_HANDLE;
    pipeline_ = VK_NULL_HANDLE;
    descriptorPool_ = VK_NULL_HANDLE;
    for (auto& set : descriptorSets_) set = VK_NULL_HANDLE;
    commandPool_ = VK_NULL_HANDLE;
    command_ = VK_NULL_HANDLE;
    fence_ = VK_NULL_HANDLE;
    partials_ = VK_NULL_HANDLE;
    partialsMemory_ = VK_NULL_HANDLE;
    mapped_ = nullptr;
}

}  // namespace raw_sharpness
