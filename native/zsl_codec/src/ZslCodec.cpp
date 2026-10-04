#include "rawr/zsl_codec/ZslCodec.h"

#include <array>
#include <cstring>
#include <stdexcept>
#include <string>

#include "raw_zsl_codec_analyze.h"
#include "raw_zsl_codec_pack.h"
#include "raw_zsl_codec_verify.h"

namespace rawr::zsl_codec {
namespace {
void check(VkResult result, const char* operation) {
    if (result != VK_SUCCESS) throw std::runtime_error(std::string(operation) + " VkResult=" + std::to_string(result));
}
}  // namespace

std::uint32_t VulkanCodec::memoryType(std::uint32_t bits, VkMemoryPropertyFlags flags) const {
    VkPhysicalDeviceMemoryProperties properties{};
    vkGetPhysicalDeviceMemoryProperties(physical_, &properties);
    for (std::uint32_t i = 0; i < properties.memoryTypeCount; ++i) {
        if ((bits & (1u << i)) && (properties.memoryTypes[i].propertyFlags & flags) == flags) return i;
    }
    throw std::runtime_error("zsl_codec: no matching host coherent memory type");
}

VulkanCodec::Buffer VulkanCodec::makeBuffer(VkDeviceSize bytes, VkBufferUsageFlags usage,
                                            VkMemoryPropertyFlags properties, bool map) {
    Buffer buffer{};
    buffer.bytes = bytes;
    VkBufferCreateInfo create{};
    create.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    create.size = bytes;
    create.usage = usage;
    create.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    check(vkCreateBuffer(device_, &create, nullptr, &buffer.buffer), "zsl_codec buffer");
    VkMemoryRequirements requirements{};
    vkGetBufferMemoryRequirements(device_, buffer.buffer, &requirements);
    VkMemoryAllocateInfo allocation{};
    allocation.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocation.allocationSize = requirements.size;
    allocation.memoryTypeIndex = memoryType(requirements.memoryTypeBits, properties);
    check(vkAllocateMemory(device_, &allocation, nullptr, &buffer.memory), "zsl_codec memory");
    check(vkBindBufferMemory(device_, buffer.buffer, buffer.memory, 0), "zsl_codec bind");
    if (map) {
        check(vkMapMemory(device_, buffer.memory, 0, VK_WHOLE_SIZE, 0, &buffer.mapped), "zsl_codec map");
        std::memset(buffer.mapped, 0, static_cast<std::size_t>(bytes));
    }
    return buffer;
}

void VulkanCodec::destroyBuffer(Buffer& buffer) noexcept {
    if (!device_) return;
    if (buffer.mapped) vkUnmapMemory(device_, buffer.memory);
    if (buffer.buffer) vkDestroyBuffer(device_, buffer.buffer, nullptr);
    if (buffer.memory) vkFreeMemory(device_, buffer.memory, nullptr);
    buffer = {};
}

VkShaderModule VulkanCodec::module(const unsigned char* data, std::size_t size) const {
    VkShaderModuleCreateInfo create{};
    create.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    create.codeSize = size;
    create.pCode = reinterpret_cast<const std::uint32_t*>(data);
    VkShaderModule shader{};
    check(vkCreateShaderModule(device_, &create, nullptr, &shader), "zsl_codec shader module");
    return shader;
}

VkPipeline VulkanCodec::makePipeline(const unsigned char* data, std::size_t size) {
    VkShaderModule shader = module(data, size);
    VkPipelineShaderStageCreateInfo stage{};
    stage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stage.stage = VK_SHADER_STAGE_COMPUTE_BIT;
    stage.module = shader;
    stage.pName = "main";
    VkComputePipelineCreateInfo create{};
    create.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
    create.stage = stage;
    create.layout = layout_;
    VkPipeline pipeline{};
    const VkResult result = vkCreateComputePipelines(device_, VK_NULL_HANDLE, 1, &create, nullptr, &pipeline);
    vkDestroyShaderModule(device_, shader, nullptr);
    check(result, "zsl_codec pipeline");
    return pipeline;
}

void VulkanCodec::initialize(VkPhysicalDevice physical, VkDevice device, float timestampPeriod, std::uint32_t width,
                             std::uint32_t height, const std::vector<VkImageView>& rawViews, bool verifyEveryFrame) {
    reset();
    if (rawViews.empty()) throw std::invalid_argument("zsl_codec: rawViews must not be empty");
    physical_ = physical;
    device_ = device;
    timestampPeriod_ = timestampPeriod;
    width_ = width;
    height_ = height;
    tilesX_ = (width + 31u) / 32u;
    tilesY_ = (height + 31u) / 32u;
    streams_ = tilesX_ * tilesY_ * 4u;
    verifyEveryFrame_ = verifyEveryFrame;
    // Analyze/pack dispatch one workgroup per tile along X only.
    VkPhysicalDeviceProperties properties{};
    vkGetPhysicalDeviceProperties(physical, &properties);
    if (static_cast<std::uint64_t>(tilesX_) * tilesY_ > properties.limits.maxComputeWorkGroupCount[0])
        throw std::invalid_argument("zsl_codec: frame has more 32x32 tiles than maxComputeWorkGroupCount[0]");

    std::array<VkDescriptorSetLayoutBinding, 7> bindings{};
    bindings[0] = {0, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr};
    for (std::uint32_t i = 1; i < 7; ++i)
        bindings[i] = {i, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr};
    VkDescriptorSetLayoutCreateInfo descriptorLayout{};
    descriptorLayout.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    descriptorLayout.bindingCount = static_cast<std::uint32_t>(bindings.size());
    descriptorLayout.pBindings = bindings.data();
    check(vkCreateDescriptorSetLayout(device_, &descriptorLayout, nullptr, &dsl_), "zsl_codec descriptor layout");

    VkPushConstantRange pushRange{VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(Push)};
    VkPipelineLayoutCreateInfo pipelineLayout{};
    pipelineLayout.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pipelineLayout.setLayoutCount = 1;
    pipelineLayout.pSetLayouts = &dsl_;
    pipelineLayout.pushConstantRangeCount = 1;
    pipelineLayout.pPushConstantRanges = &pushRange;
    check(vkCreatePipelineLayout(device_, &pipelineLayout, nullptr, &layout_), "zsl_codec pipeline layout");
    analyze_ = makePipeline(raw_zsl_codec_analyze_spv, raw_zsl_codec_analyze_spv_size);
    pack_ = makePipeline(raw_zsl_codec_pack_spv, raw_zsl_codec_pack_spv_size);
    verify_ = makePipeline(raw_zsl_codec_verify_spv, raw_zsl_codec_verify_spv_size);

    std::array<VkDescriptorPoolSize, 2> poolSizes{
        {{VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, static_cast<std::uint32_t>(rawViews.size())},
         {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, static_cast<std::uint32_t>(rawViews.size()) * 6u}}};
    VkDescriptorPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    poolInfo.maxSets = static_cast<std::uint32_t>(rawViews.size());
    poolInfo.poolSizeCount = static_cast<std::uint32_t>(poolSizes.size());
    poolInfo.pPoolSizes = poolSizes.data();
    check(vkCreateDescriptorPool(device_, &poolInfo, nullptr, &pool_), "zsl_codec descriptor pool");

    sets_.resize(rawViews.size());
    std::vector<VkDescriptorSetLayout> layouts(rawViews.size(), dsl_);
    VkDescriptorSetAllocateInfo setInfo{};
    setInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    setInfo.descriptorPool = pool_;
    setInfo.descriptorSetCount = static_cast<std::uint32_t>(layouts.size());
    setInfo.pSetLayouts = layouts.data();
    check(vkAllocateDescriptorSets(device_, &setInfo, sets_.data()), "zsl_codec descriptor sets");

    slots_.resize(rawViews.size());
    for (std::uint32_t i = 0; i < slots_.size(); ++i) {
        auto& slot = slots_[i];
        constexpr VkMemoryPropertyFlags host =
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
        slot.meta = makeBuffer(static_cast<VkDeviceSize>(streams_) * 4u,
                               VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT, host, true);
        slot.sizes = makeBuffer(static_cast<VkDeviceSize>(streams_) * 4u,
                                VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT, host, true);
        // The large compressed payload no longer needs CPU mapping in the live path.
        slot.payload = makeBuffer(static_cast<VkDeviceSize>(streams_) * kSlotWords * 4u,
                                  VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                                  VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, false);
        slot.verify =
            makeBuffer(16u, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT, host, true);
        slot.offsets = makeBuffer(static_cast<VkDeviceSize>(streams_) * 4u,
                                  VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT, host, true);
        slot.allocator =
            makeBuffer(4u, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT, host, true);
        VkQueryPoolCreateInfo queryInfo{};
        queryInfo.sType = VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO;
        queryInfo.queryType = VK_QUERY_TYPE_TIMESTAMP;
        queryInfo.queryCount = 3;
        check(vkCreateQueryPool(device_, &queryInfo, nullptr, &slot.query), "zsl_codec query pool");

        VkDescriptorImageInfo imageInfo{VK_NULL_HANDLE, rawViews[i], VK_IMAGE_LAYOUT_GENERAL};
        VkDescriptorBufferInfo infos[6] = {
            {slot.meta.buffer, 0, VK_WHOLE_SIZE},    {slot.sizes.buffer, 0, VK_WHOLE_SIZE},
            {slot.payload.buffer, 0, VK_WHOLE_SIZE}, {slot.verify.buffer, 0, VK_WHOLE_SIZE},
            {slot.offsets.buffer, 0, VK_WHOLE_SIZE}, {slot.allocator.buffer, 0, VK_WHOLE_SIZE}};
        std::array<VkWriteDescriptorSet, 7> writes{};
        writes[0] = {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr,    sets_[i], 0,      0, 1,
                     VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,       &imageInfo, nullptr,  nullptr};
        for (std::uint32_t j = 0; j < 6; ++j)
            writes[j + 1] = {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, sets_[i],  j + 1,  0, 1,
                             VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,      nullptr, &infos[j], nullptr};
        vkUpdateDescriptorSets(device_, static_cast<std::uint32_t>(writes.size()), writes.data(), 0, nullptr);
    }
}

std::optional<FrameResult> VulkanCodec::poll(std::uint32_t i) {
    if (i >= slots_.size()) throw std::out_of_range("zsl_codec: slot index");
    auto& slot = slots_[i];
    if (!slot.pending) return std::nullopt;
    std::uint64_t timestamps[3]{};
    if (vkGetQueryPoolResults(device_, slot.query, 0, 3, sizeof(timestamps), timestamps, sizeof(std::uint64_t),
                              VK_QUERY_RESULT_64_BIT) != VK_SUCCESS)
        return std::nullopt;

    const auto* sizes = static_cast<const std::uint32_t*>(slot.sizes.mapped);
    std::uint64_t words = 0;
    for (std::uint32_t n = 0; n < streams_; ++n) words += sizes[n];
    const std::uint64_t gpuWords = *static_cast<const std::uint32_t*>(slot.allocator.mapped);

    FrameResult result{};
    result.timestampNs = slot.timestampNs;
    result.width = width_;
    result.height = height_;
    result.tilesX = tilesX_;
    result.tilesY = tilesY_;
    result.rawBytes = static_cast<std::uint64_t>(width_) * height_ * 2u;
    result.payloadUsedBytes = gpuWords * 4u;
    result.payloadCapacityBytes = slot.payload.bytes;
    const std::uint64_t tableBytes = static_cast<std::uint64_t>(streams_) * sizeof(std::uint32_t);
    result.compactBytes = 56u + tableBytes * 2u + result.payloadUsedBytes;
    result.wordCountPass = gpuWords == words && result.payloadUsedBytes <= result.payloadCapacityBytes;
    result.analyzeMs = (timestamps[1] - timestamps[0]) * timestampPeriod_ / 1e6;
    result.packMs = (timestamps[2] - timestamps[1]) * timestampPeriod_ / 1e6;
    result.verified = verifyEveryFrame_;
    if (verifyEveryFrame_) {
        const auto* verify = static_cast<const std::uint32_t*>(slot.verify.mapped);
        result.mismatches = verify[0];
        result.firstX = verify[1];
        result.firstY = verify[2];
        result.maxAbs = verify[3];
    }
    result.gpuPacket = GpuPacketSource{slot.meta.buffer, slot.sizes.buffer, slot.offsets.buffer,    slot.payload.buffer,
                                       streams_,         tableBytes,        result.payloadUsedBytes};
    slot.pending = false;
    return result;
}

std::optional<FrameResult> VulkanCodec::record(VkCommandBuffer command, std::uint32_t i, std::uint64_t timestampNs) {
    auto completed = poll(i);
    auto& slot = slots_.at(i);
    const Push push{width_, height_, tilesX_, tilesY_, kSlotWords};
    vkCmdResetQueryPool(command, slot.query, 0, 3);
    if (verifyEveryFrame_) vkCmdFillBuffer(command, slot.verify.buffer, 0, 16, 0);
    vkCmdFillBuffer(command, slot.allocator.buffer, 0, 4, 0);
    VkBufferMemoryBarrier fill[2]{};
    std::uint32_t fillCount = 0;
    if (verifyEveryFrame_) {
        fill[fillCount].sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
        fill[fillCount].srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        fill[fillCount].dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
        fill[fillCount].srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        fill[fillCount].dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        fill[fillCount].buffer = slot.verify.buffer;
        fill[fillCount].offset = 0;
        fill[fillCount].size = 16;
        ++fillCount;
    }
    fill[fillCount].sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
    fill[fillCount].srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    fill[fillCount].dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
    fill[fillCount].srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    fill[fillCount].dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    fill[fillCount].buffer = slot.allocator.buffer;
    fill[fillCount].offset = 0;
    fill[fillCount].size = 4;
    ++fillCount;
    vkCmdPipelineBarrier(command, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 0, nullptr,
                         fillCount, fill, 0, nullptr);

    vkCmdWriteTimestamp(command, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, slot.query, 0);
    vkCmdBindPipeline(command, VK_PIPELINE_BIND_POINT_COMPUTE, analyze_);
    vkCmdBindDescriptorSets(command, VK_PIPELINE_BIND_POINT_COMPUTE, layout_, 0, 1, &sets_[i], 0, nullptr);
    vkCmdPushConstants(command, layout_, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(push), &push);
    vkCmdDispatch(command, tilesX_ * tilesY_, 1, 1);
    VkBufferMemoryBarrier analyzeBarriers[2]{};
    for (auto& barrier : analyzeBarriers) {
        barrier.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
        barrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
        barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
        barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.offset = 0;
        barrier.size = VK_WHOLE_SIZE;
    }
    analyzeBarriers[0].buffer = slot.meta.buffer;
    analyzeBarriers[1].buffer = slot.sizes.buffer;
    vkCmdPipelineBarrier(command, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 0,
                         nullptr, 2, analyzeBarriers, 0, nullptr);
    vkCmdWriteTimestamp(command, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, slot.query, 1);

    vkCmdBindPipeline(command, VK_PIPELINE_BIND_POINT_COMPUTE, pack_);
    vkCmdDispatch(command, tilesX_ * tilesY_, 1, 1);
    VkBufferMemoryBarrier packBarriers[3]{};
    for (auto& barrier : packBarriers) {
        barrier.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
        barrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
        barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
        barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.offset = 0;
        barrier.size = VK_WHOLE_SIZE;
    }
    packBarriers[0].buffer = slot.payload.buffer;
    packBarriers[1].buffer = slot.offsets.buffer;
    packBarriers[2].buffer = slot.allocator.buffer;
    vkCmdPipelineBarrier(command, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 0,
                         nullptr, 3, packBarriers, 0, nullptr);
    vkCmdWriteTimestamp(command, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, slot.query, 2);

    if (verifyEveryFrame_) {
        vkCmdBindPipeline(command, VK_PIPELINE_BIND_POINT_COMPUTE, verify_);
        vkCmdDispatch(command, (width_ + 15u) / 16u, (height_ + 15u) / 16u, 1);
    }
    slot.timestampNs = timestampNs;
    slot.pending = true;
    return completed;
}

void VulkanCodec::reset() noexcept {
    if (!device_) return;
    for (auto& slot : slots_) {
        if (slot.query) vkDestroyQueryPool(device_, slot.query, nullptr);
        destroyBuffer(slot.meta);
        destroyBuffer(slot.sizes);
        destroyBuffer(slot.payload);
        destroyBuffer(slot.verify);
        destroyBuffer(slot.offsets);
        destroyBuffer(slot.allocator);
    }
    slots_.clear();
    sets_.clear();
    if (pool_) vkDestroyDescriptorPool(device_, pool_, nullptr);
    if (analyze_) vkDestroyPipeline(device_, analyze_, nullptr);
    if (pack_) vkDestroyPipeline(device_, pack_, nullptr);
    if (verify_) vkDestroyPipeline(device_, verify_, nullptr);
    if (layout_) vkDestroyPipelineLayout(device_, layout_, nullptr);
    if (dsl_) vkDestroyDescriptorSetLayout(device_, dsl_, nullptr);
    pool_ = VK_NULL_HANDLE;
    analyze_ = VK_NULL_HANDLE;
    pack_ = VK_NULL_HANDLE;
    verify_ = VK_NULL_HANDLE;
    layout_ = VK_NULL_HANDLE;
    dsl_ = VK_NULL_HANDLE;
    device_ = VK_NULL_HANDLE;
    physical_ = VK_NULL_HANDLE;
    verifyEveryFrame_ = false;
}

}  // namespace rawr::zsl_codec
