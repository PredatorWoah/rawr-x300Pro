#include "RawIntegrityProbe.h"

#include <android/log.h>
#include <vulkan/vulkan_android.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <sstream>
#include <stdexcept>
#include <utility>

#include "raw_integrity_probe.h"
#include "raw_zero_copy_buffer_import.h"
#include "raw_zero_copy_buffer_preview.h"
#include "raw_zero_copy_external_compare.h"
#include "raw_zero_copy_full_compare.h"
#include "raw_zero_copy_paths.h"

namespace rawrcam::diagnostics {
namespace {
constexpr const char* kTag = "RawrCamNative";
constexpr uint32_t kDenseWordsTotal = 64u * 64u * 9u;
constexpr uint32_t kBottomWordsTotal = 32u * 32u * 3u;
constexpr uint32_t kFullCompareStatsWords = 8u;
constexpr uint32_t kFullCompareStatsBase = kDenseWordsTotal + kBottomWordsTotal;
constexpr uint32_t kCandidateStatsBase = kFullCompareStatsBase + kFullCompareStatsWords;
constexpr uint32_t kCandidateStatsWords =
    80u;  // sampled + pitch hypotheses + linear + DRM + copy-buffer + buffer-preview + external-format
constexpr uint32_t kBufferPreviewStatsBase = kCandidateStatsBase + 64u;
constexpr uint32_t kExternalStatsBase = kCandidateStatsBase + 72u;
constexpr VkDeviceSize kProbeBytes = (kCandidateStatsBase + kCandidateStatsWords) * sizeof(uint32_t);

void vkCheck(VkResult result, const char* operation) {
    if (result != VK_SUCCESS) {
        throw std::runtime_error(std::string(operation) + " VkResult=" + std::to_string(result));
    }
}
}  // namespace

RawIntegrityProbe::RawIntegrityProbe(Diagnostic diagnostic) : diagnostic_(std::move(diagnostic)) {}
RawIntegrityProbe::~RawIntegrityProbe() { reset(); }

void RawIntegrityProbe::emit(const std::string& line) const {
    if (diagnostic_) diagnostic_(line);
}

uint32_t RawIntegrityProbe::findMemoryType(uint32_t bits, VkMemoryPropertyFlags required, bool* coherent) const {
    VkPhysicalDeviceMemoryProperties properties{};
    vkGetPhysicalDeviceMemoryProperties(physicalDevice_, &properties);
    for (uint32_t i = 0; i < properties.memoryTypeCount; ++i) {
        const auto flags = properties.memoryTypes[i].propertyFlags;
        if ((bits & (1u << i)) && (flags & (required | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)) ==
                                      (required | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)) {
            *coherent = true;
            return i;
        }
    }
    for (uint32_t i = 0; i < properties.memoryTypeCount; ++i) {
        if ((bits & (1u << i)) && (properties.memoryTypes[i].propertyFlags & required) == required) {
            *coherent = false;
            return i;
        }
    }
    throw std::runtime_error("No host-visible memory type for RAW integrity probe");
}

void RawIntegrityProbe::initialize(VkPhysicalDevice physicalDevice, VkDevice device, uint32_t rawWidth,
                                   uint32_t rawHeight, uint32_t outputWidth, uint32_t outputHeight,
                                   uint32_t frameSlotCount) {
    reset();
    physicalDevice_ = physicalDevice;
    device_ = device;
    rawWidth_ = rawWidth;
    rawHeight_ = rawHeight;
    outputWidth_ = outputWidth;
    outputHeight_ = outputHeight;
    VkPhysicalDeviceProperties physicalProperties{};
    vkGetPhysicalDeviceProperties(physicalDevice_, &physicalProperties);
    timestampPeriodNs_ = physicalProperties.limits.timestampPeriod;

    VkBufferCreateInfo bufferInfo{};
    bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufferInfo.size = kProbeBytes;
    bufferInfo.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    vkCheck(vkCreateBuffer(device_, &bufferInfo, nullptr, &resources_.buffer), "create probe buffer");

    VkMemoryRequirements requirements{};
    vkGetBufferMemoryRequirements(device_, resources_.buffer, &requirements);
    VkMemoryAllocateInfo allocation{};
    allocation.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocation.allocationSize = requirements.size;
    allocation.memoryTypeIndex =
        findMemoryType(requirements.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT, &resources_.coherent);
    vkCheck(vkAllocateMemory(device_, &allocation, nullptr, &resources_.memory), "allocate probe memory");
    vkCheck(vkBindBufferMemory(device_, resources_.buffer, resources_.memory, 0), "bind probe buffer");
    vkCheck(vkMapMemory(device_, resources_.memory, 0, VK_WHOLE_SIZE, 0, &resources_.mapped), "map probe buffer");
    std::memset(resources_.mapped, 0, static_cast<size_t>(kProbeBytes));
    resources_.allocationSize = requirements.size;

    std::ostringstream bufferLog;
    bufferLog << "RAW_PROBE_BUFFER bytes=" << static_cast<unsigned long long>(kProbeBytes)
              << " allocation=" << static_cast<unsigned long long>(resources_.allocationSize)
              << " coherent=" << (resources_.coherent ? "yes" : "no");
    __android_log_print(ANDROID_LOG_INFO, kTag, "%s", bufferLog.str().c_str());
    emit(bufferLog.str());

    std::array<VkDescriptorSetLayoutBinding, 11> bindings{};
    for (uint32_t i = 0; i < 4; ++i) {
        bindings[i].binding = i;
        bindings[i].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
        bindings[i].descriptorCount = 1;
        bindings[i].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
    }
    bindings[4].binding = 4;
    bindings[4].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    bindings[4].descriptorCount = 1;
    bindings[4].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
    bindings[5].binding = 5;
    bindings[5].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    bindings[5].descriptorCount = 1;
    bindings[5].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
    bindings[6].binding = 6;
    bindings[6].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    bindings[6].descriptorCount = 1;
    bindings[6].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
    bindings[7].binding = 7;
    bindings[7].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    bindings[7].descriptorCount = 1;
    bindings[7].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
    bindings[8].binding = 8;
    bindings[8].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    bindings[8].descriptorCount = 1;
    bindings[8].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
    bindings[9].binding = 9;
    bindings[9].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    bindings[9].descriptorCount = 1;
    bindings[9].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
    bindings[10].binding = 10;
    bindings[10].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    bindings[10].descriptorCount = 1;
    bindings[10].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
    VkDescriptorSetLayoutCreateInfo layoutInfo{};
    layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    layoutInfo.bindingCount = static_cast<uint32_t>(bindings.size());
    layoutInfo.pBindings = bindings.data();
    vkCheck(vkCreateDescriptorSetLayout(device_, &layoutInfo, nullptr, &resources_.dsl),
            "create probe descriptor layout");

    VkPushConstantRange pushRange{};
    pushRange.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
    pushRange.offset = 0;
    pushRange.size = sizeof(Push);
    VkPipelineLayoutCreateInfo pipelineLayoutInfo{};
    pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pipelineLayoutInfo.setLayoutCount = 1;
    pipelineLayoutInfo.pSetLayouts = &resources_.dsl;
    pipelineLayoutInfo.pushConstantRangeCount = 1;
    pipelineLayoutInfo.pPushConstantRanges = &pushRange;
    vkCheck(vkCreatePipelineLayout(device_, &pipelineLayoutInfo, nullptr, &resources_.layout),
            "create probe pipeline layout");

    VkShaderModuleCreateInfo shaderInfo{};
    shaderInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    shaderInfo.codeSize = raw_integrity_probe_spv_size;
    shaderInfo.pCode = reinterpret_cast<const uint32_t*>(raw_integrity_probe_spv);
    VkShaderModule shader = VK_NULL_HANDLE;
    vkCheck(vkCreateShaderModule(device_, &shaderInfo, nullptr, &shader), "create probe shader module");
    VkPipelineShaderStageCreateInfo stage{};
    stage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stage.stage = VK_SHADER_STAGE_COMPUTE_BIT;
    stage.module = shader;
    stage.pName = "main";
    VkComputePipelineCreateInfo pipelineInfo{};
    pipelineInfo.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
    pipelineInfo.stage = stage;
    pipelineInfo.layout = resources_.layout;
    const VkResult pipelineResult =
        vkCreateComputePipelines(device_, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &resources_.pipeline);
    vkDestroyShaderModule(device_, shader, nullptr);
    vkCheck(pipelineResult, "create probe compute pipeline");

    VkShaderModuleCreateInfo fullShaderInfo{};
    fullShaderInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    fullShaderInfo.codeSize = raw_zero_copy_full_compare_spv_size;
    fullShaderInfo.pCode = reinterpret_cast<const uint32_t*>(raw_zero_copy_full_compare_spv);
    VkShaderModule fullShader = VK_NULL_HANDLE;
    vkCheck(vkCreateShaderModule(device_, &fullShaderInfo, nullptr, &fullShader), "create full compare shader module");
    pipelineInfo.stage.module = fullShader;
    const VkResult fullPipelineResult =
        vkCreateComputePipelines(device_, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &resources_.fullComparePipeline);
    vkDestroyShaderModule(device_, fullShader, nullptr);
    vkCheck(fullPipelineResult, "create full RAW compare compute pipeline");

    VkShaderModuleCreateInfo pathShaderInfo{};
    pathShaderInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    pathShaderInfo.codeSize = raw_zero_copy_paths_spv_size;
    pathShaderInfo.pCode = reinterpret_cast<const uint32_t*>(raw_zero_copy_paths_spv);
    VkShaderModule pathShader = VK_NULL_HANDLE;
    vkCheck(vkCreateShaderModule(device_, &pathShaderInfo, nullptr, &pathShader),
            "create zero-copy path shader module");
    pipelineInfo.stage.module = pathShader;
    const VkResult pathPipelineResult =
        vkCreateComputePipelines(device_, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &resources_.pathPipeline);
    vkDestroyShaderModule(device_, pathShader, nullptr);
    vkCheck(pathPipelineResult, "create zero-copy path compute pipeline");

    VkSamplerCreateInfo samplerInfo{};
    samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    samplerInfo.magFilter = VK_FILTER_NEAREST;
    samplerInfo.minFilter = VK_FILTER_NEAREST;
    samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
    samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.minLod = 0.0f;
    samplerInfo.maxLod = 0.0f;
    vkCheck(vkCreateSampler(device_, &samplerInfo, nullptr, &resources_.rawSampler), "create RAW sampled path sampler");

    std::array<VkDescriptorPoolSize, 3> poolSizes{};
    poolSizes[0] = {VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 7};
    poolSizes[1] = {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 2};
    poolSizes[2] = {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 2};
    VkDescriptorPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    poolInfo.maxSets = 1;
    poolInfo.poolSizeCount = static_cast<uint32_t>(poolSizes.size());
    poolInfo.pPoolSizes = poolSizes.data();
    vkCheck(vkCreateDescriptorPool(device_, &poolInfo, nullptr, &resources_.pool), "create probe descriptor pool");
    VkDescriptorSetAllocateInfo setInfo{};
    setInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    setInfo.descriptorPool = resources_.pool;
    setInfo.descriptorSetCount = 1;
    setInfo.pSetLayouts = &resources_.dsl;
    vkCheck(vkAllocateDescriptorSets(device_, &setInfo, &resources_.set), "allocate probe descriptor set");

    initializeBufferPreviewBenchmark(frameSlotCount);
    initializeBufferImportParity();

    done_ = false;
    pendingSlot_ = -1;
    pendingTimestamp_ = 0;
    pendingBufImportSlot_ = -1;
    pendingBufImportTimestamp_ = 0;
    bufImportDone_ = false;
}

uint32_t RawIntegrityProbe::findDeviceLocalMemoryType(uint32_t bits) const {
    VkPhysicalDeviceMemoryProperties properties{};
    vkGetPhysicalDeviceMemoryProperties(physicalDevice_, &properties);
    for (uint32_t i = 0; i < properties.memoryTypeCount; ++i) {
        if ((bits & (1u << i)) && (properties.memoryTypes[i].propertyFlags & VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT) != 0)
            return i;
    }
    for (uint32_t i = 0; i < properties.memoryTypeCount; ++i)
        if (bits & (1u << i)) return i;
    throw std::runtime_error("No memory type for buffer-preview benchmark image");
}

void RawIntegrityProbe::initializeBufferPreviewBenchmark(uint32_t frameSlotCount) {
    if (frameSlotCount == 0) return;
    bufferBenchmarkSlots_.resize(frameSlotCount);

    std::array<VkDescriptorSetLayoutBinding, 2> bindings{};
    bindings[0].binding = 0;
    bindings[0].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    bindings[0].descriptorCount = 1;
    bindings[0].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
    bindings[1].binding = 1;
    bindings[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    bindings[1].descriptorCount = 1;
    bindings[1].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
    VkDescriptorSetLayoutCreateInfo dsl{};
    dsl.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    dsl.bindingCount = static_cast<uint32_t>(bindings.size());
    dsl.pBindings = bindings.data();
    vkCheck(vkCreateDescriptorSetLayout(device_, &dsl, nullptr, &resources_.bufferPreviewDsl),
            "create buffer-preview descriptor layout");

    VkPushConstantRange push{};
    push.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
    push.size = sizeof(BufferPreviewPush);
    VkPipelineLayoutCreateInfo layout{};
    layout.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    layout.setLayoutCount = 1;
    layout.pSetLayouts = &resources_.bufferPreviewDsl;
    layout.pushConstantRangeCount = 1;
    layout.pPushConstantRanges = &push;
    vkCheck(vkCreatePipelineLayout(device_, &layout, nullptr, &resources_.bufferPreviewLayout),
            "create buffer-preview pipeline layout");

    VkShaderModuleCreateInfo shaderInfo{};
    shaderInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    shaderInfo.codeSize = raw_zero_copy_buffer_preview_spv_size;
    shaderInfo.pCode = reinterpret_cast<const uint32_t*>(raw_zero_copy_buffer_preview_spv);
    VkShaderModule shader = VK_NULL_HANDLE;
    vkCheck(vkCreateShaderModule(device_, &shaderInfo, nullptr, &shader), "create buffer-preview shader");
    VkPipelineShaderStageCreateInfo stage{};
    stage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stage.stage = VK_SHADER_STAGE_COMPUTE_BIT;
    stage.module = shader;
    stage.pName = "main";
    VkComputePipelineCreateInfo pipeline{};
    pipeline.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
    pipeline.stage = stage;
    pipeline.layout = resources_.bufferPreviewLayout;
    const VkResult pipelineResult =
        vkCreateComputePipelines(device_, VK_NULL_HANDLE, 1, &pipeline, nullptr, &resources_.bufferPreviewPipeline);
    vkDestroyShaderModule(device_, shader, nullptr);
    vkCheck(pipelineResult, "create buffer-preview pipeline");

    std::array<VkDescriptorPoolSize, 2> sizes{};
    sizes[0] = {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, frameSlotCount};
    sizes[1] = {VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, frameSlotCount};
    VkDescriptorPoolCreateInfo pool{};
    pool.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    pool.maxSets = frameSlotCount;
    pool.poolSizeCount = static_cast<uint32_t>(sizes.size());
    pool.pPoolSizes = sizes.data();
    vkCheck(vkCreateDescriptorPool(device_, &pool, nullptr, &resources_.bufferPreviewPool),
            "create buffer-preview descriptor pool");

    std::vector<VkDescriptorSetLayout> layouts(frameSlotCount, resources_.bufferPreviewDsl);
    std::vector<VkDescriptorSet> sets(frameSlotCount);
    VkDescriptorSetAllocateInfo alloc{};
    alloc.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    alloc.descriptorPool = resources_.bufferPreviewPool;
    alloc.descriptorSetCount = frameSlotCount;
    alloc.pSetLayouts = layouts.data();
    vkCheck(vkAllocateDescriptorSets(device_, &alloc, sets.data()), "allocate buffer-preview descriptor sets");

    for (uint32_t i = 0; i < frameSlotCount; ++i) {
        auto& slot = bufferBenchmarkSlots_[i];
        slot.set = sets[i];
        VkImageCreateInfo image{};
        image.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
        image.imageType = VK_IMAGE_TYPE_2D;
        image.format = VK_FORMAT_R16G16B16A16_SFLOAT;
        image.extent = {outputWidth_, outputHeight_, 1};
        image.mipLevels = 1;
        image.arrayLayers = 1;
        image.samples = VK_SAMPLE_COUNT_1_BIT;
        image.tiling = VK_IMAGE_TILING_OPTIMAL;
        image.usage = VK_IMAGE_USAGE_STORAGE_BIT;
        image.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        image.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        vkCheck(vkCreateImage(device_, &image, nullptr, &slot.image), "create buffer-preview output image");
        VkMemoryRequirements requirements{};
        vkGetImageMemoryRequirements(device_, slot.image, &requirements);
        VkMemoryAllocateInfo memory{};
        memory.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        memory.allocationSize = requirements.size;
        memory.memoryTypeIndex = findDeviceLocalMemoryType(requirements.memoryTypeBits);
        vkCheck(vkAllocateMemory(device_, &memory, nullptr, &slot.memory), "allocate buffer-preview output image");
        vkCheck(vkBindImageMemory(device_, slot.image, slot.memory, 0), "bind buffer-preview output image");
        VkImageViewCreateInfo view{};
        view.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        view.image = slot.image;
        view.viewType = VK_IMAGE_VIEW_TYPE_2D;
        view.format = VK_FORMAT_R16G16B16A16_SFLOAT;
        view.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
        vkCheck(vkCreateImageView(device_, &view, nullptr, &slot.view), "create buffer-preview output view");
    }

    VkQueryPoolCreateInfo query{};
    query.sType = VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO;
    query.queryType = VK_QUERY_TYPE_TIMESTAMP;
    query.queryCount = frameSlotCount * 5u;
    vkCheck(vkCreateQueryPool(device_, &query, nullptr, &resources_.bufferBenchmarkQueryPool),
            "create zero-copy final benchmark query pool");
    emit("RAW_ZERO_COPY_BUFFER_BENCHMARK_READY slots=" + std::to_string(frameSlotCount) +
         " comparison=copy_image_plus_raw_preview_vs_copy_buffer_plus_buffer_preview");
}

void RawIntegrityProbe::initializeBufferImportParity() {
    constexpr VkDeviceSize kStatsBytes = 8u * sizeof(uint32_t);
    VkBufferCreateInfo bufferInfo{};
    bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufferInfo.size = kStatsBytes;
    bufferInfo.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    vkCheck(vkCreateBuffer(device_, &bufferInfo, nullptr, &resources_.bufImportBuffer),
            "create buffer-import probe buffer");

    VkMemoryRequirements requirements{};
    vkGetBufferMemoryRequirements(device_, resources_.bufImportBuffer, &requirements);
    VkMemoryAllocateInfo allocation{};
    allocation.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocation.allocationSize = requirements.size;
    allocation.memoryTypeIndex =
        findMemoryType(requirements.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT, &resources_.bufImportCoherent);
    vkCheck(vkAllocateMemory(device_, &allocation, nullptr, &resources_.bufImportMemory),
            "allocate buffer-import probe memory");
    vkCheck(vkBindBufferMemory(device_, resources_.bufImportBuffer, resources_.bufImportMemory, 0),
            "bind buffer-import probe buffer");
    vkCheck(vkMapMemory(device_, resources_.bufImportMemory, 0, VK_WHOLE_SIZE, 0, &resources_.bufImportMapped),
            "map buffer-import probe buffer");

    std::array<VkDescriptorSetLayoutBinding, 3> bindings{};
    bindings[0].binding = 0;
    bindings[0].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    bindings[0].descriptorCount = 1;
    bindings[0].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
    bindings[1].binding = 1;
    bindings[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    bindings[1].descriptorCount = 1;
    bindings[1].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
    bindings[2].binding = 2;
    bindings[2].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    bindings[2].descriptorCount = 1;
    bindings[2].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
    VkDescriptorSetLayoutCreateInfo layoutInfo{};
    layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    layoutInfo.bindingCount = static_cast<uint32_t>(bindings.size());
    layoutInfo.pBindings = bindings.data();
    vkCheck(vkCreateDescriptorSetLayout(device_, &layoutInfo, nullptr, &resources_.bufImportDsl),
            "create buffer-import descriptor layout");

    VkPushConstantRange pushRange{};
    pushRange.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
    pushRange.offset = 0;
    pushRange.size = sizeof(BufferImportPush);
    VkPipelineLayoutCreateInfo pipelineLayoutInfo{};
    pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pipelineLayoutInfo.setLayoutCount = 1;
    pipelineLayoutInfo.pSetLayouts = &resources_.bufImportDsl;
    pipelineLayoutInfo.pushConstantRangeCount = 1;
    pipelineLayoutInfo.pPushConstantRanges = &pushRange;
    vkCheck(vkCreatePipelineLayout(device_, &pipelineLayoutInfo, nullptr, &resources_.bufImportLayout),
            "create buffer-import pipeline layout");

    VkShaderModuleCreateInfo shaderInfo{};
    shaderInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    shaderInfo.codeSize = raw_zero_copy_buffer_import_spv_size;
    shaderInfo.pCode = reinterpret_cast<const uint32_t*>(raw_zero_copy_buffer_import_spv);
    VkShaderModule shader = VK_NULL_HANDLE;
    vkCheck(vkCreateShaderModule(device_, &shaderInfo, nullptr, &shader), "create buffer-import shader module");
    VkPipelineShaderStageCreateInfo stage{};
    stage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stage.stage = VK_SHADER_STAGE_COMPUTE_BIT;
    stage.module = shader;
    stage.pName = "main";
    VkComputePipelineCreateInfo pipelineInfo{};
    pipelineInfo.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
    pipelineInfo.stage = stage;
    pipelineInfo.layout = resources_.bufImportLayout;
    const VkResult pipelineResult =
        vkCreateComputePipelines(device_, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &resources_.bufImportPipeline);
    vkDestroyShaderModule(device_, shader, nullptr);
    vkCheck(pipelineResult, "create buffer-import compute pipeline");

    std::array<VkDescriptorPoolSize, 2> poolSizes{};
    poolSizes[0] = {VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1};
    poolSizes[1] = {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 2};
    VkDescriptorPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    poolInfo.maxSets = 1;
    poolInfo.poolSizeCount = static_cast<uint32_t>(poolSizes.size());
    poolInfo.pPoolSizes = poolSizes.data();
    vkCheck(vkCreateDescriptorPool(device_, &poolInfo, nullptr, &resources_.bufImportPool),
            "create buffer-import descriptor pool");
    VkDescriptorSetAllocateInfo setInfo{};
    setInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    setInfo.descriptorPool = resources_.bufImportPool;
    setInfo.descriptorSetCount = 1;
    setInfo.pSetLayouts = &resources_.bufImportDsl;
    vkCheck(vkAllocateDescriptorSets(device_, &setInfo, &resources_.bufImportSet),
            "allocate buffer-import descriptor set");

    emit("RAW_ZERO_COPY_BUFFER_IMPORT_READY comparison=imported_ahb_storage_buffer_vs_bridge_owned_r16");
}

void RawIntegrityProbe::recordBufferImportParity(VkCommandBuffer command, VkImageView referenceView,
                                                 VkBuffer importBuffer, uint32_t stridePixels, uint32_t slotIndex,
                                                 uint64_t timestampNs) {
    if (resources_.bufImportPipeline == VK_NULL_HANDLE || resources_.bufImportMapped == nullptr || bufImportDone_ ||
        pendingBufImportSlot_ >= 0 || referenceView == VK_NULL_HANDLE || importBuffer == VK_NULL_HANDLE ||
        stridePixels == 0 || rawWidth_ == 0 || rawHeight_ == 0)
        return;

    VkDescriptorImageInfo refImage{};
    refImage.imageView = referenceView;
    refImage.imageLayout = VK_IMAGE_LAYOUT_GENERAL;
    VkDescriptorBufferInfo importInfo{};
    importInfo.buffer = importBuffer;
    importInfo.offset = 0;
    importInfo.range = VK_WHOLE_SIZE;
    VkDescriptorBufferInfo statsInfo{};
    statsInfo.buffer = resources_.bufImportBuffer;
    statsInfo.offset = 0;
    statsInfo.range = VK_WHOLE_SIZE;
    std::array<VkWriteDescriptorSet, 3> writes{};
    writes[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    writes[0].dstSet = resources_.bufImportSet;
    writes[0].dstBinding = 0;
    writes[0].descriptorCount = 1;
    writes[0].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    writes[0].pImageInfo = &refImage;
    writes[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    writes[1].dstSet = resources_.bufImportSet;
    writes[1].dstBinding = 1;
    writes[1].descriptorCount = 1;
    writes[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    writes[1].pBufferInfo = &importInfo;
    writes[2].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    writes[2].dstSet = resources_.bufImportSet;
    writes[2].dstBinding = 2;
    writes[2].descriptorCount = 1;
    writes[2].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    writes[2].pBufferInfo = &statsInfo;
    vkUpdateDescriptorSets(device_, static_cast<uint32_t>(writes.size()), writes.data(), 0, nullptr);

    // The imported camera memory was written externally; make it visible to
    // the compute shader. One-shot diagnostic: conservative full barrier.
    VkBufferMemoryBarrier importBarrier{};
    importBarrier.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
    importBarrier.srcAccessMask = VK_ACCESS_MEMORY_WRITE_BIT;
    importBarrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    importBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    importBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    importBarrier.buffer = importBuffer;
    importBarrier.offset = 0;
    importBarrier.size = VK_WHOLE_SIZE;
    vkCmdPipelineBarrier(command, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 0,
                         nullptr, 1, &importBarrier, 0, nullptr);

    vkCmdFillBuffer(command, resources_.bufImportBuffer, 0, 7u * sizeof(uint32_t), 0u);
    vkCmdFillBuffer(command, resources_.bufImportBuffer, 6u * sizeof(uint32_t), sizeof(uint32_t), 0xffffffffu);
    VkBufferMemoryBarrier fillBarrier{};
    fillBarrier.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
    fillBarrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    fillBarrier.dstAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
    fillBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    fillBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    fillBarrier.buffer = resources_.bufImportBuffer;
    fillBarrier.offset = 0;
    fillBarrier.size = VK_WHOLE_SIZE;
    vkCmdPipelineBarrier(command, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 0, nullptr,
                         1, &fillBarrier, 0, nullptr);

    const BufferImportPush push{rawWidth_, rawHeight_, stridePixels, 0u};
    vkCmdBindPipeline(command, VK_PIPELINE_BIND_POINT_COMPUTE, resources_.bufImportPipeline);
    vkCmdBindDescriptorSets(command, VK_PIPELINE_BIND_POINT_COMPUTE, resources_.bufImportLayout, 0, 1,
                            &resources_.bufImportSet, 0, nullptr);
    vkCmdPushConstants(command, resources_.bufImportLayout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(push), &push);
    vkCmdDispatch(command, (rawWidth_ + 15u) / 16u, (rawHeight_ + 15u) / 16u, 1);

    VkBufferMemoryBarrier hostBarrier{};
    hostBarrier.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
    hostBarrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
    hostBarrier.dstAccessMask = VK_ACCESS_HOST_READ_BIT;
    hostBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    hostBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    hostBarrier.buffer = resources_.bufImportBuffer;
    hostBarrier.offset = 0;
    hostBarrier.size = VK_WHOLE_SIZE;
    vkCmdPipelineBarrier(command, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_HOST_BIT, 0, 0, nullptr, 1,
                         &hostBarrier, 0, nullptr);

    pendingBufImportSlot_ = static_cast<int>(slotIndex);
    pendingBufImportTimestamp_ = timestampNs;
}

void RawIntegrityProbe::beginProductionRawBenchmark(VkCommandBuffer command, uint32_t slotIndex) {
    if (resources_.bufferBenchmarkQueryPool == VK_NULL_HANDLE || slotIndex >= bufferBenchmarkSlots_.size()) return;
    const uint32_t base = slotIndex * 5u;
    vkCmdResetQueryPool(command, resources_.bufferBenchmarkQueryPool, base, 5u);
    vkCmdWriteTimestamp(command, VK_PIPELINE_STAGE_TRANSFER_BIT, resources_.bufferBenchmarkQueryPool, base + 0u);
}

void RawIntegrityProbe::endProductionRawBenchmark(VkCommandBuffer command, uint32_t slotIndex) {
    if (resources_.bufferBenchmarkQueryPool == VK_NULL_HANDLE || slotIndex >= bufferBenchmarkSlots_.size()) return;
    vkCmdWriteTimestamp(command, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, resources_.bufferBenchmarkQueryPool,
                        slotIndex * 5u + 1u);
}

void RawIntegrityProbe::recordBufferPreviewBenchmark(VkCommandBuffer command, uint32_t slotIndex, VkImage sourceImage,
                                                     VkBuffer copyBuffer, const BufferPreviewParams& params) {
    if (resources_.bufferPreviewPipeline == VK_NULL_HANDLE || resources_.bufferBenchmarkQueryPool == VK_NULL_HANDLE ||
        sourceImage == VK_NULL_HANDLE || copyBuffer == VK_NULL_HANDLE || slotIndex >= bufferBenchmarkSlots_.size())
        return;
    auto& slot = bufferBenchmarkSlots_[slotIndex];
    const uint32_t queryBase = slotIndex * 5u;
    vkCmdWriteTimestamp(command, VK_PIPELINE_STAGE_TRANSFER_BIT, resources_.bufferBenchmarkQueryPool, queryBase + 2u);

    VkBufferImageCopy copy{};
    copy.bufferOffset = 0;
    copy.bufferRowLength = 0;
    copy.bufferImageHeight = 0;
    copy.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
    copy.imageExtent = {rawWidth_, rawHeight_, 1};
    vkCmdCopyImageToBuffer(command, sourceImage, VK_IMAGE_LAYOUT_GENERAL, copyBuffer, 1, &copy);
    vkCmdWriteTimestamp(command, VK_PIPELINE_STAGE_TRANSFER_BIT, resources_.bufferBenchmarkQueryPool, queryBase + 3u);

    VkBufferMemoryBarrier bufferBarrier{};
    bufferBarrier.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
    bufferBarrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    bufferBarrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    bufferBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    bufferBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    bufferBarrier.buffer = copyBuffer;
    bufferBarrier.offset = 0;
    bufferBarrier.size = VK_WHOLE_SIZE;
    vkCmdPipelineBarrier(command, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 0, nullptr,
                         1, &bufferBarrier, 0, nullptr);

    VkImageMemoryBarrier imageBarrier{};
    imageBarrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    imageBarrier.srcAccessMask = slot.layoutInitialized ? VK_ACCESS_SHADER_WRITE_BIT : 0;
    imageBarrier.dstAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
    imageBarrier.oldLayout = slot.layoutInitialized ? VK_IMAGE_LAYOUT_GENERAL : VK_IMAGE_LAYOUT_UNDEFINED;
    imageBarrier.newLayout = VK_IMAGE_LAYOUT_GENERAL;
    imageBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    imageBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    imageBarrier.image = slot.image;
    imageBarrier.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    vkCmdPipelineBarrier(
        command, slot.layoutInitialized ? VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT : VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 0, nullptr, 0, nullptr, 1, &imageBarrier);
    slot.layoutInitialized = true;

    VkDescriptorBufferInfo bufferInfo{};
    bufferInfo.buffer = copyBuffer;
    bufferInfo.offset = 0;
    bufferInfo.range = VK_WHOLE_SIZE;
    VkDescriptorImageInfo imageInfo{};
    imageInfo.imageView = slot.view;
    imageInfo.imageLayout = VK_IMAGE_LAYOUT_GENERAL;
    std::array<VkWriteDescriptorSet, 2> writes{};
    writes[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    writes[0].dstSet = slot.set;
    writes[0].dstBinding = 0;
    writes[0].descriptorCount = 1;
    writes[0].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    writes[0].pBufferInfo = &bufferInfo;
    writes[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    writes[1].dstSet = slot.set;
    writes[1].dstBinding = 1;
    writes[1].descriptorCount = 1;
    writes[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    writes[1].pImageInfo = &imageInfo;
    vkUpdateDescriptorSets(device_, static_cast<uint32_t>(writes.size()), writes.data(), 0, nullptr);

    BufferPreviewPush push{};
    for (uint32_t c = 0; c < 4; ++c) {
        push.black[c] = params.black[c];
        const float range = params.whiteLevel - params.black[c];
        push.invRange[c] = 1.0f / (range > 1.0f ? range : 1.0f);
        push.wb[c] = params.whiteBalance[c];
    }
    push.clipThreshold = std::max(0.90f, std::min(1.0f, params.clipThreshold));
    push.edgeStrength = std::max(0.0f, params.edgeStrength);
    push.chromaBlend = std::max(0.0f, std::min(1.0f, params.chromaBlend));
    push.highlightWarningThreshold = std::max(0.0f, std::min(1.0f, params.highlightWarningThreshold));
    push.shadowWarningThreshold = std::max(0.0f, std::min(1.0f, params.shadowWarningThreshold));
    push.width = rawWidth_;
    push.height = rawHeight_;
    push.pattern = params.pattern;

    vkCmdBindPipeline(command, VK_PIPELINE_BIND_POINT_COMPUTE, resources_.bufferPreviewPipeline);
    vkCmdBindDescriptorSets(command, VK_PIPELINE_BIND_POINT_COMPUTE, resources_.bufferPreviewLayout, 0, 1, &slot.set, 0,
                            nullptr);
    vkCmdPushConstants(command, resources_.bufferPreviewLayout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(push), &push);
    vkCmdDispatch(command, (outputWidth_ + 7u) / 8u, (outputHeight_ + 7u) / 8u, 1);
    vkCmdWriteTimestamp(command, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, resources_.bufferBenchmarkQueryPool,
                        queryBase + 4u);
    slot.timingPending = true;
}

void RawIntegrityProbe::consumeBufferPreviewBenchmark(uint32_t slotIndex) {
    if (slotIndex >= bufferBenchmarkSlots_.size()) return;
    auto& slot = bufferBenchmarkSlots_[slotIndex];
    if (!slot.timingPending || resources_.bufferBenchmarkQueryPool == VK_NULL_HANDLE) return;
    std::array<uint64_t, 5> query{};
    const VkResult result =
        vkGetQueryPoolResults(device_, resources_.bufferBenchmarkQueryPool, slotIndex * 5u, 5u, sizeof(query),
                              query.data(), sizeof(uint64_t), VK_QUERY_RESULT_64_BIT);
    if (result != VK_SUCCESS) return;
    slot.timingPending = false;
    productionRawBenchmarkNs_ += static_cast<double>(query[1] - query[0]) * timestampPeriodNs_;
    copyToBufferBenchmarkNs_ += static_cast<double>(query[3] - query[2]) * timestampPeriodNs_;
    bufferPreviewComputeNs_ += static_cast<double>(query[4] - query[3]) * timestampPeriodNs_;
    bufferPathTotalNs_ += static_cast<double>(query[4] - query[2]) * timestampPeriodNs_;
    ++bufferBenchmarkSamples_;
    if ((bufferBenchmarkSamples_ % 120u) == 0u) {
        const double n = static_cast<double>(bufferBenchmarkSamples_);
        std::ostringstream out;
        out << "RAW_ZERO_COPY_BUFFER_BENCHMARK samples=" << bufferBenchmarkSamples_
            << " productionCopyImagePlusPreviewMs=" << (productionRawBenchmarkNs_ / n / 1.0e6)
            << " copyImageToBufferMs=" << (copyToBufferBenchmarkNs_ / n / 1.0e6)
            << " bufferPreviewComputeMs=" << (bufferPreviewComputeNs_ / n / 1.0e6)
            << " bufferPathTotalMs=" << (bufferPathTotalNs_ / n / 1.0e6);
        __android_log_print(ANDROID_LOG_INFO, kTag, "%s", out.str().c_str());
        emit(out.str());
    }
}

bool RawIntegrityProbe::ensureExternalPipeline(const AdvancedCandidates& advanced) {
    if (!advanced.externalFormatAvailable || advanced.externalFormatImage == VK_NULL_HANDLE ||
        advanced.externalFormat == 0)
        return false;
    if (advanced.externalModel != VK_SAMPLER_YCBCR_MODEL_CONVERSION_RGB_IDENTITY) {
        std::ostringstream gated;
        gated << "RAW_ZERO_COPY_EXTERNAL_FORMAT_PARITY_GATED reason=non_identity_model"
              << " model=" << static_cast<int>(advanced.externalModel)
              << " suggestedRange=" << static_cast<int>(advanced.externalRange);
        __android_log_print(ANDROID_LOG_INFO, kTag, "%s", gated.str().c_str());
        emit(gated.str());
        return false;
    }
    // The AHB range field is explicitly only a suggestion in VK_ANDROID_external_memory_android_hardware_buffer.
    // Exact RAW recovery requires no narrow-range expansion, so deliberately test RGB_IDENTITY + ITU_FULL even
    // when the driver suggests ITU_NARROW. This is a characterization experiment only; parity decides usability.
    if (advanced.externalRange != VK_SAMPLER_YCBCR_RANGE_ITU_FULL) {
        std::ostringstream overrideLog;
        overrideLog << "RAW_ZERO_COPY_EXTERNAL_FORMAT_RANGE_OVERRIDE"
                    << " suggestedRange=" << static_cast<int>(advanced.externalRange)
                    << " testedRange=" << static_cast<int>(VK_SAMPLER_YCBCR_RANGE_ITU_FULL)
                    << " reason=exact_raw_requires_full_range";
        __android_log_print(ANDROID_LOG_INFO, kTag, "%s", overrideLog.str().c_str());
        emit(overrideLog.str());
    }
    if ((advanced.externalFormatFeatures & VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT) == 0) {
        __android_log_print(ANDROID_LOG_INFO, kTag, "%s",
                            "RAW_ZERO_COPY_EXTERNAL_FORMAT_PARITY_GATED reason=not_sampled");
        emit("RAW_ZERO_COPY_EXTERNAL_FORMAT_PARITY_GATED reason=not_sampled");
        return false;
    }
    if (resources_.externalPipeline != VK_NULL_HANDLE && resources_.externalViewImage == advanced.externalFormatImage &&
        resources_.externalViewFormat == advanced.externalFormat) {
        return true;
    }
    if (resources_.externalPipeline != VK_NULL_HANDLE) {
        __android_log_print(ANDROID_LOG_INFO, kTag, "%s",
                            "RAW_ZERO_COPY_EXTERNAL_FORMAT_PARITY_GATED reason=probe_already_bound_to_other_image");
        emit("RAW_ZERO_COPY_EXTERNAL_FORMAT_PARITY_GATED reason=probe_already_bound_to_other_image");
        return false;
    }

    VkExternalFormatANDROID externalFormatInfo{};
    externalFormatInfo.sType = VK_STRUCTURE_TYPE_EXTERNAL_FORMAT_ANDROID;
    externalFormatInfo.externalFormat = advanced.externalFormat;
    VkSamplerYcbcrConversionCreateInfo conversionInfo{};
    conversionInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_YCBCR_CONVERSION_CREATE_INFO;
    conversionInfo.pNext = &externalFormatInfo;
    conversionInfo.format = VK_FORMAT_UNDEFINED;
    conversionInfo.ycbcrModel = advanced.externalModel;
    conversionInfo.ycbcrRange = VK_SAMPLER_YCBCR_RANGE_ITU_FULL;
    conversionInfo.components = advanced.externalComponents;
    conversionInfo.xChromaOffset = advanced.externalX;
    conversionInfo.yChromaOffset = advanced.externalY;
    conversionInfo.chromaFilter = VK_FILTER_NEAREST;
    conversionInfo.forceExplicitReconstruction = VK_FALSE;
    VkResult result = vkCreateSamplerYcbcrConversion(device_, &conversionInfo, nullptr, &resources_.externalConversion);
    if (result != VK_SUCCESS) {
        {
            const std::string line =
                "RAW_ZERO_COPY_EXTERNAL_FORMAT_PARITY_GATED reason=create_conversion result=" + std::to_string(result);
            __android_log_print(ANDROID_LOG_INFO, kTag, "%s", line.c_str());
            emit(line);
        }
        return false;
    }

    VkSamplerYcbcrConversionInfo samplerConversion{};
    samplerConversion.sType = VK_STRUCTURE_TYPE_SAMPLER_YCBCR_CONVERSION_INFO;
    samplerConversion.conversion = resources_.externalConversion;
    VkSamplerCreateInfo samplerInfo{};
    samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    samplerInfo.pNext = &samplerConversion;
    samplerInfo.magFilter = VK_FILTER_NEAREST;
    samplerInfo.minFilter = VK_FILTER_NEAREST;
    samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
    samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.minLod = 0.0f;
    samplerInfo.maxLod = 0.0f;
    result = vkCreateSampler(device_, &samplerInfo, nullptr, &resources_.externalSampler);
    if (result != VK_SUCCESS) {
        {
            const std::string line =
                "RAW_ZERO_COPY_EXTERNAL_FORMAT_PARITY_GATED reason=create_sampler result=" + std::to_string(result);
            __android_log_print(ANDROID_LOG_INFO, kTag, "%s", line.c_str());
            emit(line);
        }
        return false;
    }

    VkSamplerYcbcrConversionInfo viewConversion{};
    viewConversion.sType = VK_STRUCTURE_TYPE_SAMPLER_YCBCR_CONVERSION_INFO;
    viewConversion.conversion = resources_.externalConversion;
    VkImageViewCreateInfo viewInfo{};
    viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    viewInfo.pNext = &viewConversion;
    viewInfo.image = advanced.externalFormatImage;
    viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
    viewInfo.format = VK_FORMAT_UNDEFINED;
    viewInfo.components = {VK_COMPONENT_SWIZZLE_IDENTITY, VK_COMPONENT_SWIZZLE_IDENTITY, VK_COMPONENT_SWIZZLE_IDENTITY,
                           VK_COMPONENT_SWIZZLE_IDENTITY};
    viewInfo.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    result = vkCreateImageView(device_, &viewInfo, nullptr, &resources_.externalView);
    if (result != VK_SUCCESS) {
        {
            const std::string line =
                "RAW_ZERO_COPY_EXTERNAL_FORMAT_PARITY_GATED reason=create_view result=" + std::to_string(result);
            __android_log_print(ANDROID_LOG_INFO, kTag, "%s", line.c_str());
            emit(line);
        }
        return false;
    }

    std::array<VkDescriptorSetLayoutBinding, 3> bindings{};
    bindings[0].binding = 0;
    bindings[0].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    bindings[0].descriptorCount = 1;
    bindings[0].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
    bindings[0].pImmutableSamplers = &resources_.externalSampler;
    bindings[1].binding = 1;
    bindings[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    bindings[1].descriptorCount = 1;
    bindings[1].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
    bindings[2].binding = 2;
    bindings[2].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    bindings[2].descriptorCount = 1;
    bindings[2].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
    VkDescriptorSetLayoutCreateInfo dslInfo{};
    dslInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    dslInfo.bindingCount = static_cast<uint32_t>(bindings.size());
    dslInfo.pBindings = bindings.data();
    result = vkCreateDescriptorSetLayout(device_, &dslInfo, nullptr, &resources_.externalDsl);
    if (result != VK_SUCCESS) {
        {
            const std::string line =
                "RAW_ZERO_COPY_EXTERNAL_FORMAT_PARITY_GATED reason=create_dsl result=" + std::to_string(result);
            __android_log_print(ANDROID_LOG_INFO, kTag, "%s", line.c_str());
            emit(line);
        }
        return false;
    }

    VkPushConstantRange push{};
    push.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
    push.offset = 0;
    push.size = sizeof(ExternalPush);
    VkPipelineLayoutCreateInfo layoutInfo{};
    layoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    layoutInfo.setLayoutCount = 1;
    layoutInfo.pSetLayouts = &resources_.externalDsl;
    layoutInfo.pushConstantRangeCount = 1;
    layoutInfo.pPushConstantRanges = &push;
    result = vkCreatePipelineLayout(device_, &layoutInfo, nullptr, &resources_.externalLayout);
    if (result != VK_SUCCESS) {
        {
            const std::string line =
                "RAW_ZERO_COPY_EXTERNAL_FORMAT_PARITY_GATED reason=create_layout result=" + std::to_string(result);
            __android_log_print(ANDROID_LOG_INFO, kTag, "%s", line.c_str());
            emit(line);
        }
        return false;
    }

    VkShaderModuleCreateInfo moduleInfo{};
    moduleInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    moduleInfo.codeSize = raw_zero_copy_external_compare_spv_size;
    moduleInfo.pCode = reinterpret_cast<const uint32_t*>(raw_zero_copy_external_compare_spv);
    VkShaderModule module = VK_NULL_HANDLE;
    result = vkCreateShaderModule(device_, &moduleInfo, nullptr, &module);
    if (result != VK_SUCCESS) {
        {
            const std::string line =
                "RAW_ZERO_COPY_EXTERNAL_FORMAT_PARITY_GATED reason=create_shader result=" + std::to_string(result);
            __android_log_print(ANDROID_LOG_INFO, kTag, "%s", line.c_str());
            emit(line);
        }
        return false;
    }
    VkPipelineShaderStageCreateInfo stage{};
    stage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stage.stage = VK_SHADER_STAGE_COMPUTE_BIT;
    stage.module = module;
    stage.pName = "main";
    VkComputePipelineCreateInfo pipelineInfo{};
    pipelineInfo.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
    pipelineInfo.stage = stage;
    pipelineInfo.layout = resources_.externalLayout;
    result = vkCreateComputePipelines(device_, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &resources_.externalPipeline);
    vkDestroyShaderModule(device_, module, nullptr);
    if (result != VK_SUCCESS) {
        {
            const std::string line =
                "RAW_ZERO_COPY_EXTERNAL_FORMAT_PARITY_GATED reason=create_pipeline result=" + std::to_string(result);
            __android_log_print(ANDROID_LOG_INFO, kTag, "%s", line.c_str());
            emit(line);
        }
        return false;
    }

    std::array<VkDescriptorPoolSize, 3> poolSizes{};
    // YCbCr conversions may consume more than one combined-image descriptor internally.
    // Reserve the maximum plane count rather than assuming one descriptor.
    poolSizes[0] = {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 4};
    poolSizes[1] = {VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1};
    poolSizes[2] = {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1};
    VkDescriptorPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    poolInfo.maxSets = 1;
    poolInfo.poolSizeCount = static_cast<uint32_t>(poolSizes.size());
    poolInfo.pPoolSizes = poolSizes.data();
    result = vkCreateDescriptorPool(device_, &poolInfo, nullptr, &resources_.externalPool);
    if (result != VK_SUCCESS) {
        {
            const std::string line =
                "RAW_ZERO_COPY_EXTERNAL_FORMAT_PARITY_GATED reason=create_pool result=" + std::to_string(result);
            __android_log_print(ANDROID_LOG_INFO, kTag, "%s", line.c_str());
            emit(line);
        }
        return false;
    }
    VkDescriptorSetAllocateInfo alloc{};
    alloc.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    alloc.descriptorPool = resources_.externalPool;
    alloc.descriptorSetCount = 1;
    alloc.pSetLayouts = &resources_.externalDsl;
    result = vkAllocateDescriptorSets(device_, &alloc, &resources_.externalSet);
    if (result != VK_SUCCESS) {
        {
            const std::string line =
                "RAW_ZERO_COPY_EXTERNAL_FORMAT_PARITY_GATED reason=allocate_set result=" + std::to_string(result);
            __android_log_print(ANDROID_LOG_INFO, kTag, "%s", line.c_str());
            emit(line);
        }
        return false;
    }

    resources_.externalViewImage = advanced.externalFormatImage;
    resources_.externalViewFormat = advanced.externalFormat;
    {
        std::ostringstream ready;
        ready << "RAW_ZERO_COPY_EXTERNAL_FORMAT_PARITY_READY"
              << " model=RGB_IDENTITY"
              << " testedRange=FULL"
              << " suggestedRange=" << static_cast<int>(advanced.externalRange) << " immutableSampler=yes";
        __android_log_print(ANDROID_LOG_INFO, kTag, "%s", ready.str().c_str());
        emit(ready.str());
    }
    return true;
}

void RawIntegrityProbe::record(VkCommandBuffer command, VkImageView rawView, VkImageView referenceRawView,
                               bool compareRawReference, VkImageView linearCandidateView, bool linearCandidateAvailable,
                               const AdvancedCandidates& advanced, VkImageView linearView, VkImageView displayView,
                               VkImage linearImage, VkImage displayImage, uint32_t slotIndex, uint64_t timestampNs) {
    if (!readyForCapture()) return;

    std::array<VkDescriptorImageInfo, 7> images{};
    images[0].imageView = rawView;
    images[0].imageLayout = VK_IMAGE_LAYOUT_GENERAL;
    images[1].imageView = linearView;
    images[1].imageLayout = VK_IMAGE_LAYOUT_GENERAL;
    images[2].imageView = displayView;
    images[2].imageLayout = VK_IMAGE_LAYOUT_GENERAL;
    images[3].imageView = referenceRawView;
    images[3].imageLayout = VK_IMAGE_LAYOUT_GENERAL;
    images[4].imageView = linearCandidateAvailable ? linearCandidateView : referenceRawView;
    images[4].imageLayout = VK_IMAGE_LAYOUT_GENERAL;
    images[5].imageView = (advanced.drmAvailable && (advanced.drmUsage & VK_IMAGE_USAGE_STORAGE_BIT) != 0)
                              ? advanced.drmView
                              : referenceRawView;
    images[5].imageLayout = VK_IMAGE_LAYOUT_GENERAL;
    const bool bufferPreviewAvailable = advanced.copyBufferAvailable && slotIndex < bufferBenchmarkSlots_.size() &&
                                        bufferBenchmarkSlots_[slotIndex].layoutInitialized &&
                                        bufferBenchmarkSlots_[slotIndex].view != VK_NULL_HANDLE;
    images[6].imageView = bufferPreviewAvailable ? bufferBenchmarkSlots_[slotIndex].view : linearView;
    images[6].imageLayout = VK_IMAGE_LAYOUT_GENERAL;
    VkDescriptorBufferInfo buffer{};
    buffer.buffer = resources_.buffer;
    buffer.offset = 0;
    buffer.range = VK_WHOLE_SIZE;
    std::array<VkWriteDescriptorSet, 11> writes{};
    for (uint32_t i = 0; i < 4; ++i) {
        writes[i].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[i].dstSet = resources_.set;
        writes[i].dstBinding = i;
        writes[i].descriptorCount = 1;
        writes[i].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
        writes[i].pImageInfo = &images[i];
    }
    writes[4].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    writes[4].dstSet = resources_.set;
    writes[4].dstBinding = 4;
    writes[4].descriptorCount = 1;
    writes[4].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    writes[4].pBufferInfo = &buffer;
    VkDescriptorImageInfo sampled{};
    sampled.sampler = resources_.rawSampler;
    sampled.imageView = rawView;
    sampled.imageLayout = VK_IMAGE_LAYOUT_GENERAL;
    writes[5].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    writes[5].dstSet = resources_.set;
    writes[5].dstBinding = 5;
    writes[5].descriptorCount = 1;
    writes[5].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    writes[5].pImageInfo = &sampled;
    writes[6].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    writes[6].dstSet = resources_.set;
    writes[6].dstBinding = 6;
    writes[6].descriptorCount = 1;
    writes[6].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    writes[6].pImageInfo = &images[4];
    writes[7].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    writes[7].dstSet = resources_.set;
    writes[7].dstBinding = 7;
    writes[7].descriptorCount = 1;
    writes[7].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    writes[7].pImageInfo = &images[5];
    VkDescriptorImageInfo drmSampled{};
    drmSampled.sampler = resources_.rawSampler;
    drmSampled.imageView =
        (advanced.drmAvailable && (advanced.drmUsage & VK_IMAGE_USAGE_SAMPLED_BIT) != 0) ? advanced.drmView : rawView;
    drmSampled.imageLayout = VK_IMAGE_LAYOUT_GENERAL;
    writes[8].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    writes[8].dstSet = resources_.set;
    writes[8].dstBinding = 8;
    writes[8].descriptorCount = 1;
    writes[8].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    writes[8].pImageInfo = &drmSampled;
    VkDescriptorBufferInfo copyBufferInfo{};
    copyBufferInfo.buffer = advanced.copyBufferAvailable ? advanced.copyBuffer : resources_.buffer;
    copyBufferInfo.offset = 0;
    copyBufferInfo.range = VK_WHOLE_SIZE;
    writes[9].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    writes[9].dstSet = resources_.set;
    writes[9].dstBinding = 9;
    writes[9].descriptorCount = 1;
    writes[9].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    writes[9].pBufferInfo = &copyBufferInfo;
    writes[10].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    writes[10].dstSet = resources_.set;
    writes[10].dstBinding = 10;
    writes[10].descriptorCount = 1;
    writes[10].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    writes[10].pImageInfo = &images[6];
    vkUpdateDescriptorSets(device_, static_cast<uint32_t>(writes.size()), writes.data(), 0, nullptr);

    std::array<VkImageMemoryBarrier, 3> barriers{};
    barriers[0].sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barriers[0].srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
    barriers[0].dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    barriers[0].oldLayout = VK_IMAGE_LAYOUT_GENERAL;
    barriers[0].newLayout = VK_IMAGE_LAYOUT_GENERAL;
    barriers[0].srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barriers[0].dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barriers[0].image = linearImage;
    barriers[0].subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    barriers[1] = barriers[0];
    barriers[1].image = displayImage;
    uint32_t barrierCount = 2u;
    if (bufferPreviewAvailable) {
        barriers[2] = barriers[0];
        barriers[2].image = bufferBenchmarkSlots_[slotIndex].image;
        barrierCount = 3u;
    }
    vkCmdPipelineBarrier(command, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 0,
                         nullptr, 0, nullptr, barrierCount, barriers.data());

    vkCmdFillBuffer(command, resources_.buffer, 0, static_cast<VkDeviceSize>(kFullCompareStatsBase) * sizeof(uint32_t),
                    0xdeadbeefu);
    vkCmdFillBuffer(command, resources_.buffer, static_cast<VkDeviceSize>(kFullCompareStatsBase) * sizeof(uint32_t),
                    static_cast<VkDeviceSize>(kFullCompareStatsWords + kCandidateStatsWords) * sizeof(uint32_t), 0u);
    // firstMismatchLinearIndex fields use atomicMin, so initialize each to UINT32_MAX.
    const uint32_t firstMismatchWords[] = {
        kFullCompareStatsBase + 6u,     kCandidateStatsBase + 6u,       kCandidateStatsBase + 8u + 6u,
        kCandidateStatsBase + 16u + 6u, kCandidateStatsBase + 24u + 6u, kCandidateStatsBase + 32u + 6u,
        kCandidateStatsBase + 40u + 6u, kCandidateStatsBase + 48u + 6u, kCandidateStatsBase + 56u + 6u,
        kBufferPreviewStatsBase + 6u,   kExternalStatsBase + 6u};
    for (uint32_t word : firstMismatchWords) {
        vkCmdFillBuffer(command, resources_.buffer, static_cast<VkDeviceSize>(word) * sizeof(uint32_t),
                        sizeof(uint32_t), 0xffffffffu);
    }
    VkBufferMemoryBarrier fillBarrier{};
    fillBarrier.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
    fillBarrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    fillBarrier.dstAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
    fillBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    fillBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    fillBarrier.buffer = resources_.buffer;
    fillBarrier.offset = 0;
    fillBarrier.size = VK_WHOLE_SIZE;
    vkCmdPipelineBarrier(command, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 0, nullptr,
                         1, &fillBarrier, 0, nullptr);

    const bool drmStorageAvailable = advanced.drmAvailable && (advanced.drmUsage & VK_IMAGE_USAGE_STORAGE_BIT) != 0;
    const bool drmSampledAvailable = advanced.drmAvailable && (advanced.drmUsage & VK_IMAGE_USAGE_SAMPLED_BIT) != 0;
    const Push push{rawWidth_,
                    rawHeight_,
                    outputWidth_,
                    outputHeight_,
                    linearCandidateAvailable ? 1u : 0u,
                    drmStorageAvailable ? 1u : 0u,
                    drmSampledAvailable ? 1u : 0u,
                    advanced.copyBufferAvailable ? 1u : 0u,
                    bufferPreviewAvailable ? 1u : 0u};
    vkCmdBindPipeline(command, VK_PIPELINE_BIND_POINT_COMPUTE, resources_.pipeline);
    vkCmdBindDescriptorSets(command, VK_PIPELINE_BIND_POINT_COMPUTE, resources_.layout, 0, 1, &resources_.set, 0,
                            nullptr);
    vkCmdPushConstants(command, resources_.layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(push), &push);
    vkCmdDispatch(command, 8, 8, 1);

    if (compareRawReference) {
        // Full-frame direct-import vs transfer-copy comparison. Only 8 uints are
        // read back; the ~12.5M pixel comparison stays entirely on the GPU.
        vkCmdBindPipeline(command, VK_PIPELINE_BIND_POINT_COMPUTE, resources_.fullComparePipeline);
        vkCmdDispatch(command, (rawWidth_ + 15u) / 16u, (rawHeight_ + 15u) / 16u, 1);
        vkCmdBindPipeline(command, VK_PIPELINE_BIND_POINT_COMPUTE, resources_.pathPipeline);
        vkCmdDispatch(command, (rawWidth_ + 15u) / 16u, (rawHeight_ + 15u) / 16u, 1);

        if (advanced.externalFormatAvailable && ensureExternalPipeline(advanced)) {
            VkDescriptorImageInfo extImage{};
            extImage.imageView = resources_.externalView;
            extImage.imageLayout = VK_IMAGE_LAYOUT_GENERAL;
            VkDescriptorImageInfo refImage{};
            refImage.imageView = referenceRawView;
            refImage.imageLayout = VK_IMAGE_LAYOUT_GENERAL;
            VkDescriptorBufferInfo extStats{};
            extStats.buffer = resources_.buffer;
            extStats.offset = 0;
            extStats.range = VK_WHOLE_SIZE;
            std::array<VkWriteDescriptorSet, 3> extWrites{};
            extWrites[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            extWrites[0].dstSet = resources_.externalSet;
            extWrites[0].dstBinding = 0;
            extWrites[0].descriptorCount = 1;
            extWrites[0].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
            extWrites[0].pImageInfo = &extImage;
            extWrites[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            extWrites[1].dstSet = resources_.externalSet;
            extWrites[1].dstBinding = 1;
            extWrites[1].descriptorCount = 1;
            extWrites[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
            extWrites[1].pImageInfo = &refImage;
            extWrites[2].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            extWrites[2].dstSet = resources_.externalSet;
            extWrites[2].dstBinding = 2;
            extWrites[2].descriptorCount = 1;
            extWrites[2].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
            extWrites[2].pBufferInfo = &extStats;
            vkUpdateDescriptorSets(device_, static_cast<uint32_t>(extWrites.size()), extWrites.data(), 0, nullptr);
            const ExternalPush externalPush{rawWidth_, rawHeight_, kExternalStatsBase, 0u};
            vkCmdBindPipeline(command, VK_PIPELINE_BIND_POINT_COMPUTE, resources_.externalPipeline);
            vkCmdBindDescriptorSets(command, VK_PIPELINE_BIND_POINT_COMPUTE, resources_.externalLayout, 0, 1,
                                    &resources_.externalSet, 0, nullptr);
            vkCmdPushConstants(command, resources_.externalLayout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(externalPush),
                               &externalPush);
            vkCmdDispatch(command, (rawWidth_ + 15u) / 16u, (rawHeight_ + 15u) / 16u, 1);
            pendingExternalCandidateAvailable_ = true;
        }
    }

    VkBufferMemoryBarrier hostBarrier{};
    hostBarrier.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
    hostBarrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
    hostBarrier.dstAccessMask = VK_ACCESS_HOST_READ_BIT;
    hostBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    hostBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    hostBarrier.buffer = resources_.buffer;
    hostBarrier.offset = 0;
    hostBarrier.size = VK_WHOLE_SIZE;
    vkCmdPipelineBarrier(command, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_HOST_BIT, 0, 0, nullptr, 1,
                         &hostBarrier, 0, nullptr);

    pendingSlot_ = static_cast<int>(slotIndex);
    pendingTimestamp_ = timestampNs;
    pendingRawReferenceComparison_ = compareRawReference;
    pendingLinearCandidateAvailable_ = linearCandidateAvailable;
    pendingDrmStorageAvailable_ = drmStorageAvailable;
    pendingDrmSampledAvailable_ = drmSampledAvailable;
    pendingCopyBufferAvailable_ = advanced.copyBufferAvailable;
    pendingBufferPreviewAvailable_ = bufferPreviewAvailable;
}

float RawIntegrityProbe::wordToFloat(uint32_t word) {
    float value = 0.0f;
    std::memcpy(&value, &word, sizeof(value));
    return value;
}

void RawIntegrityProbe::complete(uint32_t slotIndex) {
    // Per-slot benchmark timing continues after the one-shot integrity probe.
    consumeBufferPreviewBenchmark(slotIndex);
    if (pendingBufImportSlot_ == static_cast<int>(slotIndex) && !bufImportDone_ &&
        resources_.bufImportMapped != nullptr) {
        if (!resources_.bufImportCoherent) {
            VkMappedMemoryRange range{};
            range.sType = VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE;
            range.memory = resources_.bufImportMemory;
            range.offset = 0;
            range.size = VK_WHOLE_SIZE;
            vkCheck(vkInvalidateMappedMemoryRanges(device_, 1, &range), "invalidate buffer-import probe memory");
        }
        const uint32_t* words = static_cast<const uint32_t*>(resources_.bufImportMapped);
        const uint32_t diff = words[0];
        const uint32_t maxAbs = words[1];
        const uint32_t first = words[6];
        const uint64_t samples = static_cast<uint64_t>(rawWidth_) * rawHeight_;
        std::ostringstream out;
        out << "RAW_GPU_ZERO_COPY_CANDIDATE path=BUFFER_IMPORT_ZERO_COPY " << (diff == 0 ? "PASS" : "FAIL")
            << " timestampNs=" << pendingBufImportTimestamp_ << " size=" << rawWidth_ << 'x' << rawHeight_
            << " samples=" << samples << " diff=" << diff << " maxAbs=" << maxAbs << " parityDiff=" << words[2] << ','
            << words[3] << ',' << words[4] << ',' << words[5];
        if (diff != 0u && first != 0xffffffffu)
            out << " firstX=" << (first % rawWidth_) << " firstY=" << (first / rawWidth_);
        __android_log_print(ANDROID_LOG_INFO, kTag, "%s", out.str().c_str());
        emit(out.str());
        bufImportDone_ = true;
        pendingBufImportSlot_ = -1;
    }
    if (pendingSlot_ != static_cast<int>(slotIndex) || done_ || resources_.mapped == nullptr) return;
    if (!resources_.coherent) {
        VkMappedMemoryRange range{};
        range.sType = VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE;
        range.memory = resources_.memory;
        range.offset = 0;
        range.size = VK_WHOLE_SIZE;
        vkCheck(vkInvalidateMappedMemoryRanges(device_, 1, &range), "invalidate probe memory");
    }

    constexpr uint32_t kDense = 64;
    constexpr uint32_t kDenseSamples = kDense * kDense;
    constexpr uint32_t kDenseWords = kDenseSamples * 9;
    constexpr uint32_t kBottomW = 32;
    constexpr uint32_t kBottomH = 32;
    const auto* words = static_cast<const uint32_t*>(resources_.mapped);
    uint32_t sentinelWords = 0, coordinateErrors = 0;
    uint32_t rawMin = UINT32_MAX, rawMax = 0, finiteLinear = 0, finiteDisplay = 0;
    double rawSum = 0.0, rawDx = 0.0, rawDy = 0.0;
    double linearDx = 0.0, linearDy = 0.0, displayDx = 0.0, displayDy = 0.0;
    std::array<uint32_t, kDenseSamples> rawValues{};
    std::array<double, kDenseSamples> linearLuma{}, displayLuma{};
    uint32_t rawReferenceDiff = 0, rawReferenceMaxAbs = 0;
    uint32_t firstRawReferenceX = 0, firstRawReferenceY = 0, firstRawValue = 0, firstReferenceValue = 0;
    bool haveFirstRawReferenceDiff = false;
    const uint32_t centerX = (rawWidth_ / 2u) & ~1u;
    const uint32_t centerY = (rawHeight_ / 2u) & ~1u;
    const uint32_t startX = (centerX >= kDense ? centerX - kDense : 0u) & ~1u;
    const uint32_t startY = (centerY >= kDense ? centerY - kDense : 0u) & ~1u;
    for (uint32_t i = 0; i < kDenseSamples; ++i) {
        const uint32_t base = i * 9u;
        for (uint32_t k = 0; k < 9; ++k)
            if (words[base + k] == 0xdeadbeefu) ++sentinelWords;
        const uint32_t gx = i % kDense, gy = i / kDense;
        const uint32_t rx = std::min(startX + gx * 2u, rawWidth_ - 1u);
        const uint32_t ry = std::min(startY + gy * 2u, rawHeight_ - 1u);
        const uint32_t expected = (ry << 16u) | (rx & 0xffffu);
        if (words[base + 7] != expected) ++coordinateErrors;
        const uint32_t raw = words[base + 0];
        if (pendingRawReferenceComparison_) {
            const uint32_t referenceRaw = words[base + 8];
            if (raw != referenceRaw) {
                ++rawReferenceDiff;
                rawReferenceMaxAbs =
                    std::max(rawReferenceMaxAbs, raw > referenceRaw ? raw - referenceRaw : referenceRaw - raw);
                if (!haveFirstRawReferenceDiff) {
                    firstRawReferenceX = rx;
                    firstRawReferenceY = ry;
                    firstRawValue = raw;
                    firstReferenceValue = referenceRaw;
                    haveFirstRawReferenceDiff = true;
                }
            }
        }
        rawValues[i] = raw;
        rawMin = std::min(rawMin, raw);
        rawMax = std::max(rawMax, raw);
        rawSum += raw;
        const float lr = wordToFloat(words[base + 1]), lg = wordToFloat(words[base + 2]),
                    lb = wordToFloat(words[base + 3]);
        const float dr = wordToFloat(words[base + 4]), dg = wordToFloat(words[base + 5]),
                    db = wordToFloat(words[base + 6]);
        if (std::isfinite(lr) && std::isfinite(lg) && std::isfinite(lb)) ++finiteLinear;
        if (std::isfinite(dr) && std::isfinite(dg) && std::isfinite(db)) ++finiteDisplay;
        linearLuma[i] = (static_cast<double>(lr) + lg + lb) / 3.0;
        displayLuma[i] = (static_cast<double>(dr) + dg + db) / 3.0;
    }

    uint32_t dxCount = 0, dyCount = 0;
    for (uint32_t y = 0; y < kDense; ++y)
        for (uint32_t x = 0; x < kDense; ++x) {
            const uint32_t i = y * kDense + x;
            if (x) {
                const uint32_t j = i - 1;
                rawDx += std::abs(static_cast<double>(rawValues[i]) - rawValues[j]);
                linearDx += std::abs(linearLuma[i] - linearLuma[j]);
                displayDx += std::abs(displayLuma[i] - displayLuma[j]);
                ++dxCount;
            }
            if (y) {
                const uint32_t j = i - kDense;
                rawDy += std::abs(static_cast<double>(rawValues[i]) - rawValues[j]);
                linearDy += std::abs(linearLuma[i] - linearLuma[j]);
                displayDy += std::abs(displayLuma[i] - displayLuma[j]);
                ++dyCount;
            }
        }
    rawDx /= std::max(1u, dxCount);
    linearDx /= std::max(1u, dxCount);
    displayDx /= std::max(1u, dxCount);
    rawDy /= std::max(1u, dyCount);
    linearDy /= std::max(1u, dyCount);
    displayDy /= std::max(1u, dyCount);
    const auto ratio = [](double a, double b) { return a / std::max(b, 1.0e-9); };

    std::ostringstream summary;
    summary << "DENSE_STAGE_PROBE timestampNs=" << pendingTimestamp_ << " rawWindowStart=" << startX << ',' << startY
            << " rawStep=2 size=64x64"
            << " rawMin=" << rawMin << " rawMax=" << rawMax << " rawMean=" << rawSum / kDenseSamples
            << " rawDxDy=" << ratio(rawDx, rawDy) << " linearFinite=" << finiteLinear << '/' << kDenseSamples
            << " linearDxDy=" << ratio(linearDx, linearDy) << " displayFinite=" << finiteDisplay << '/' << kDenseSamples
            << " displayDxDy=" << ratio(displayDx, displayDy) << " sentinelWords=" << sentinelWords
            << " coordinateErrors=" << coordinateErrors;
    __android_log_print(ANDROID_LOG_INFO, kTag, "%s", summary.str().c_str());
    emit(summary.str());

    for (uint32_t y = 0; y < kDense; ++y) {
        double rr = 0, ll = 0, dd = 0;
        for (uint32_t x = 0; x < kDense; ++x) {
            const auto i = y * kDense + x;
            rr += rawValues[i];
            ll += linearLuma[i];
            dd += displayLuma[i];
        }
        std::ostringstream row;
        row << "DENSE_ROW y=" << y << " rawMean=" << rr / kDense << " linearMean=" << ll / kDense
            << " displayMean=" << dd / kDense;
        emit(row.str());
    }
    for (uint32_t x = 0; x < kDense; ++x) {
        double rr = 0, ll = 0, dd = 0;
        for (uint32_t y = 0; y < kDense; ++y) {
            const auto i = y * kDense + x;
            rr += rawValues[i];
            ll += linearLuma[i];
            dd += displayLuma[i];
        }
        std::ostringstream col;
        col << "DENSE_COL x=" << x << " rawMean=" << rr / kDense << " linearMean=" << ll / kDense
            << " displayMean=" << dd / kDense;
        emit(col.str());
    }

    uint32_t bottomSentinels = 0, bottomCoordErrors = 0;
    for (uint32_t gy = 0; gy < kBottomH; ++gy) {
        uint32_t mn = UINT32_MAX, mx = 0, zeros = 0;
        double sum = 0, evenSum = 0, oddSum = 0;
        uint32_t evenN = 0, oddN = 0;
        const uint32_t by = rawHeight_ - kBottomH + gy;
        for (uint32_t gx = 0; gx < kBottomW; ++gx) {
            const uint32_t base = kDenseWords + (gy * kBottomW + gx) * 3u;
            if (words[base] == 0xdeadbeefu || words[base + 1] == 0xdeadbeefu || words[base + 2] == 0xdeadbeefu)
                ++bottomSentinels;
            const uint32_t bx = (gx * (rawWidth_ - 1u)) / (kBottomW - 1u);
            const uint32_t expected = (by << 16u) | (bx & 0xffffu);
            if (words[base + 1] != expected) ++bottomCoordErrors;
            const uint32_t value = words[base];
            if (pendingRawReferenceComparison_) {
                const uint32_t referenceRaw = words[base + 2];
                if (value != referenceRaw) {
                    ++rawReferenceDiff;
                    rawReferenceMaxAbs = std::max(rawReferenceMaxAbs,
                                                  value > referenceRaw ? value - referenceRaw : referenceRaw - value);
                    if (!haveFirstRawReferenceDiff) {
                        firstRawReferenceX = bx;
                        firstRawReferenceY = by;
                        firstRawValue = value;
                        firstReferenceValue = referenceRaw;
                        haveFirstRawReferenceDiff = true;
                    }
                }
            }
            mn = std::min(mn, value);
            mx = std::max(mx, value);
            sum += value;
            if (value == 0) ++zeros;
            if ((bx & 1u) == 0) {
                evenSum += value;
                ++evenN;
            } else {
                oddSum += value;
                ++oddN;
            }
        }
        std::ostringstream edge;
        edge << "BOTTOM_EDGE_ROW rawY=" << by << " min=" << mn << " max=" << mx << " mean=" << sum / kBottomW
             << " zeroCount=" << zeros << '/' << kBottomW << " evenXMean=" << (evenN ? evenSum / evenN : 0.0)
             << " oddXMean=" << (oddN ? oddSum / oddN : 0.0);
        emit(edge.str());
    }
    if (pendingRawReferenceComparison_) {
        const uint32_t fullDiff = words[kFullCompareStatsBase + 0u];
        const uint32_t fullMaxAbs = words[kFullCompareStatsBase + 1u];
        const uint32_t firstLinear = words[kFullCompareStatsBase + 6u];
        std::ostringstream full;
        full << "RAW_GPU_ZERO_COPY_FULL " << (fullDiff == 0 ? "PASS" : "FAIL") << " timestampNs=" << pendingTimestamp_
             << " size=" << rawWidth_ << 'x' << rawHeight_
             << " samples=" << (static_cast<uint64_t>(rawWidth_) * rawHeight_) << " diff=" << fullDiff
             << " maxAbs=" << fullMaxAbs << " parityDiff=" << words[kFullCompareStatsBase + 2u] << ','
             << words[kFullCompareStatsBase + 3u] << ',' << words[kFullCompareStatsBase + 4u] << ','
             << words[kFullCompareStatsBase + 5u];
        if (fullDiff != 0u && firstLinear != 0xffffffffu) {
            full << " firstX=" << (firstLinear % rawWidth_) << " firstY=" << (firstLinear / rawWidth_);
        }
        __android_log_print(ANDROID_LOG_INFO, kTag, "%s", full.str().c_str());
        emit(full.str());

        std::ostringstream ab;
        ab << "RAW_GPU_ZERO_COPY_AB " << (rawReferenceDiff == 0 ? "PASS" : "FAIL")
           << " timestampNs=" << pendingTimestamp_ << " samples=" << (kDenseSamples + kBottomW * kBottomH)
           << " diff=" << rawReferenceDiff << " maxAbs=" << rawReferenceMaxAbs;
        if (haveFirstRawReferenceDiff) {
            ab << " firstX=" << firstRawReferenceX << " firstY=" << firstRawReferenceY << " direct=" << firstRawValue
               << " copied=" << firstReferenceValue;
        }
        __android_log_print(ANDROID_LOG_INFO, kTag, "%s", ab.str().c_str());
        emit(ab.str());

        const auto logCandidate = [&](const char* name, uint32_t base, uint64_t validSamples) {
            const uint32_t diff = words[base + 0u];
            const uint32_t maxAbs = words[base + 1u];
            const uint32_t firstLinear = words[base + 6u];
            std::ostringstream out;
            out << "RAW_GPU_ZERO_COPY_CANDIDATE path=" << name << " " << (diff == 0 ? "PASS" : "FAIL")
                << " timestampNs=" << pendingTimestamp_ << " size=" << rawWidth_ << 'x' << rawHeight_
                << " samples=" << validSamples << " diff=" << diff << " maxAbs=" << maxAbs
                << " parityDiff=" << words[base + 2u] << ',' << words[base + 3u] << ',' << words[base + 4u] << ','
                << words[base + 5u];
            if (diff != 0u && firstLinear != 0xffffffffu)
                out << " firstX=" << (firstLinear % rawWidth_) << " firstY=" << (firstLinear / rawWidth_);
            __android_log_print(ANDROID_LOG_INFO, kTag, "%s", out.str().c_str());
            emit(out.str());
        };
        logCandidate("SAMPLED_TEXEL_FETCH", kCandidateStatsBase + 0u, static_cast<uint64_t>(rawWidth_) * rawHeight_);
        const char* pitchNames[] = {"STORAGE_AS_PITCH_4096", "STORAGE_AS_PITCH_4064", "STORAGE_AS_PITCH_4080"};
        for (uint32_t i = 0; i < 3u; ++i) {
            const uint32_t base = kCandidateStatsBase + 8u + i * 8u;
            logCandidate(pitchNames[i], base, words[base + 7u]);
        }
        if (pendingLinearCandidateAvailable_) {
            logCandidate("LINEAR_IMAGE_IMPORT", kCandidateStatsBase + 32u,
                         static_cast<uint64_t>(rawWidth_) * rawHeight_);
        } else {
            std::ostringstream unavailable;
            unavailable << "RAW_GPU_ZERO_COPY_CANDIDATE path=LINEAR_IMAGE_IMPORT UNAVAILABLE"
                        << " timestampNs=" << pendingTimestamp_ << " size=" << rawWidth_ << 'x' << rawHeight_;
            __android_log_print(ANDROID_LOG_INFO, kTag, "%s", unavailable.str().c_str());
            emit(unavailable.str());
        }
        if (pendingDrmStorageAvailable_)
            logCandidate("DRM_MODIFIER_STORAGE", kCandidateStatsBase + 40u,
                         static_cast<uint64_t>(rawWidth_) * rawHeight_);
        else
            emit("RAW_GPU_ZERO_COPY_CANDIDATE path=DRM_MODIFIER_STORAGE UNAVAILABLE");
        if (pendingDrmSampledAvailable_)
            logCandidate("DRM_MODIFIER_SAMPLED", kCandidateStatsBase + 48u,
                         static_cast<uint64_t>(rawWidth_) * rawHeight_);
        else
            emit("RAW_GPU_ZERO_COPY_CANDIDATE path=DRM_MODIFIER_SAMPLED UNAVAILABLE");
        if (pendingCopyBufferAvailable_)
            logCandidate("COPY_IMAGE_TO_BUFFER", kCandidateStatsBase + 56u,
                         static_cast<uint64_t>(rawWidth_) * rawHeight_);
        else
            emit("RAW_GPU_ZERO_COPY_CANDIDATE path=COPY_IMAGE_TO_BUFFER UNAVAILABLE");
        if (pendingBufferPreviewAvailable_) {
            const uint32_t base = kBufferPreviewStatsBase;
            const uint32_t diff = words[base + 0u];
            const float maxDelta = wordToFloat(words[base + 1u]);
            const uint32_t firstLinear = words[base + 6u];
            std::ostringstream out;
            out << "RAW_GPU_ZERO_COPY_CANDIDATE path=COPY_BUFFER_RAW_PREVIEW_OUTPUT"
                << " " << (diff == 0 ? "PASS" : "FAIL") << " timestampNs=" << pendingTimestamp_
                << " size=" << outputWidth_ << 'x' << outputHeight_
                << " samples=" << (static_cast<uint64_t>(outputWidth_) * outputHeight_) << " diff=" << diff
                << " maxDelta=" << maxDelta << " parityDiff=" << words[base + 2u] << ',' << words[base + 3u] << ','
                << words[base + 4u] << ',' << words[base + 5u];
            if (diff != 0u && firstLinear != 0xffffffffu)
                out << " firstX=" << (firstLinear % outputWidth_) << " firstY=" << (firstLinear / outputWidth_);
            __android_log_print(ANDROID_LOG_INFO, kTag, "%s", out.str().c_str());
            emit(out.str());
        } else {
            emit("RAW_GPU_ZERO_COPY_CANDIDATE path=COPY_BUFFER_RAW_PREVIEW_OUTPUT UNAVAILABLE");
        }
        if (pendingExternalCandidateAvailable_) {
            logCandidate("EXTERNAL_FORMAT_SAMPLED", kExternalStatsBase, static_cast<uint64_t>(rawWidth_) * rawHeight_);
        } else {
            const std::string unavailable =
                "RAW_GPU_ZERO_COPY_CANDIDATE path=EXTERNAL_FORMAT_SAMPLED UNAVAILABLE_OR_GATED";
            __android_log_print(ANDROID_LOG_INFO, kTag, "%s", unavailable.c_str());
            emit(unavailable);
        }
    }

    std::ostringstream edgeSummary;
    edgeSummary << "BOTTOM_EDGE_SUMMARY rows=" << (rawHeight_ - kBottomH) << ".." << (rawHeight_ - 1u)
                << " sampledColumns=32 sentinelWords=" << bottomSentinels << " coordinateErrors=" << bottomCoordErrors;
    emit(edgeSummary.str());
    emit(
        "PROBE_NOTE dense stage and bottom-edge probes are diagnostic only; no full-frame readback and no production "
        "shader changes");

    done_ = true;
    pendingRawReferenceComparison_ = false;
    pendingLinearCandidateAvailable_ = false;
    pendingSlot_ = -1;
}

void RawIntegrityProbe::reset() {
    if (device_ != VK_NULL_HANDLE) {
        if (resources_.mapped != nullptr && resources_.memory != VK_NULL_HANDLE)
            vkUnmapMemory(device_, resources_.memory);
        if (resources_.buffer != VK_NULL_HANDLE) vkDestroyBuffer(device_, resources_.buffer, nullptr);
        if (resources_.memory != VK_NULL_HANDLE) vkFreeMemory(device_, resources_.memory, nullptr);
        if (resources_.pool != VK_NULL_HANDLE) vkDestroyDescriptorPool(device_, resources_.pool, nullptr);

        for (auto& slot : bufferBenchmarkSlots_) {
            if (slot.view != VK_NULL_HANDLE) vkDestroyImageView(device_, slot.view, nullptr);
            if (slot.image != VK_NULL_HANDLE) vkDestroyImage(device_, slot.image, nullptr);
            if (slot.memory != VK_NULL_HANDLE) vkFreeMemory(device_, slot.memory, nullptr);
        }
        if (resources_.bufferBenchmarkQueryPool != VK_NULL_HANDLE)
            vkDestroyQueryPool(device_, resources_.bufferBenchmarkQueryPool, nullptr);
        if (resources_.bufferPreviewPool != VK_NULL_HANDLE)
            vkDestroyDescriptorPool(device_, resources_.bufferPreviewPool, nullptr);
        if (resources_.bufferPreviewPipeline != VK_NULL_HANDLE)
            vkDestroyPipeline(device_, resources_.bufferPreviewPipeline, nullptr);
        if (resources_.bufferPreviewLayout != VK_NULL_HANDLE)
            vkDestroyPipelineLayout(device_, resources_.bufferPreviewLayout, nullptr);
        if (resources_.bufferPreviewDsl != VK_NULL_HANDLE)
            vkDestroyDescriptorSetLayout(device_, resources_.bufferPreviewDsl, nullptr);

        if (resources_.externalView != VK_NULL_HANDLE) vkDestroyImageView(device_, resources_.externalView, nullptr);
        if (resources_.externalPool != VK_NULL_HANDLE)
            vkDestroyDescriptorPool(device_, resources_.externalPool, nullptr);
        if (resources_.externalPipeline != VK_NULL_HANDLE)
            vkDestroyPipeline(device_, resources_.externalPipeline, nullptr);
        if (resources_.externalLayout != VK_NULL_HANDLE)
            vkDestroyPipelineLayout(device_, resources_.externalLayout, nullptr);
        if (resources_.externalDsl != VK_NULL_HANDLE)
            vkDestroyDescriptorSetLayout(device_, resources_.externalDsl, nullptr);
        if (resources_.externalSampler != VK_NULL_HANDLE)
            vkDestroySampler(device_, resources_.externalSampler, nullptr);
        if (resources_.externalConversion != VK_NULL_HANDLE)
            vkDestroySamplerYcbcrConversion(device_, resources_.externalConversion, nullptr);

        if (resources_.pathPipeline != VK_NULL_HANDLE) vkDestroyPipeline(device_, resources_.pathPipeline, nullptr);
        if (resources_.fullComparePipeline != VK_NULL_HANDLE)
            vkDestroyPipeline(device_, resources_.fullComparePipeline, nullptr);
        if (resources_.rawSampler != VK_NULL_HANDLE) vkDestroySampler(device_, resources_.rawSampler, nullptr);
        if (resources_.pipeline != VK_NULL_HANDLE) vkDestroyPipeline(device_, resources_.pipeline, nullptr);
        if (resources_.layout != VK_NULL_HANDLE) vkDestroyPipelineLayout(device_, resources_.layout, nullptr);
        if (resources_.dsl != VK_NULL_HANDLE) vkDestroyDescriptorSetLayout(device_, resources_.dsl, nullptr);
        if (resources_.bufImportPipeline != VK_NULL_HANDLE)
            vkDestroyPipeline(device_, resources_.bufImportPipeline, nullptr);
        if (resources_.bufImportLayout != VK_NULL_HANDLE)
            vkDestroyPipelineLayout(device_, resources_.bufImportLayout, nullptr);
        if (resources_.bufImportDsl != VK_NULL_HANDLE)
            vkDestroyDescriptorSetLayout(device_, resources_.bufImportDsl, nullptr);
        if (resources_.bufImportPool != VK_NULL_HANDLE)
            vkDestroyDescriptorPool(device_, resources_.bufImportPool, nullptr);
        if (resources_.bufImportMapped != nullptr && resources_.bufImportMemory != VK_NULL_HANDLE)
            vkUnmapMemory(device_, resources_.bufImportMemory);
        if (resources_.bufImportBuffer != VK_NULL_HANDLE) vkDestroyBuffer(device_, resources_.bufImportBuffer, nullptr);
        if (resources_.bufImportMemory != VK_NULL_HANDLE) vkFreeMemory(device_, resources_.bufImportMemory, nullptr);
    }
    resources_ = {};
    bufferBenchmarkSlots_.clear();
    bufferBenchmarkSamples_ = 0;
    productionRawBenchmarkNs_ = 0.0;
    copyToBufferBenchmarkNs_ = 0.0;
    bufferPreviewComputeNs_ = 0.0;
    bufferPathTotalNs_ = 0.0;
    pendingSlot_ = -1;
    pendingTimestamp_ = 0;
    done_ = false;
    pendingRawReferenceComparison_ = false;
    pendingLinearCandidateAvailable_ = false;
    pendingDrmStorageAvailable_ = false;
    pendingDrmSampledAvailable_ = false;
    pendingCopyBufferAvailable_ = false;
    pendingBufferPreviewAvailable_ = false;
    pendingExternalCandidateAvailable_ = false;
    pendingBufImportSlot_ = -1;
    pendingBufImportTimestamp_ = 0;
    bufImportDone_ = false;
    rawWidth_ = rawHeight_ = outputWidth_ = outputHeight_ = 0;
}

}  // namespace rawrcam::diagnostics
