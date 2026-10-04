#include "PipelineDiagnostics.h"

#include <array>
#include <stdexcept>
#include <utility>

#include "diagnostic_linear_pattern.h"
#include "diagnostic_raw_copy.h"
#include "diagnostic_raw_sampled_viz.h"
#include "diagnostic_raw_storage_viz.h"
#include "diagnostic_tone_pattern.h"

namespace rawrcam::diagnostics {
namespace {
void vkCheck(VkResult result, const char* operation) {
    if (result != VK_SUCCESS) throw std::runtime_error(std::string(operation) + " VkResult=" + std::to_string(result));
}
}  // namespace

PipelineDiagnostics::PipelineDiagnostics(Diagnostic diagnostic) : diagnostic_(std::move(diagnostic)) {}
PipelineDiagnostics::~PipelineDiagnostics() { reset(); }

void PipelineDiagnostics::initialize(
    VkDevice device, uint32_t rawWidth, uint32_t rawHeight, uint32_t previewWidth, uint32_t previewHeight,
    const std::array<VkImageView, rawrcam::imaging::kRealtimeFramesInFlight>& rawCopyViews,
    const std::array<VkImageView, rawrcam::imaging::kRealtimeFramesInFlight>& linearViews,
    const std::array<VkImageView, rawrcam::imaging::kRealtimeFramesInFlight>& tonemappedViews) {
    reset();
    device_ = device;
    rawWidth_ = rawWidth;
    rawHeight_ = rawHeight;
    previewWidth_ = previewWidth;
    previewHeight_ = previewHeight;
    rawCopyViews_ = rawCopyViews;
    tonemappedViews_ = tonemappedViews;

    createRawCopyPipeline();
    rawStorageViz_ = createRawVizPipeline(diagnostic_raw_storage_viz_spv, diagnostic_raw_storage_viz_spv_size, false);
    rawSampledViz_ = createRawVizPipeline(diagnostic_raw_sampled_viz_spv, diagnostic_raw_sampled_viz_spv_size, true);

    VkSamplerCreateInfo samplerInfo{};
    samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    samplerInfo.magFilter = VK_FILTER_NEAREST;
    samplerInfo.minFilter = VK_FILTER_NEAREST;
    samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
    samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.maxLod = 0.0f;
    vkCheck(vkCreateSampler(device_, &samplerInfo, nullptr, &nearestSampler_), "raw nearest sampler");

    linearPattern_ =
        createPatternPipeline(diagnostic_linear_pattern_spv, diagnostic_linear_pattern_spv_size, linearViews);
    tonePattern_ =
        createPatternPipeline(diagnostic_tone_pattern_spv, diagnostic_tone_pattern_spv_size, tonemappedViews);

    if (diagnostic_) {
        diagnostic_(
            "RAW_COPY_DIAGNOSTIC_READY mode=GPU_storage_image_copy source=imported_AHB destination=app_owned_R16_UINT");
        diagnostic_("RAW_VIZ_DIAGNOSTICS_READY storage_imageLoad=yes sampled_texelFetch=yes transfer_copy=yes");
    }
}

VkShaderModule PipelineDiagnostics::createShaderModule(const unsigned char* bytes, size_t size) const {
    VkShaderModuleCreateInfo info{};
    info.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    info.codeSize = size;
    info.pCode = reinterpret_cast<const uint32_t*>(bytes);
    VkShaderModule module = VK_NULL_HANDLE;
    vkCheck(vkCreateShaderModule(device_, &info, nullptr, &module), "diagnostic shader module");
    return module;
}

void PipelineDiagnostics::createRawCopyPipeline() {
    std::array<VkDescriptorSetLayoutBinding, 2> bindings{};
    for (uint32_t i = 0; i < 2; ++i) {
        bindings[i].binding = i;
        bindings[i].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
        bindings[i].descriptorCount = 1;
        bindings[i].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
    }
    VkDescriptorSetLayoutCreateInfo dslInfo{};
    dslInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    dslInfo.bindingCount = static_cast<uint32_t>(bindings.size());
    dslInfo.pBindings = bindings.data();
    vkCheck(vkCreateDescriptorSetLayout(device_, &dslInfo, nullptr, &rawCopy_.dsl), "raw copy dsl");

    VkPipelineLayoutCreateInfo layoutInfo{};
    layoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    layoutInfo.setLayoutCount = 1;
    layoutInfo.pSetLayouts = &rawCopy_.dsl;
    vkCheck(vkCreatePipelineLayout(device_, &layoutInfo, nullptr, &rawCopy_.layout), "raw copy layout");

    VkShaderModule module = createShaderModule(diagnostic_raw_copy_spv, diagnostic_raw_copy_spv_size);
    VkPipelineShaderStageCreateInfo stage{};
    stage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stage.stage = VK_SHADER_STAGE_COMPUTE_BIT;
    stage.module = module;
    stage.pName = "main";
    VkComputePipelineCreateInfo pipelineInfo{};
    pipelineInfo.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
    pipelineInfo.stage = stage;
    pipelineInfo.layout = rawCopy_.layout;
    const VkResult result =
        vkCreateComputePipelines(device_, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &rawCopy_.pipeline);
    vkDestroyShaderModule(device_, module, nullptr);
    vkCheck(result, "raw copy pipeline");

    VkDescriptorPoolSize poolSize{VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 2u * rawrcam::imaging::kRealtimeFramesInFlight};
    VkDescriptorPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    poolInfo.maxSets = rawrcam::imaging::kRealtimeFramesInFlight;
    poolInfo.poolSizeCount = 1;
    poolInfo.pPoolSizes = &poolSize;
    vkCheck(vkCreateDescriptorPool(device_, &poolInfo, nullptr, &rawCopy_.pool), "raw copy pool");
    std::array<VkDescriptorSetLayout, rawrcam::imaging::kRealtimeFramesInFlight> layouts{};
    layouts.fill(rawCopy_.dsl);
    VkDescriptorSetAllocateInfo setInfo{};
    setInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    setInfo.descriptorPool = rawCopy_.pool;
    setInfo.descriptorSetCount = rawrcam::imaging::kRealtimeFramesInFlight;
    setInfo.pSetLayouts = layouts.data();
    vkCheck(vkAllocateDescriptorSets(device_, &setInfo, rawCopy_.sets.data()), "raw copy sets");
}

PipelineDiagnostics::RawVizPipeline PipelineDiagnostics::createRawVizPipeline(const unsigned char* bytes, size_t size,
                                                                              bool sampled) {
    RawVizPipeline out{};
    out.sourceType = sampled ? VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER : VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    std::array<VkDescriptorSetLayoutBinding, 2> bindings{};
    bindings[0] = {0, out.sourceType, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr};
    bindings[1] = {1, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr};
    VkDescriptorSetLayoutCreateInfo dslInfo{};
    dslInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    dslInfo.bindingCount = 2;
    dslInfo.pBindings = bindings.data();
    vkCheck(vkCreateDescriptorSetLayout(device_, &dslInfo, nullptr, &out.dsl), "raw viz dsl");

    VkPushConstantRange push{VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(RawVizPush)};
    VkPipelineLayoutCreateInfo layoutInfo{};
    layoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    layoutInfo.setLayoutCount = 1;
    layoutInfo.pSetLayouts = &out.dsl;
    layoutInfo.pushConstantRangeCount = 1;
    layoutInfo.pPushConstantRanges = &push;
    vkCheck(vkCreatePipelineLayout(device_, &layoutInfo, nullptr, &out.layout), "raw viz layout");

    VkShaderModule module = createShaderModule(bytes, size);
    VkPipelineShaderStageCreateInfo stage{};
    stage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stage.stage = VK_SHADER_STAGE_COMPUTE_BIT;
    stage.module = module;
    stage.pName = "main";
    VkComputePipelineCreateInfo pipelineInfo{};
    pipelineInfo.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
    pipelineInfo.stage = stage;
    pipelineInfo.layout = out.layout;
    const VkResult result = vkCreateComputePipelines(device_, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &out.pipeline);
    vkDestroyShaderModule(device_, module, nullptr);
    vkCheck(result, "raw viz pipeline");

    std::array<VkDescriptorPoolSize, 2> sizes{};
    uint32_t poolSizeCount = 1;
    sizes[0] = {out.sourceType, rawrcam::imaging::kRealtimeFramesInFlight};
    if (out.sourceType == VK_DESCRIPTOR_TYPE_STORAGE_IMAGE) {
        sizes[0].descriptorCount = 2u * rawrcam::imaging::kRealtimeFramesInFlight;
    } else {
        sizes[1] = {VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, rawrcam::imaging::kRealtimeFramesInFlight};
        poolSizeCount = 2;
    }
    VkDescriptorPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    poolInfo.maxSets = rawrcam::imaging::kRealtimeFramesInFlight;
    poolInfo.poolSizeCount = poolSizeCount;
    poolInfo.pPoolSizes = sizes.data();
    vkCheck(vkCreateDescriptorPool(device_, &poolInfo, nullptr, &out.pool), "raw viz pool");
    std::array<VkDescriptorSetLayout, rawrcam::imaging::kRealtimeFramesInFlight> layouts{};
    layouts.fill(out.dsl);
    VkDescriptorSetAllocateInfo setInfo{};
    setInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    setInfo.descriptorPool = out.pool;
    setInfo.descriptorSetCount = rawrcam::imaging::kRealtimeFramesInFlight;
    setInfo.pSetLayouts = layouts.data();
    vkCheck(vkAllocateDescriptorSets(device_, &setInfo, out.sets.data()), "raw viz sets");
    return out;
}

PipelineDiagnostics::PatternPipeline PipelineDiagnostics::createPatternPipeline(
    const unsigned char* bytes, size_t size,
    const std::array<VkImageView, rawrcam::imaging::kRealtimeFramesInFlight>& targetViews) {
    PatternPipeline out{};
    VkDescriptorSetLayoutBinding binding{};
    binding.binding = 0;
    binding.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    binding.descriptorCount = 1;
    binding.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
    VkDescriptorSetLayoutCreateInfo dslInfo{};
    dslInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    dslInfo.bindingCount = 1;
    dslInfo.pBindings = &binding;
    vkCheck(vkCreateDescriptorSetLayout(device_, &dslInfo, nullptr, &out.dsl), "diagnostic pattern dsl");
    VkPushConstantRange push{VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(PatternPush)};
    VkPipelineLayoutCreateInfo layoutInfo{};
    layoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    layoutInfo.setLayoutCount = 1;
    layoutInfo.pSetLayouts = &out.dsl;
    layoutInfo.pushConstantRangeCount = 1;
    layoutInfo.pPushConstantRanges = &push;
    vkCheck(vkCreatePipelineLayout(device_, &layoutInfo, nullptr, &out.layout), "diagnostic pattern layout");
    VkShaderModule module = createShaderModule(bytes, size);
    VkPipelineShaderStageCreateInfo stage{};
    stage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stage.stage = VK_SHADER_STAGE_COMPUTE_BIT;
    stage.module = module;
    stage.pName = "main";
    VkComputePipelineCreateInfo pipelineInfo{};
    pipelineInfo.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
    pipelineInfo.stage = stage;
    pipelineInfo.layout = out.layout;
    const VkResult result = vkCreateComputePipelines(device_, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &out.pipeline);
    vkDestroyShaderModule(device_, module, nullptr);
    vkCheck(result, "diagnostic pattern pipeline");
    VkDescriptorPoolSize poolSize{VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, rawrcam::imaging::kRealtimeFramesInFlight};
    VkDescriptorPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    poolInfo.maxSets = rawrcam::imaging::kRealtimeFramesInFlight;
    poolInfo.poolSizeCount = 1;
    poolInfo.pPoolSizes = &poolSize;
    vkCheck(vkCreateDescriptorPool(device_, &poolInfo, nullptr, &out.pool), "diagnostic pattern pool");
    std::array<VkDescriptorSetLayout, rawrcam::imaging::kRealtimeFramesInFlight> layouts{};
    layouts.fill(out.dsl);
    VkDescriptorSetAllocateInfo setInfo{};
    setInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    setInfo.descriptorPool = out.pool;
    setInfo.descriptorSetCount = rawrcam::imaging::kRealtimeFramesInFlight;
    setInfo.pSetLayouts = layouts.data();
    vkCheck(vkAllocateDescriptorSets(device_, &setInfo, out.sets.data()), "diagnostic pattern sets");
    for (uint32_t i = 0; i < rawrcam::imaging::kRealtimeFramesInFlight; ++i) {
        VkDescriptorImageInfo imageInfo{VK_NULL_HANDLE, targetViews[i], VK_IMAGE_LAYOUT_GENERAL};
        VkWriteDescriptorSet write{};
        write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        write.dstSet = out.sets[i];
        write.dstBinding = 0;
        write.descriptorCount = 1;
        write.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
        write.pImageInfo = &imageInfo;
        vkUpdateDescriptorSets(device_, 1, &write, 0, nullptr);
    }
    return out;
}

void PipelineDiagnostics::recordRawCopy(VkCommandBuffer command, uint32_t slotIndex, VkImageView sourceView) {
    VkDescriptorImageInfo src{VK_NULL_HANDLE, sourceView, VK_IMAGE_LAYOUT_GENERAL};
    VkDescriptorImageInfo dst{VK_NULL_HANDLE, rawCopyViews_.at(slotIndex), VK_IMAGE_LAYOUT_GENERAL};
    VkWriteDescriptorSet writes[2]{};
    writes[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    writes[0].dstSet = rawCopy_.sets.at(slotIndex);
    writes[0].dstBinding = 0;
    writes[0].descriptorCount = 1;
    writes[0].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    writes[0].pImageInfo = &src;
    writes[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    writes[1].dstSet = rawCopy_.sets.at(slotIndex);
    writes[1].dstBinding = 1;
    writes[1].descriptorCount = 1;
    writes[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    writes[1].pImageInfo = &dst;
    vkUpdateDescriptorSets(device_, 2, writes, 0, nullptr);
    vkCmdBindPipeline(command, VK_PIPELINE_BIND_POINT_COMPUTE, rawCopy_.pipeline);
    vkCmdBindDescriptorSets(command, VK_PIPELINE_BIND_POINT_COMPUTE, rawCopy_.layout, 0, 1, &rawCopy_.sets[slotIndex],
                            0, nullptr);
    vkCmdDispatch(command, (rawWidth_ + 15u) / 16u, (rawHeight_ + 15u) / 16u, 1);
}

void PipelineDiagnostics::recordRawVisualization(VkCommandBuffer command, uint32_t slotIndex, VkImageView sourceView,
                                                 bool sampledSource, const std::array<float, 4>& blackLevels,
                                                 float whiteLevel) {
    RawVizPipeline& pipeline = sampledSource ? rawSampledViz_ : rawStorageViz_;
    VkDescriptorImageInfo source{};
    source.imageView = sourceView;
    source.imageLayout = VK_IMAGE_LAYOUT_GENERAL;
    if (sampledSource) source.sampler = nearestSampler_;
    VkDescriptorImageInfo target{};
    target.imageView = tonemappedViews_.at(slotIndex);
    target.imageLayout = VK_IMAGE_LAYOUT_GENERAL;
    VkWriteDescriptorSet writes[2]{};
    writes[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    writes[0].dstSet = pipeline.sets.at(slotIndex);
    writes[0].dstBinding = 0;
    writes[0].descriptorCount = 1;
    writes[0].descriptorType = pipeline.sourceType;
    writes[0].pImageInfo = &source;
    writes[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    writes[1].dstSet = pipeline.sets.at(slotIndex);
    writes[1].dstBinding = 1;
    writes[1].descriptorCount = 1;
    writes[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    writes[1].pImageInfo = &target;
    vkUpdateDescriptorSets(device_, 2, writes, 0, nullptr);

    const float black = (blackLevels[0] + blackLevels[1] + blackLevels[2] + blackLevels[3]) * 0.25f;
    const RawVizPush push{previewWidth_, previewHeight_, black, whiteLevel};
    vkCmdBindPipeline(command, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline.pipeline);
    vkCmdBindDescriptorSets(command, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline.layout, 0, 1, &pipeline.sets[slotIndex],
                            0, nullptr);
    vkCmdPushConstants(command, pipeline.layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(push), &push);
    vkCmdDispatch(command, (previewWidth_ + 7u) / 8u, (previewHeight_ + 7u) / 8u, 1);
}

void PipelineDiagnostics::recordPattern(VkCommandBuffer command, const PatternPipeline& pipeline,
                                        uint32_t slotIndex) const {
    vkCmdBindPipeline(command, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline.pipeline);
    vkCmdBindDescriptorSets(command, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline.layout, 0, 1,
                            &pipeline.sets.at(slotIndex), 0, nullptr);
    const PatternPush push{previewWidth_, previewHeight_};
    vkCmdPushConstants(command, pipeline.layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(push), &push);
    vkCmdDispatch(command, (previewWidth_ + 7u) / 8u, (previewHeight_ + 7u) / 8u, 1);
}

void PipelineDiagnostics::recordLinearPattern(VkCommandBuffer command, uint32_t slotIndex) const {
    recordPattern(command, linearPattern_, slotIndex);
}
void PipelineDiagnostics::recordTonePattern(VkCommandBuffer command, uint32_t slotIndex) const {
    recordPattern(command, tonePattern_, slotIndex);
}

void PipelineDiagnostics::destroyRawVizPipeline(RawVizPipeline& pipeline) {
    if (pipeline.pool) vkDestroyDescriptorPool(device_, pipeline.pool, nullptr);
    if (pipeline.pipeline) vkDestroyPipeline(device_, pipeline.pipeline, nullptr);
    if (pipeline.layout) vkDestroyPipelineLayout(device_, pipeline.layout, nullptr);
    if (pipeline.dsl) vkDestroyDescriptorSetLayout(device_, pipeline.dsl, nullptr);
    pipeline = {};
}
void PipelineDiagnostics::destroyPatternPipeline(PatternPipeline& pipeline) {
    if (pipeline.pool) vkDestroyDescriptorPool(device_, pipeline.pool, nullptr);
    if (pipeline.pipeline) vkDestroyPipeline(device_, pipeline.pipeline, nullptr);
    if (pipeline.layout) vkDestroyPipelineLayout(device_, pipeline.layout, nullptr);
    if (pipeline.dsl) vkDestroyDescriptorSetLayout(device_, pipeline.dsl, nullptr);
    pipeline = {};
}

void PipelineDiagnostics::reset() {
    if (device_ != VK_NULL_HANDLE) {
        if (rawCopy_.pool) vkDestroyDescriptorPool(device_, rawCopy_.pool, nullptr);
        if (rawCopy_.pipeline) vkDestroyPipeline(device_, rawCopy_.pipeline, nullptr);
        if (rawCopy_.layout) vkDestroyPipelineLayout(device_, rawCopy_.layout, nullptr);
        if (rawCopy_.dsl) vkDestroyDescriptorSetLayout(device_, rawCopy_.dsl, nullptr);
        destroyRawVizPipeline(rawStorageViz_);
        destroyRawVizPipeline(rawSampledViz_);
        destroyPatternPipeline(linearPattern_);
        destroyPatternPipeline(tonePattern_);
        if (nearestSampler_) vkDestroySampler(device_, nearestSampler_, nullptr);
    }
    rawCopy_ = {};
    rawStorageViz_ = {};
    rawSampledViz_ = {};
    linearPattern_ = {};
    tonePattern_ = {};
    nearestSampler_ = VK_NULL_HANDLE;
    rawCopyViews_.fill(VK_NULL_HANDLE);
    tonemappedViews_.fill(VK_NULL_HANDLE);
    rawWidth_ = rawHeight_ = previewWidth_ = previewHeight_ = 0;
}

}  // namespace rawrcam::diagnostics
