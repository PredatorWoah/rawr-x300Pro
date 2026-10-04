#include "quadfix/Pipeline.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace quadfix {
namespace {

void check(VkResult r, const char* what) {
    if (r != VK_SUCCESS) throw std::runtime_error(what);
}

uint32_t memoryType(VkPhysicalDevice pd, uint32_t bits, VkMemoryPropertyFlags flags) {
    VkPhysicalDeviceMemoryProperties m{};
    vkGetPhysicalDeviceMemoryProperties(pd, &m);
    for (uint32_t i = 0; i < m.memoryTypeCount; ++i)
        if ((bits & (1u << i)) && (m.memoryTypes[i].propertyFlags & flags) == flags) return i;
    throw std::runtime_error("quadfix: no compatible Vulkan memory type");
}

struct OwnedImage {
    VkImage image = VK_NULL_HANDLE;
    VkDeviceMemory memory = VK_NULL_HANDLE;
    VkImageView view = VK_NULL_HANDLE;
    uint64_t allocationBytes = 0;
};

}  // namespace

// Pass indices (guide variant selected at construction).
enum : size_t { kGuide = 0, kEnergy, kDemod, kGaussH, kGaussV, kMaskH, kMaskV, kBlend, kPassCount };

struct QuadfixPipeline::Impl {
    VulkanContext ctx{};
    ShaderProvider shaders;
    PipelineConfig cfg{};

    VkDescriptorSetLayout setLayout = VK_NULL_HANDLE;
    VkPipelineLayout pipelineLayout = VK_NULL_HANDLE;
    VkDescriptorPool descriptorPool = VK_NULL_HANDLE;
    VkDescriptorSet descriptorSet = VK_NULL_HANDLE;
    std::array<VkShaderModule, kPassCount> modules{};
    std::array<VkPipeline, kPassCount> pipelines{};

    OwnedImage guide{};
    OwnedImage energy{};
    OwnedImage maskTmp{};
    OwnedImage coarse{};
    OwnedImage coarseTmp{};
    bool internalLayoutsInitialized = false;

    uint64_t liveBytes = 0;
    uint64_t peakBytes = 0;

    struct Push {
        uint32_t width, height;
    };
    static_assert(sizeof(Push) == 8, "quadfix push constant ABI changed");

    Impl(VulkanContext c, ShaderProvider sp, PipelineConfig pc, PipelineAssets)
        : ctx(c), shaders(std::move(sp)), cfg(pc) {
        const char* why = nullptr;
        if (!QuadfixPipeline::validateConfig(cfg, {}, &why))
            throw std::runtime_error(std::string("quadfix configuration invalid: ") + (why ? why : "unknown"));
        if (ctx.physicalDevice == VK_NULL_HANDLE || ctx.device == VK_NULL_HANDLE)
            throw std::runtime_error("quadfix VulkanContext requires physicalDevice and device");
        if (!shaders) throw std::runtime_error("quadfix ShaderProvider is empty");
        try {
            createResources();
        } catch (...) {
            cleanup();
            throw;
        }
    }

    ~Impl() { cleanup(); }

    void addAllocation(uint64_t n) {
        liveBytes += n;
        peakBytes = std::max(peakBytes, liveBytes);
    }
    void removeAllocation(uint64_t n) { liveBytes -= n; }

    OwnedImage makeImage(uint32_t w, uint32_t h, uint32_t layers, VkFormat format) {
        OwnedImage o{};
        VkImageCreateInfo ci{};
        ci.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
        ci.imageType = VK_IMAGE_TYPE_2D;
        ci.format = format;
        ci.extent = {w, h, 1};
        ci.mipLevels = 1;
        ci.arrayLayers = layers;
        ci.samples = VK_SAMPLE_COUNT_1_BIT;
        ci.tiling = VK_IMAGE_TILING_OPTIMAL;
        ci.usage = VK_IMAGE_USAGE_STORAGE_BIT;
        ci.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        ci.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        check(vkCreateImage(ctx.device, &ci, ctx.allocator, &o.image), "quadfix vkCreateImage failed");
        VkMemoryRequirements mr{};
        vkGetImageMemoryRequirements(ctx.device, o.image, &mr);
        VkMemoryAllocateInfo ai{};
        ai.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        ai.allocationSize = mr.size;
        ai.memoryTypeIndex = memoryType(ctx.physicalDevice, mr.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
        check(vkAllocateMemory(ctx.device, &ai, ctx.allocator, &o.memory), "quadfix vkAllocateMemory failed");
        check(vkBindImageMemory(ctx.device, o.image, o.memory, 0), "quadfix vkBindImageMemory failed");
        VkImageViewCreateInfo vi{};
        vi.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        vi.image = o.image;
        vi.viewType = layers > 1 ? VK_IMAGE_VIEW_TYPE_2D_ARRAY : VK_IMAGE_VIEW_TYPE_2D;
        vi.format = format;
        vi.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, layers};
        check(vkCreateImageView(ctx.device, &vi, ctx.allocator, &o.view), "quadfix vkCreateImageView failed");
        o.allocationBytes = mr.size;
        addAllocation(o.allocationBytes);
        return o;
    }

    void destroy(OwnedImage& o) {
        if (o.view) vkDestroyImageView(ctx.device, o.view, ctx.allocator);
        if (o.image) vkDestroyImage(ctx.device, o.image, ctx.allocator);
        if (o.memory) vkFreeMemory(ctx.device, o.memory, ctx.allocator);
        if (o.allocationBytes) removeAllocation(o.allocationBytes);
        o = {};
    }

    void createResources() {
        const uint32_t cw = cfg.width >> 4;   // W/16 coarse texels
        const uint32_t ch = cfg.height >> 4;  // H/16
        guide = makeImage(cfg.width, cfg.height, 1, VK_FORMAT_R32_SFLOAT);
        energy = makeImage(cfg.width, cfg.height, 1, VK_FORMAT_R32_SFLOAT);
        maskTmp = makeImage(cfg.width, cfg.height, 1, VK_FORMAT_R32_SFLOAT);
        coarse = makeImage(cw, ch, 15, VK_FORMAT_R32G32B32A32_SFLOAT);
        coarseTmp = makeImage(cw, ch, 15, VK_FORMAT_R32G32B32A32_SFLOAT);

        std::array<VkDescriptorSetLayoutBinding, 7> b{};
        b[0] = {0, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr};
        b[1] = {1, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr};
        b[2] = {2, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr};
        b[3] = {3, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr};
        b[4] = {4, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr};
        b[5] = {5, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr};
        b[6] = {6, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr};
        VkDescriptorSetLayoutCreateInfo sl{};
        sl.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
        sl.bindingCount = b.size();
        sl.pBindings = b.data();
        check(vkCreateDescriptorSetLayout(ctx.device, &sl, ctx.allocator, &setLayout),
              "quadfix descriptor-set layout failed");

        VkPushConstantRange pr{VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(Push)};
        VkPipelineLayoutCreateInfo pl{};
        pl.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
        pl.setLayoutCount = 1;
        pl.pSetLayouts = &setLayout;
        pl.pushConstantRangeCount = 1;
        pl.pPushConstantRanges = &pr;
        check(vkCreatePipelineLayout(ctx.device, &pl, ctx.allocator, &pipelineLayout),
              "quadfix pipeline layout failed");

        std::array<VkDescriptorPoolSize, 2> ps{{{VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 2},
                                               {VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 5}}};
        VkDescriptorPoolCreateInfo dpi{};
        dpi.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
        dpi.maxSets = 1;
        dpi.poolSizeCount = ps.size();
        dpi.pPoolSizes = ps.data();
        check(vkCreateDescriptorPool(ctx.device, &dpi, ctx.allocator, &descriptorPool),
              "quadfix descriptor pool failed");
        VkDescriptorSetAllocateInfo dai{};
        dai.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
        dai.descriptorPool = descriptorPool;
        dai.descriptorSetCount = 1;
        dai.pSetLayouts = &setLayout;
        check(vkAllocateDescriptorSets(ctx.device, &dai, &descriptorSet), "quadfix descriptor set alloc failed");

        std::array<const char*, kPassCount> names;
        names[kGuide] = cfg.fastMedian ? "quadfix_guide_fast.comp" : "quadfix_guide.comp";
        names[kEnergy] = "quadfix_energy.comp";
        names[kDemod] = "quadfix_demod.comp";
        names[kGaussH] = "quadfix_gauss_h.comp";
        names[kGaussV] = "quadfix_gauss_v.comp";
        names[kMaskH] = "quadfix_mask_h.comp";
        names[kMaskV] = "quadfix_mask_v.comp";
        names[kBlend] = "quadfix_blend.comp";
        for (size_t i = 0; i < modules.size(); ++i) {
            auto words = shaders(names[i]);
            if (words.empty()) throw std::runtime_error(std::string("quadfix empty SPIR-V: ") + names[i]);
            VkShaderModuleCreateInfo sm{};
            sm.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
            sm.codeSize = words.size() * sizeof(uint32_t);
            sm.pCode = words.data();
            check(vkCreateShaderModule(ctx.device, &sm, ctx.allocator, &modules[i]),
                  "quadfix shader module failed");
            VkPipelineShaderStageCreateInfo st{};
            st.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
            st.stage = VK_SHADER_STAGE_COMPUTE_BIT;
            st.module = modules[i];
            st.pName = "main";
            VkComputePipelineCreateInfo ci{};
            ci.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
            ci.stage = st;
            ci.layout = pipelineLayout;
            check(vkCreateComputePipelines(ctx.device, VK_NULL_HANDLE, 1, &ci, ctx.allocator, &pipelines[i]),
                  "quadfix compute pipeline failed");
        }
    }

    void cleanup() noexcept {
        if (ctx.device == VK_NULL_HANDLE) return;
        for (auto& p : pipelines)
            if (p) {
                vkDestroyPipeline(ctx.device, p, ctx.allocator);
                p = VK_NULL_HANDLE;
            }
        for (auto& m : modules)
            if (m) {
                vkDestroyShaderModule(ctx.device, m, ctx.allocator);
                m = VK_NULL_HANDLE;
            }
        if (descriptorPool)
            vkDestroyDescriptorPool(ctx.device, descriptorPool, ctx.allocator), descriptorPool = VK_NULL_HANDLE;
        if (pipelineLayout)
            vkDestroyPipelineLayout(ctx.device, pipelineLayout, ctx.allocator), pipelineLayout = VK_NULL_HANDLE;
        if (setLayout) vkDestroyDescriptorSetLayout(ctx.device, setLayout, ctx.allocator), setLayout = VK_NULL_HANDLE;
        destroy(coarseTmp);
        destroy(coarse);
        destroy(maskTmp);
        destroy(energy);
        destroy(guide);
    }

    static VkImageMemoryBarrier imageBarrier(VkImage image) {
        VkImageMemoryBarrier b{};
        b.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        b.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
        b.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
        b.oldLayout = VK_IMAGE_LAYOUT_GENERAL;
        b.newLayout = VK_IMAGE_LAYOUT_GENERAL;
        b.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        b.image = image;
        b.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
        return b;
    }

    void initializeInternalLayouts(VkCommandBuffer cmd) {
        if (internalLayoutsInitialized) return;
        std::array<VkImageMemoryBarrier, 5> bs{};
        const VkImage imgs[5] = {guide.image, energy.image, maskTmp.image, coarse.image, coarseTmp.image};
        for (size_t i = 0; i < 5; ++i) {
            bs[i] = imageBarrier(imgs[i]);
            bs[i].srcAccessMask = 0;
            bs[i].oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
            bs[i].subresourceRange.layerCount = (i >= 3) ? 15 : 1;
        }
        vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 0,
                             nullptr, 0, nullptr, bs.size(), bs.data());
        internalLayoutsInitialized = true;
    }

    void imageStepBarrier(VkCommandBuffer cmd, VkImage image, uint32_t layers = 1) {
        auto b = imageBarrier(image);
        b.subresourceRange.layerCount = layers;
        vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 0,
                             nullptr, 0, nullptr, 1, &b);
    }

    void updateDescriptors(VkBuffer in, VkDeviceSize inRange, VkBuffer out, VkDeviceSize outRange) {
        VkDescriptorBufferInfo bi0{in, 0, inRange};
        VkDescriptorImageInfo i1{VK_NULL_HANDLE, guide.view, VK_IMAGE_LAYOUT_GENERAL};
        VkDescriptorImageInfo i2{VK_NULL_HANDLE, energy.view, VK_IMAGE_LAYOUT_GENERAL};
        VkDescriptorImageInfo i3{VK_NULL_HANDLE, coarse.view, VK_IMAGE_LAYOUT_GENERAL};
        VkDescriptorImageInfo i4{VK_NULL_HANDLE, coarseTmp.view, VK_IMAGE_LAYOUT_GENERAL};
        VkDescriptorImageInfo i5{VK_NULL_HANDLE, maskTmp.view, VK_IMAGE_LAYOUT_GENERAL};
        VkDescriptorBufferInfo bi6{out, 0, outRange};
        std::array<VkWriteDescriptorSet, 7> w{};
        for (uint32_t i = 0; i < w.size(); ++i) {
            w[i].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            w[i].dstSet = descriptorSet;
            w[i].dstBinding = i;
            w[i].descriptorCount = 1;
        }
        w[0].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        w[0].pBufferInfo = &bi0;
        w[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
        w[1].pImageInfo = &i1;
        w[2].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
        w[2].pImageInfo = &i2;
        w[3].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
        w[3].pImageInfo = &i3;
        w[4].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
        w[4].pImageInfo = &i4;
        w[5].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
        w[5].pImageInfo = &i5;
        w[6].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        w[6].pBufferInfo = &bi6;
        vkUpdateDescriptorSets(ctx.device, w.size(), w.data(), 0, nullptr);
    }

    void dispatchAll(VkCommandBuffer cmd, VkBuffer outBuffer, VkDeviceSize outBytes) {
        initializeInternalLayouts(cmd);
        const uint32_t gx = (cfg.width + cfg.workgroupX - 1u) / cfg.workgroupX;
        const uint32_t gy = (cfg.height + cfg.workgroupY - 1u) / cfg.workgroupY;
        const uint32_t cgx = (cfg.width / 16u + cfg.workgroupX - 1u) / cfg.workgroupX;
        const uint32_t cgy = (cfg.height / 16u + cfg.workgroupY - 1u) / cfg.workgroupY;
        Push pc{cfg.width, cfg.height};
        auto dispatch = [&](size_t i, uint32_t x, uint32_t y) {
            vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipelines[i]);
            vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipelineLayout, 0, 1, &descriptorSet, 0,
                                    nullptr);
            vkCmdPushConstants(cmd, pipelineLayout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(Push), &pc);
            vkCmdDispatch(cmd, x, y, 1);
        };
        // guide -> energy -> demod -> gaussH/V -> maskH/V -> blend
        dispatch(kGuide, gx, gy);
        imageStepBarrier(cmd, guide.image);
        dispatch(kEnergy, gx, gy);
        imageStepBarrier(cmd, energy.image);
        dispatch(kDemod, cgx, cgy);
        imageStepBarrier(cmd, coarse.image, 15);
        dispatch(kGaussH, cgx, cgy);
        imageStepBarrier(cmd, coarseTmp.image, 15);
        dispatch(kGaussV, cgx, cgy);
        imageStepBarrier(cmd, coarse.image, 15);
        dispatch(kMaskH, gx, gy);
        imageStepBarrier(cmd, maskTmp.image);
        dispatch(kMaskV, gx, gy);
        imageStepBarrier(cmd, energy.image);
        dispatch(kBlend, gx, gy);
        // Trailing buffer barrier: blend's output must be visible to whatever
        // consumes it next in the same command buffer (RCD storage reads or
        // the prepare() core-compaction transfer copy).
        VkBufferMemoryBarrier bb{};
        bb.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
        bb.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
        bb.dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_TRANSFER_READ_BIT;
        bb.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        bb.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        bb.buffer = outBuffer;
        bb.offset = 0;
        bb.size = outBytes;
        vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 0,
                             nullptr, 1, &bb, 0, nullptr);
    }

    void record(const BayerBufferView& in, const BayerBufferView& out, VkCommandBuffer cmd) {
        if (cmd == VK_NULL_HANDLE) throw std::runtime_error("quadfix command buffer is null");
        for (const auto* v : {&in, &out}) {
            if (v->width != cfg.width || v->height != cfg.height)
                throw std::runtime_error("quadfix buffer dimensions do not match pipeline");
            if (v->buffer.buffer == VK_NULL_HANDLE || v->buffer.offset != 0)
                throw std::runtime_error("quadfix requires non-null buffer and offset=0");
        }
        const VkDeviceSize needed = VkDeviceSize(size_t(cfg.width) * cfg.height * sizeof(float));
        const VkDeviceSize inRange = in.buffer.range == VK_WHOLE_SIZE ? needed : in.buffer.range;
        const VkDeviceSize outRange = out.buffer.range == VK_WHOLE_SIZE ? needed : out.buffer.range;
        if (inRange < needed || outRange < needed)
            throw std::runtime_error("quadfix input/output buffer too small");
        if (in.buffer.buffer == out.buffer.buffer)
            throw std::runtime_error("quadfix does not support in-place filtering");
        updateDescriptors(in.buffer.buffer, inRange, out.buffer.buffer, outRange);
        dispatchAll(cmd, out.buffer.buffer, outRange);
    }
};


bool QuadfixPipeline::validateConfig(const PipelineConfig& c, const PipelineAssets&, const char** reason) noexcept {
    const char* why = nullptr;
    if (c.width == 0 || c.height == 0)
        why = "quadfix requires non-zero dimensions";
    else if ((c.width & 15u) || (c.height & 15u))
        why = "quadfix requires width and height multiples of 16 (plane 8x8 demod blocks)";
    else if (c.width < 64u || c.height < 64u)
        why = "quadfix requires width and height >= 64";
    else if (c.workgroupX == 0u || c.workgroupY == 0u)
        why = "quadfix workgroup dimensions must be non-zero";
    if (reason) *reason = why;
    return why == nullptr;
}

QuadfixPipeline::QuadfixPipeline(VulkanContext c, ShaderProvider s, PipelineConfig p, PipelineAssets a)
    : impl_(std::make_unique<Impl>(c, std::move(s), p, a)) {}
QuadfixPipeline::~QuadfixPipeline() = default;
void QuadfixPipeline::record(VkCommandBuffer c, const BayerBufferView& i, const BayerBufferView& o) {
    impl_->record(i, o, c);
}
const PipelineConfig& QuadfixPipeline::config() const { return impl_->cfg; }
uint64_t QuadfixPipeline::currentAllocatedBytes() const { return impl_->liveBytes; }
uint64_t QuadfixPipeline::peakAllocatedBytes() const { return impl_->peakBytes; }

}  // namespace quadfix
