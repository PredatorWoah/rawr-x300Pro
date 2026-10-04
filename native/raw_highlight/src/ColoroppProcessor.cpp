#include "raw_highlight/ColoroppProcessor.hpp"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <stdexcept>
#include <string>
#include "raw_highlight/Coloropp.hpp"

namespace rawr::highlight {
namespace {
constexpr VkDeviceSize kMaxShadingBytes = 256u * 256u * sizeof(float) * 4u;
void ck(VkResult r, const char* what) {
    if (r != VK_SUCCESS) throw std::runtime_error(std::string(what) + " VkResult=" + std::to_string(r));
}
uint32_t memoryType(VkPhysicalDevice pd, uint32_t bits, VkMemoryPropertyFlags flags) {
    VkPhysicalDeviceMemoryProperties mp{};
    vkGetPhysicalDeviceMemoryProperties(pd, &mp);
    for (uint32_t i = 0; i < mp.memoryTypeCount; ++i)
        if ((bits & (1u << i)) && (mp.memoryTypes[i].propertyFlags & flags) == flags) return i;
    throw std::runtime_error("Coloropp has no host coherent shading memory");
}
VkImageMemoryBarrier imageBarrier(VkImage image, VkAccessFlags src, VkAccessFlags dst, bool first) {
    VkImageMemoryBarrier b{};
    b.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    b.srcAccessMask = src;
    b.dstAccessMask = dst;
    b.oldLayout = first ? VK_IMAGE_LAYOUT_UNDEFINED : VK_IMAGE_LAYOUT_GENERAL;
    b.newLayout = VK_IMAGE_LAYOUT_GENERAL;
    b.srcQueueFamilyIndex = b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    b.image = image;
    b.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    return b;
}
void makePipeline(VkDevice device, uint32_t bindingCount, const VkDescriptorType* types,
                  SpirvWords spirv, uint32_t pushBytes,
                  VkDescriptorSetLayout& dsl, VkPipelineLayout& layout, VkPipeline& pipeline,
                  VkDescriptorPool& pool, VkDescriptorSet& set) {
    VkDescriptorSetLayoutBinding bindings[3]{};
    for (uint32_t i = 0; i < bindingCount; ++i) {
        bindings[i] = {i, types[i], 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr};
    }
    VkDescriptorSetLayoutCreateInfo ds{}; ds.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    ds.bindingCount = bindingCount;
    ds.pBindings = bindings;
    ck(vkCreateDescriptorSetLayout(device, &ds, nullptr, &dsl), "Coloropp descriptor layout");
    VkPushConstantRange push{VK_SHADER_STAGE_COMPUTE_BIT, 0, pushBytes};
    VkPipelineLayoutCreateInfo pl{}; pl.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pl.setLayoutCount = 1; pl.pSetLayouts = &dsl;
    pl.pushConstantRangeCount = 1; pl.pPushConstantRanges = &push;
    ck(vkCreatePipelineLayout(device, &pl, nullptr, &layout), "Coloropp pipeline layout");
    VkShaderModuleCreateInfo sm{}; sm.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    if (!spirv.words || !spirv.count) throw std::invalid_argument("Coloropp: missing SPIR-V");
    sm.codeSize = spirv.count * sizeof(uint32_t);
    sm.pCode = spirv.words;
    VkShaderModule module = VK_NULL_HANDLE;
    ck(vkCreateShaderModule(device, &sm, nullptr, &module), "Coloropp shader module");
    VkPipelineShaderStageCreateInfo stage{}; stage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stage.stage = VK_SHADER_STAGE_COMPUTE_BIT;
    stage.module = module;
    stage.pName = "main";
    VkComputePipelineCreateInfo ci{}; ci.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
    ci.stage = stage; ci.layout = layout;
    const auto result = vkCreateComputePipelines(device, VK_NULL_HANDLE, 1, &ci, nullptr, &pipeline);
    vkDestroyShaderModule(device, module, nullptr);
    ck(result, "Coloropp compute pipeline");
    VkDescriptorPoolCreateInfo dp{}; dp.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    VkDescriptorPoolSize sizes[2]{{VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 2},
                                   {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1}};
    dp.maxSets = 1; dp.poolSizeCount = bindingCount == 3 ? 2u : 1u; dp.pPoolSizes = sizes;
    ck(vkCreateDescriptorPool(device, &dp, nullptr, &pool), "Coloropp descriptor pool");
    VkDescriptorSetAllocateInfo alloc{}; alloc.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    alloc.descriptorPool = pool; alloc.descriptorSetCount = 1; alloc.pSetLayouts = &dsl;
    ck(vkAllocateDescriptorSets(device, &alloc, &set), "Coloropp descriptor set");
}
}

ColoroppProcessor::ColoroppProcessor(VkPhysicalDevice pd, VkDevice device, const ColoroppShaders& shaders,
                                     uint32_t width, uint32_t height)
    : physicalDevice_(pd), device_(device), width_(width), height_(height) {
    try {
        // preWb_ and tone_ are allocated on first use: the fused WB path and a
        // caller that applies the tone tap itself need neither.
        VkBufferCreateInfo bi{}; bi.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        bi.size = kMaxShadingBytes; bi.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
        bi.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        ck(vkCreateBuffer(device_, &bi, nullptr, &shadingBuffer_), "Coloropp shading buffer");
        VkMemoryRequirements req{};
        vkGetBufferMemoryRequirements(device_, shadingBuffer_, &req);
        VkMemoryAllocateInfo ai{}; ai.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        ai.allocationSize = req.size;
        ai.memoryTypeIndex = memoryType(pd, req.memoryTypeBits,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
        ck(vkAllocateMemory(device_, &ai, nullptr, &shadingMemory_), "Coloropp shading allocation");
        ck(vkBindBufferMemory(device_, shadingBuffer_, shadingMemory_, 0), "Coloropp shading bind");
        ck(vkMapMemory(device_, shadingMemory_, 0, kMaxShadingBytes, 0, &shadingMapped_), "Coloropp shading map");
        const VkDescriptorType preTypes[3]{VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
            VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER};
        makePipeline(device_, 3, preTypes, shaders.coloropp, 68,
                     preDsl_, preLayout_, prePipeline_, prePool_, preSet_);
        const VkDescriptorType toneTypes[2]{VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE};
        makePipeline(device_, 2, toneTypes, shaders.tone, 16,
                     toneDsl_, toneLayout_, tonePipeline_, tonePool_, toneSet_);
        if (shaders.fusedWb.words && shaders.fusedWb.count)
            makePipeline(device_, 3, preTypes, shaders.fusedWb, sizeof(ColoroppFusedPush),
                         fusedDsl_, fusedLayout_, fusedPipeline_, fusedPool_, fusedSet_);
    } catch (...) { release(); throw; }
}
ColoroppProcessor::~ColoroppProcessor() { release(); }
void ColoroppProcessor::release() noexcept {
    if (!device_) return;
    if (fusedPool_) vkDestroyDescriptorPool(device_, fusedPool_, nullptr);
    if (fusedPipeline_) vkDestroyPipeline(device_, fusedPipeline_, nullptr);
    if (fusedLayout_) vkDestroyPipelineLayout(device_, fusedLayout_, nullptr);
    if (fusedDsl_) vkDestroyDescriptorSetLayout(device_, fusedDsl_, nullptr);
    if (tonePool_) vkDestroyDescriptorPool(device_, tonePool_, nullptr);
    if (prePool_) vkDestroyDescriptorPool(device_, prePool_, nullptr);
    if (tonePipeline_) vkDestroyPipeline(device_, tonePipeline_, nullptr);
    if (prePipeline_) vkDestroyPipeline(device_, prePipeline_, nullptr);
    if (toneLayout_) vkDestroyPipelineLayout(device_, toneLayout_, nullptr);
    if (preLayout_) vkDestroyPipelineLayout(device_, preLayout_, nullptr);
    if (toneDsl_) vkDestroyDescriptorSetLayout(device_, toneDsl_, nullptr);
    if (preDsl_) vkDestroyDescriptorSetLayout(device_, preDsl_, nullptr);
    if (shadingMapped_) vkUnmapMemory(device_, shadingMemory_);
    if (shadingBuffer_) vkDestroyBuffer(device_, shadingBuffer_, nullptr);
    if (shadingMemory_) vkFreeMemory(device_, shadingMemory_, nullptr);
    rawr::vk::destroyOwnedImage(device_, tone_);
    rawr::vk::destroyOwnedImage(device_, preWb_);
}
void ColoroppProcessor::recordPreWb(VkCommandBuffer cmd, VkImageView source, const std::array<float, 3>& wb,
                                    float threshold, const rawr::shading::LensShadingMapView& shading,
                                    uint32_t cfaPattern, ColoroppSensorGeometry geometry) {
    if (!preWbInitialized_) {
        preWb_ = rawr::vk::createOwnedImage(physicalDevice_, device_, width_, height_, VK_FORMAT_R16G16B16A16_SFLOAT,
                                            VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT);
        auto init = imageBarrier(preWb_.image, 0, VK_ACCESS_SHADER_WRITE_BIT, true);
        vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                             0, 0, nullptr, 0, nullptr, 1, &init);
        preWbInitialized_ = true;
    }
    const bool useGrid = uploadShading(cmd, shading);
    VkDescriptorImageInfo images[2]{{VK_NULL_HANDLE, source, VK_IMAGE_LAYOUT_GENERAL},
                                    {VK_NULL_HANDLE, preWb_.view, VK_IMAGE_LAYOUT_GENERAL}};
    VkDescriptorBufferInfo buffer{shadingBuffer_, 0, kMaxShadingBytes};
    VkWriteDescriptorSet writes[3]{};
    for (uint32_t i = 0; i < 3; ++i) {
        writes[i].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[i].dstSet = preSet_; writes[i].dstBinding = i; writes[i].descriptorCount = 1;
        writes[i].descriptorType = i == 2 ? VK_DESCRIPTOR_TYPE_STORAGE_BUFFER : VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
        if (i == 2) writes[i].pBufferInfo = &buffer; else writes[i].pImageInfo = &images[i];
    }
    vkUpdateDescriptorSets(device_, 3, writes, 0, nullptr);
    const ColoroppPush pc = push(wb, threshold, useGrid, shading, cfaPattern, geometry);
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, prePipeline_);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, preLayout_, 0, 1, &preSet_, 0, nullptr);
    vkCmdPushConstants(cmd, preLayout_, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(pc), &pc);
    vkCmdDispatch(cmd, (width_ + 15u) / 16u, (height_ + 15u) / 16u, 1);
    auto ready = imageBarrier(preWb_.image, VK_ACCESS_SHADER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT, false);
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                         0, 0, nullptr, 0, nullptr, 1, &ready);
}
bool ColoroppProcessor::uploadShading(VkCommandBuffer cmd, const rawr::shading::LensShadingMapView& shading) {
    const bool useGrid = shading.valid() && shading.count * sizeof(float) <= kMaxShadingBytes;
    if (useGrid) std::memcpy(shadingMapped_, shading.gains, shading.count * sizeof(float));
    else { const float ones[4]{1,1,1,1}; std::memcpy(shadingMapped_, ones, sizeof(ones)); }
    VkBufferMemoryBarrier hostReady{}; hostReady.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
    hostReady.srcAccessMask = VK_ACCESS_HOST_WRITE_BIT;
    hostReady.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    hostReady.srcQueueFamilyIndex = hostReady.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    hostReady.buffer = shadingBuffer_; hostReady.offset = 0; hostReady.size = VK_WHOLE_SIZE;
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_HOST_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                         0, 0, nullptr, 1, &hostReady, 0, nullptr);
    return useGrid;
}

ColoroppPush ColoroppProcessor::push(const std::array<float, 3>& wb, float threshold, bool useGrid,
                                     const rawr::shading::LensShadingMapView& shading, uint32_t cfaPattern,
                                     ColoroppSensorGeometry geometry) const {
    if (geometry.width == 0 || geometry.height == 0) geometry = {width_, height_, 0, 0, 1};
    return ColoroppPush{
        width_, height_, useGrid ? shading.width : 0u, useGrid ? shading.height : 0u,
        cfaPattern, coloroppClipValue(threshold), useGrid ? 1u : 0u, 0u,
        {coloroppWhiteBalance(wb[0]), coloroppWhiteBalance(wb[1]), coloroppWhiteBalance(wb[2]), 0.0f},
        geometry.width, geometry.height, geometry.cropX, geometry.cropY, geometry.scale};
}

void ColoroppProcessor::recordFused(VkCommandBuffer cmd, VkImageView source, VkImageView target,
                                    const std::array<float, 3>& wb, float threshold,
                                    const rawr::shading::LensShadingMapView& shading, uint32_t cfaPattern,
                                    ColoroppSensorGeometry geometry) {
    if (!fusedPipeline_) throw std::logic_error("Coloropp: fused WB shader was not provided");
    const bool useGrid = uploadShading(cmd, shading);
    VkDescriptorImageInfo images[2]{{VK_NULL_HANDLE, source, VK_IMAGE_LAYOUT_GENERAL},
                                    {VK_NULL_HANDLE, target, VK_IMAGE_LAYOUT_GENERAL}};
    VkDescriptorBufferInfo buffer{shadingBuffer_, 0, kMaxShadingBytes};
    VkWriteDescriptorSet writes[3]{};
    for (uint32_t i = 0; i < 3; ++i) {
        writes[i].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[i].dstSet = fusedSet_; writes[i].dstBinding = i; writes[i].descriptorCount = 1;
        writes[i].descriptorType = i == 2 ? VK_DESCRIPTOR_TYPE_STORAGE_BUFFER : VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
        if (i == 2) writes[i].pBufferInfo = &buffer; else writes[i].pImageInfo = &images[i];
    }
    vkUpdateDescriptorSets(device_, 3, writes, 0, nullptr);
    ColoroppFusedPush pc{};
    pc.base = push(wb, threshold, useGrid, shading, cfaPattern, geometry);
    for (int i = 0; i < 3; ++i) pc.gains[i] = wb[static_cast<size_t>(i)];
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, fusedPipeline_);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, fusedLayout_, 0, 1, &fusedSet_, 0, nullptr);
    vkCmdPushConstants(cmd, fusedLayout_, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(pc), &pc);
    vkCmdDispatch(cmd, (width_ + 15u) / 16u, (height_ + 15u) / 16u, 1);
}

void ColoroppProcessor::recordTone(VkCommandBuffer cmd, VkImageView source, float compression, float exposureGain) {
    if (!toneInitialized_) {
        tone_ = rawr::vk::createOwnedImage(physicalDevice_, device_, width_, height_, VK_FORMAT_R16G16B16A16_SFLOAT,
                                           VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT);
        auto init = imageBarrier(tone_.image, 0, VK_ACCESS_SHADER_WRITE_BIT, true);
        vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                             0, 0, nullptr, 0, nullptr, 1, &init);
        toneInitialized_ = true;
    }
    VkDescriptorImageInfo images[2]{{VK_NULL_HANDLE, source, VK_IMAGE_LAYOUT_GENERAL},
                                    {VK_NULL_HANDLE, tone_.view, VK_IMAGE_LAYOUT_GENERAL}};
    VkWriteDescriptorSet writes[2]{};
    for (uint32_t i = 0; i < 2; ++i) {
        writes[i].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[i].dstSet = toneSet_; writes[i].dstBinding = i; writes[i].descriptorCount = 1;
        writes[i].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE; writes[i].pImageInfo = &images[i];
    }
    vkUpdateDescriptorSets(device_, 2, writes, 0, nullptr);
    const rawr::highlight::ColoroppTonePush pc{width_, height_, rawr::highlight::coloroppToneCompression(compression),
                                               rawr::highlight::coloroppToneExposureGain(exposureGain)};
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, tonePipeline_);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, toneLayout_, 0, 1, &toneSet_, 0, nullptr);
    vkCmdPushConstants(cmd, toneLayout_, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(pc), &pc);
    vkCmdDispatch(cmd, (width_ + 15u) / 16u, (height_ + 15u) / 16u, 1);
    auto ready = imageBarrier(tone_.image, VK_ACCESS_SHADER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT, false);
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                         0, 0, nullptr, 0, nullptr, 1, &ready);
}
}
