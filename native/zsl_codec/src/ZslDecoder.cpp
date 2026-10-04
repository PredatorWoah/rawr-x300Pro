#include "rawr/zsl_codec/ZslDecoder.h"

#include <limits>
#include <stdexcept>
#include <string>

#include "raw_zsl_codec_decode.h"

namespace rawr::zsl_codec {
namespace {
void check(VkResult r, const char* op) {
    if (r != VK_SUCCESS) throw std::runtime_error(std::string(op) + " VkResult=" + std::to_string(r));
}
VkShaderModule module(VkDevice device, const unsigned char* bytes, std::size_t size) {
    VkShaderModuleCreateInfo ci{};
    ci.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    ci.codeSize = size;
    ci.pCode = reinterpret_cast<const std::uint32_t*>(bytes);
    VkShaderModule m = VK_NULL_HANDLE;
    check(vkCreateShaderModule(device, &ci, nullptr, &m), "zsl decode shader module");
    return m;
}
struct Push {
    std::uint32_t width, height, tilesX, tilesY, streams;
};
}  // namespace

void VulkanDecoder::initialize(VkDevice device) {
    reset();
    if (!device) throw std::invalid_argument("zsl decoder: null device");
    device_ = device;
    VkDescriptorSetLayoutBinding bindings[2]{};
    bindings[0].binding = 0;
    bindings[0].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    bindings[0].descriptorCount = 1;
    bindings[0].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
    bindings[1].binding = 1;
    bindings[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    bindings[1].descriptorCount = 1;
    bindings[1].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
    VkDescriptorSetLayoutCreateInfo di{};
    di.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    di.bindingCount = 2;
    di.pBindings = bindings;
    check(vkCreateDescriptorSetLayout(device_, &di, nullptr, &dsl_), "zsl decode descriptor layout");
    VkPushConstantRange range{VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(Push)};
    VkPipelineLayoutCreateInfo li{};
    li.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    li.setLayoutCount = 1;
    li.pSetLayouts = &dsl_;
    li.pushConstantRangeCount = 1;
    li.pPushConstantRanges = &range;
    check(vkCreatePipelineLayout(device_, &li, nullptr, &layout_), "zsl decode pipeline layout");
    VkShaderModule sm = module(device_, raw_zsl_codec_decode_spv, raw_zsl_codec_decode_spv_size);
    VkPipelineShaderStageCreateInfo stage{};
    stage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stage.stage = VK_SHADER_STAGE_COMPUTE_BIT;
    stage.module = sm;
    stage.pName = "main";
    VkComputePipelineCreateInfo pi{};
    pi.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
    pi.stage = stage;
    pi.layout = layout_;
    const VkResult pr = vkCreateComputePipelines(device_, VK_NULL_HANDLE, 1, &pi, nullptr, &pipeline_);
    vkDestroyShaderModule(device_, sm, nullptr);
    check(pr, "zsl decode pipeline");
    constexpr std::uint32_t kSetsPerBatch = 64;
    VkDescriptorPoolSize sizes[2]{{VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, kSetsPerBatch},
                                  {VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, kSetsPerBatch}};
    VkDescriptorPoolCreateInfo pci{};
    pci.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    pci.maxSets = kSetsPerBatch;
    pci.poolSizeCount = 2;
    pci.pPoolSizes = sizes;
    check(vkCreateDescriptorPool(device_, &pci, nullptr, &pool_), "zsl decode descriptor pool");
}

void VulkanDecoder::beginBatch() {
    if (!device_ || !pool_) throw std::logic_error("zsl decoder: not initialized");
    check(vkResetDescriptorPool(device_, pool_, 0), "zsl decode reset descriptor pool");
}

void VulkanDecoder::record(VkCommandBuffer command, const DecodePacket& p, VkImageView dst) {
    if (!device_ || !pipeline_ || !command || !dst || !p.buffer)
        throw std::invalid_argument("zsl decoder: invalid record arguments");
    if (!p.width || !p.height || !p.tilesX || !p.tilesY || !p.streams)
        throw std::invalid_argument("zsl decoder: invalid packet geometry");
    const std::uint64_t expectedTable = static_cast<std::uint64_t>(p.streams) * sizeof(std::uint32_t);
    if (p.tableBytes != expectedTable) throw std::invalid_argument("zsl decoder: unexpected table size");
    const std::uint64_t expected = 3u * p.tableBytes + p.payloadBytes;
    if (expected != p.packedBytes || p.packedBytes > std::numeric_limits<VkDeviceSize>::max())
        throw std::invalid_argument("zsl decoder: invalid logical packet size");
    VkDescriptorSet set = VK_NULL_HANDLE;
    VkDescriptorSetAllocateInfo ai{};
    ai.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    ai.descriptorPool = pool_;
    ai.descriptorSetCount = 1;
    ai.pSetLayouts = &dsl_;
    check(vkAllocateDescriptorSets(device_, &ai, &set), "zsl decode transient descriptor set");
    VkDescriptorBufferInfo bi{p.buffer, 0, static_cast<VkDeviceSize>(p.packedBytes)};
    VkDescriptorImageInfo ii{};
    ii.imageView = dst;
    ii.imageLayout = VK_IMAGE_LAYOUT_GENERAL;
    VkWriteDescriptorSet writes[2]{};
    writes[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    writes[0].dstSet = set;
    writes[0].dstBinding = 0;
    writes[0].descriptorCount = 1;
    writes[0].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    writes[0].pBufferInfo = &bi;
    writes[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    writes[1].dstSet = set;
    writes[1].dstBinding = 1;
    writes[1].descriptorCount = 1;
    writes[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    writes[1].pImageInfo = &ii;
    vkUpdateDescriptorSets(device_, 2, writes, 0, nullptr);
    const Push push{p.width, p.height, p.tilesX, p.tilesY, p.streams};
    vkCmdBindPipeline(command, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline_);
    vkCmdBindDescriptorSets(command, VK_PIPELINE_BIND_POINT_COMPUTE, layout_, 0, 1, &set, 0, nullptr);
    vkCmdPushConstants(command, layout_, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(push), &push);
    vkCmdDispatch(command, (p.width + 15u) / 16u, (p.height + 15u) / 16u, 1);
}

void VulkanDecoder::reset() noexcept {
    if (device_) {
        if (pool_) vkDestroyDescriptorPool(device_, pool_, nullptr);
        if (pipeline_) vkDestroyPipeline(device_, pipeline_, nullptr);
        if (layout_) vkDestroyPipelineLayout(device_, layout_, nullptr);
        if (dsl_) vkDestroyDescriptorSetLayout(device_, dsl_, nullptr);
    }
    device_ = VK_NULL_HANDLE;
    dsl_ = VK_NULL_HANDLE;
    layout_ = VK_NULL_HANDLE;
    pipeline_ = VK_NULL_HANDLE;
    pool_ = VK_NULL_HANDLE;
}

}  // namespace rawr::zsl_codec
