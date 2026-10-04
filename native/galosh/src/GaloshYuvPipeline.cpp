// GaloshYuvPipeline: app-native Vulkan port of GALOSH-YUV (O-variant).
//
// Faithful to tmp/GALOSH/standalone/vk/galosh_yuv_vk.c run_core() (sRGB
// path, banded/CLI structure) section by section. Adaptations ([PORT]):
//  - Linear entry: sRGB gamma wrap replaced by app-authored BT.709-only
//    bridges (no gamma, no clip — pre-tonemap HDR preserved).
//  - No instance/device/files/env/video-modes: fit-mode only, stills.
//  - Synchronous stage (own submits + fence waits, like GaloshRawPipeline).
//  - O-variant only (upstream has no GPU Q pyramid); multi-scale Q is P2.
//  - Wiener-NaN guard: ChromaOnly (or strengthY<=0) copies y_stab->y_den.
//  - Subgroup pass12 omitted (needs the device ext); classic serves.
//  - Timestamps 14->15 on the caller pool (nullable).

#include "galosh/GaloshYuvPipeline.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <mutex>
#include <stdexcept>
#include <string>
#include <vector>

#include "galosh/GaloshVkUtils.hpp"

namespace galosh {
namespace {

// Upstream constants (galosh_yuv_vk.c).
constexpr size_t kParamsSize = 32;
constexpr size_t kGatLutSize = 4096;
constexpr int kO32Tile = 28;
constexpr int kPAlpha = 13;
constexpr int kPSigmaSq = 14;
constexpr int kPSigmaY = 20;
constexpr int kPSigmaGat = 21;
constexpr int kPEnvDarkThresh = 24;
constexpr int kPEnvDegen = 25;
constexpr int kPAlphaMad = 26;
constexpr int kPSigmaSqMad = 27;

constexpr uint32_t kQueryBegin = 14;
constexpr uint32_t kQueryEnd = 15;

constexpr uint32_t aup(uint32_t n, uint32_t a) { return (n + a - 1) / a; }

enum Kernel {
    K_LAP_MAD, K_LAP_MAD_H16, K_SYNTH, K_GAT_FWD, K_NORM, K_DENORM,
    K_LUT_BUILD, K_LUT_FIN, K_PASS12, K_MAKITALO, K_LOESS,
    K_ENV_BLK, K_ENV_FIN, K_ENV_SEL, K_ENV_DTH, K_ENV_DTF, K_ENV_DLH, K_ENV_DFN,
    K_BR_IN, K_BR_OUT,
    K_COUNT
};

struct KernelDesc {
    const char* name;
    int nbind;
    int pushBytes;
};

const KernelDesc kKernels[K_COUNT] = {
    {"yuv_lap_mad", 3, 16},
    {"yuv_lap_mad_h16", 3, 16},
    {"yuv_synth_alpha", 1, 12},
    {"yuv_gat_fwd", 3, 4},
    {"yuv_sigma_norm", 2, 8},
    {"yuv_sigma_denorm", 2, 8},
    {"o32_build_inv_lut", 4, 0},
    {"o32_lut_finalize", 2, 0},
    {"o32_pass12", 2, 16},
    {"yuv_makitalo", 5, 4},
    {"yuv_loess", 6, 24},
    {"yuv_env_block_stats", 3, 16},
    {"o32_ne_finalize", 4, 12},
    {"yuv_env_select", 1, 4},
    {"yuv_env_dark_thresh_hist", 2, 8},
    {"o32_ne_dark_thresh_finalize", 2, 4},
    {"yuv_env_dark_lap_hist", 3, 12},
    {"o32_ne_dark_finalize", 2, 4},
    {"galosh_yuv_bridge_in", 4, 8},
    {"galosh_yuv_bridge_out", 7, 16},
};

struct BuiltKernel {
    VkDescriptorSetLayout dsl = VK_NULL_HANDLE;
    VkPipelineLayout pl = VK_NULL_HANDLE;
    VkPipeline pipe = VK_NULL_HANDLE;
    int nbind = 0;
    int pushBytes = 0;
    bool present = false;
};

}  // namespace


struct YuvContext {
    VkPhysicalDevice physical = VK_NULL_HANDLE;
    VkDevice device = VK_NULL_HANDLE;
    uint32_t queueFamily = 0;

    VkCommandPool pool = VK_NULL_HANDLE;
    VkCommandBuffer cmd = VK_NULL_HANDLE;
    VkFence fence = VK_NULL_HANDLE;
    VkDescriptorPool dpool = VK_NULL_HANDLE;

    std::array<BuiltKernel, K_COUNT> kernels{};

    uint32_t epochW = 0;
    uint32_t epochH = 0;

    vk::Buffer yLin, yStab, yDen, ySnap, yNoisySnap;
    vk::Buffer cbB, crB, cbDen, crDen;
    vk::Buffer neScr, params, lutD, lutX, lutP;
    vk::Buffer envBm, envBv, envH1, envH2;
    vk::Buffer staging;
    void* stagingMap = nullptr;
    float lastAlpha = 0.0f, lastSigmaSq = 0.0f, lastSigmaGat = 0.0f;
    float lastDegen = 0.0f;

    VkDescriptorSet sBrIn = VK_NULL_HANDLE, sBrOut = VK_NULL_HANDLE;
    VkDescriptorSet sMad = VK_NULL_HANDLE;
    VkDescriptorSet sEnvBlk = VK_NULL_HANDLE, sEnvFin = VK_NULL_HANDLE, sEnvSel = VK_NULL_HANDLE;
    VkDescriptorSet sEnvDth = VK_NULL_HANDLE, sEnvDtf = VK_NULL_HANDLE;
    VkDescriptorSet sEnvDlh = VK_NULL_HANDLE, sEnvDfn = VK_NULL_HANDLE;
    VkDescriptorSet sGat = VK_NULL_HANDLE, sMad16 = VK_NULL_HANDLE, sNorm = VK_NULL_HANDLE;
    VkDescriptorSet sLut = VK_NULL_HANDLE, sLutFin = VK_NULL_HANDLE;
    VkDescriptorSet sP12 = VK_NULL_HANDLE;
    VkDescriptorSet sDen = VK_NULL_HANDLE, sMak = VK_NULL_HANDLE, sLo = VK_NULL_HANDLE;
    VkDescriptorSet sSyn = VK_NULL_HANDLE;
};

namespace {

union PcW {
    int32_t i;
    float f;
};

void checkImpl(bool ok, const std::string& what) {
    if (!ok) throw std::runtime_error("galosh: " + what);
}

// Per-process() recording state.
struct Call {
    YuvContext* im = nullptr;
    VkQueue queue = VK_NULL_HANDLE;
    VkQueryPool timing = VK_NULL_HANDLE;
    bool recording = false;
    uint32_t W = 0, H = 0;
    uint64_t npix = 0;
    float strengthY = 1.0f, strengthC = 1.0f;
    bool chromaOnly = false;
    float kneeLo = 1.0f, kneeHi = 1.5f;
};

void beginCb(Call& c) {
    VkCommandBufferBeginInfo bi{};
    bi.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vk::check(vkResetCommandBuffer(c.im->cmd, 0), "yuv reset cmd");
    vk::check(vkBeginCommandBuffer(c.im->cmd, &bi), "yuv begin cmd");
    c.recording = true;
}

void submitWait(Call& c) {
    vk::check(vkEndCommandBuffer(c.im->cmd), "yuv end cmd");
    VkSubmitInfo si{};
    si.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    si.commandBufferCount = 1;
    si.pCommandBuffers = &c.im->cmd;
    vk::check(vkResetFences(c.im->device, 1, &c.im->fence), "yuv reset fence");
    vk::check(vkQueueSubmit(c.queue, 1, &si, c.im->fence), "yuv submit");
    vk::check(vkWaitForFences(c.im->device, 1, &c.im->fence, VK_TRUE, UINT64_MAX), "yuv fence");
    c.recording = false;
}

void dispatchKid(Call& c, Kernel kid, VkDescriptorSet set, PcW* pc, int npc, uint32_t gx,
                 uint32_t gy, uint32_t gz = 1) {
    const BuiltKernel& k = c.im->kernels[static_cast<size_t>(kid)];
    checkImpl(k.present, "yuv kernel not built");
    vkCmdBindPipeline(c.im->cmd, VK_PIPELINE_BIND_POINT_COMPUTE, k.pipe);
    vkCmdBindDescriptorSets(c.im->cmd, VK_PIPELINE_BIND_POINT_COMPUTE, k.pl, 0, 1, &set, 0, nullptr);
    if (npc > 0) {
        vkCmdPushConstants(c.im->cmd, k.pl, VK_SHADER_STAGE_COMPUTE_BIT, 0,
                           static_cast<uint32_t>(npc * 4), pc);
    }
    vkCmdDispatch(c.im->cmd, gx, gy, gz);
    vk::computeBarrier(c.im->cmd);
}

void fillBuf(Call& c, vk::Buffer& b, VkDeviceSize size) {
    vkCmdFillBuffer(c.im->cmd, b.buf, 0, size, 0);
    vk::computeBarrier(c.im->cmd);
}

// Probe + single remainder (mirrors the RAW port; no rate learning in P1b).
// Micro-banded 2 rows x 48 groups per submit (same Adreno watchdog bound
// as GaloshRawPipeline::pass12Banded): the remainder for K_LOESS at 12 MP
// is ~250x186 workgroups in one submit, ~4x that at merged 2x output.
// Bands are independent (halo from input, accumulators in shared memory),
// so finer banding is bit-identical math with bounded per-submit work.
void banded(Call& c, Kernel kid, VkDescriptorSet set, PcW* pc, int npc, uint32_t gx, uint32_t gy) {
    constexpr uint32_t kBandRows = 2;
    constexpr uint32_t kBandGroupsX = 48;
    auto piece = [&](uint32_t x0, uint32_t y0, uint32_t nx, uint32_t rows) {
        beginCb(c);
        const BuiltKernel& k = c.im->kernels[static_cast<size_t>(kid)];
        vkCmdBindPipeline(c.im->cmd, VK_PIPELINE_BIND_POINT_COMPUTE, k.pipe);
        vkCmdBindDescriptorSets(c.im->cmd, VK_PIPELINE_BIND_POINT_COMPUTE, k.pl, 0, 1, &set, 0,
                                nullptr);
        vkCmdPushConstants(c.im->cmd, k.pl, VK_SHADER_STAGE_COMPUTE_BIT, 0,
                           static_cast<uint32_t>(npc * 4), pc);
        vkCmdDispatchBase(c.im->cmd, x0, y0, 0, nx, rows, 1);
        vk::computeBarrier(c.im->cmd);
        submitWait(c);
    };
    for (uint32_t y = 0; y < gy; y += kBandRows) {
        const uint32_t rows = std::min(kBandRows, gy - y);
        for (uint32_t x = 0; x < gx; x += kBandGroupsX) {
            piece(x, y, std::min(kBandGroupsX, gx - x), rows);
        }
    }
}

void downloadParams(Call& c, float* out128) {
    VkBufferCopy cp{};
    cp.srcOffset = 0;
    cp.dstOffset = 0;
    cp.size = kParamsSize * 4;
    beginCb(c);
    vkCmdCopyBuffer(c.im->cmd, c.im->params.buf, c.im->staging.buf, 1, &cp);
    submitWait(c);
    std::memcpy(out128, c.im->stagingMap, kParamsSize * 4);
}

void uploadParams(Call& c, const float* in128) {
    std::memcpy(c.im->stagingMap, in128, kParamsSize * 4);
    VkBufferCopy cp{};
    cp.srcOffset = 0;
    cp.dstOffset = 0;
    cp.size = kParamsSize * 4;
    vkCmdCopyBuffer(c.im->cmd, c.im->staging.buf, c.im->params.buf, 1, &cp);
    vk::computeBarrier(c.im->cmd);
}

VkDescriptorSet allocSet(Call& c, Kernel kid, std::initializer_list<vk::Buffer*> bufs) {
    YuvContext* im = c.im;
    const BuiltKernel& k = im->kernels[static_cast<size_t>(kid)];
    checkImpl(bufs.size() == static_cast<size_t>(k.nbind), "yuv set arity");
    VkDescriptorSetAllocateInfo ai{};
    ai.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    ai.descriptorPool = im->dpool;
    ai.descriptorSetCount = 1;
    ai.pSetLayouts = &k.dsl;
    VkDescriptorSet ds = VK_NULL_HANDLE;
    vk::check(vkAllocateDescriptorSets(im->device, &ai, &ds), "yuv alloc set");
    std::vector<VkDescriptorBufferInfo> bi;
    std::vector<VkWriteDescriptorSet> wr;
    bi.reserve(bufs.size());
    wr.reserve(bufs.size());
    uint32_t b = 0;
    for (vk::Buffer* buf : bufs) {
        bi.push_back({buf->buf, 0, VK_WHOLE_SIZE});
        VkWriteDescriptorSet w{};
        w.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        w.dstSet = ds;
        w.dstBinding = b++;
        w.descriptorCount = 1;
        w.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        w.pBufferInfo = &bi.back();
        wr.push_back(w);
    }
    vkUpdateDescriptorSets(im->device, static_cast<uint32_t>(wr.size()), wr.data(), 0, nullptr);
    return ds;
}

void writeTimestamp(Call& c, uint32_t slot, VkPipelineStageFlagBits stage) {
    if (c.timing == VK_NULL_HANDLE) return;
    vkCmdWriteTimestamp(c.im->cmd, stage, c.timing, slot);
}

// (Re)allocates graph + pool/sets per geometry epoch. Caller-idle contract.
void ensureResources(YuvContext& im, Call& c) {
    if (im.epochW == c.W && im.epochH == c.H && im.dpool != VK_NULL_HANDLE) return;
    if (im.dpool != VK_NULL_HANDLE) {
        vkDestroyDescriptorPool(im.device, im.dpool, nullptr);
        im.dpool = VK_NULL_HANDLE;
        // Pool destruction invalidates all sets. Epoch-bound sets below are
        // overwritten; per-call bridge sets must be nulled so bindBridges
        // doesn't vkFree stale handles on the new pool.
        im.sBrIn = VK_NULL_HANDLE;
        im.sBrOut = VK_NULL_HANDLE;
    }
    auto mk = [&](vk::Buffer& b, VkDeviceSize size) {
        vk::freeBuffer(im.device, b);
        b = vk::makeBuffer(im.physical, im.device, size,
                           VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT |
                               VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                           VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, "yuv graph");
    };
    const size_t npix = static_cast<size_t>(c.W) * c.H;
    const size_t fb = npix * 4, hb = npix * 2;
    const int nBlk = (c.W / 16) * (c.H / 16);
    mk(im.yLin, fb);
    mk(im.yNoisySnap, fb);
    mk(im.yStab, hb);
    mk(im.yDen, hb);
    mk(im.ySnap, hb);
    mk(im.cbB, hb);
    mk(im.crB, hb);
    mk(im.cbDen, hb);
    mk(im.crDen, hb);
    mk(im.neScr, (200000 + 64) * 4);
    mk(im.params, kParamsSize * 4);
    mk(im.lutD, kGatLutSize * 4);
    mk(im.lutX, kGatLutSize * 4);
    mk(im.lutP, 32);
    mk(im.envBm, static_cast<size_t>(nBlk) * 4);
    mk(im.envBv, static_cast<size_t>(nBlk) * 4);
    mk(im.envH1, 4096 * 4);
    mk(im.envH2, 4096 * 4);

    VkDescriptorPoolSize dps[2]{};
    dps[0].type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    dps[0].descriptorCount = 256;
    dps[1].type = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    dps[1].descriptorCount = 8;
    VkDescriptorPoolCreateInfo dpi{};
    dpi.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    dpi.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
    dpi.maxSets = 64;
    dpi.poolSizeCount = 2;
    dpi.pPoolSizes = dps;
    vk::check(vkCreateDescriptorPool(im.device, &dpi, nullptr, &im.dpool), "yuv dpool");

    Call tmp{};
    tmp.im = &im;
    im.sMad = allocSet(tmp, K_LAP_MAD, {&im.yLin, &im.params, &im.neScr});
    im.sEnvBlk = allocSet(tmp, K_ENV_BLK, {&im.yLin, &im.envBm, &im.envBv});
    im.sEnvFin = allocSet(tmp, K_ENV_FIN, {&im.envBm, &im.envBv, &im.yLin, &im.params});
    im.sEnvSel = allocSet(tmp, K_ENV_SEL, {&im.params});
    im.sEnvDth = allocSet(tmp, K_ENV_DTH, {&im.yLin, &im.envH1});
    im.sEnvDtf = allocSet(tmp, K_ENV_DTF, {&im.envH1, &im.params});
    im.sEnvDlh = allocSet(tmp, K_ENV_DLH, {&im.yLin, &im.params, &im.envH2});
    im.sEnvDfn = allocSet(tmp, K_ENV_DFN, {&im.envH2, &im.params});
    im.sGat = allocSet(tmp, K_GAT_FWD, {&im.yLin, &im.yStab, &im.params});
    im.sMad16 = allocSet(tmp, K_LAP_MAD_H16, {&im.yStab, &im.params, &im.neScr});
    im.sNorm = allocSet(tmp, K_NORM, {&im.yStab, &im.params});
    im.sLut = allocSet(tmp, K_LUT_BUILD, {&im.params, &im.lutD, &im.lutX, &im.lutP});
    im.sLutFin = allocSet(tmp, K_LUT_FIN, {&im.lutD, &im.lutP});
    im.sP12 = allocSet(tmp, K_PASS12, {&im.yStab, &im.yDen});
    im.sDen = allocSet(tmp, K_DENORM, {&im.yDen, &im.params});
    im.sMak = allocSet(tmp, K_MAKITALO, {&im.yDen, &im.yLin, &im.lutD, &im.lutX, &im.lutP});
    im.sSyn = allocSet(tmp, K_SYNTH, {&im.params});
    im.sLo = allocSet(tmp, K_LOESS, {&im.ySnap, &im.cbB, &im.crB, &im.cbDen, &im.crDen,
                                     &im.params});
    im.epochW = c.W;
    im.epochH = c.H;
}

// Binds bridge image views per call (views vary); frees prior bridge sets.
void bindBridges(Call& c, VkImageView srcView, VkImageView dstView) {
    YuvContext* im = c.im;
    if (im->sBrIn != VK_NULL_HANDLE) vkFreeDescriptorSets(im->device, im->dpool, 1, &im->sBrIn);
    if (im->sBrOut != VK_NULL_HANDLE) vkFreeDescriptorSets(im->device, im->dpool, 1, &im->sBrOut);
    im->sBrIn = VK_NULL_HANDLE;
    im->sBrOut = VK_NULL_HANDLE;
    auto allocOne = [&](Kernel kid) {
        const BuiltKernel& k = im->kernels[static_cast<size_t>(kid)];
        checkImpl(k.present, "yuv bridge kernel not built");
        VkDescriptorSetAllocateInfo ai{};
        ai.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
        ai.descriptorPool = im->dpool;
        ai.descriptorSetCount = 1;
        ai.pSetLayouts = &k.dsl;
        VkDescriptorSet ds = VK_NULL_HANDLE;
        vk::check(vkAllocateDescriptorSets(im->device, &ai, &ds), "yuv alloc bridge set");
        return ds;
    };
    // Bridge-in layout: (image=0, y=1, cb=2, cr=3).
    {
        VkDescriptorSet ds = allocOne(K_BR_IN);
        VkDescriptorImageInfo ii{};
        ii.imageView = srcView;
        ii.imageLayout = VK_IMAGE_LAYOUT_GENERAL;
        VkDescriptorBufferInfo by{im->yLin.buf, 0, VK_WHOLE_SIZE};
        VkDescriptorBufferInfo bb{im->cbB.buf, 0, VK_WHOLE_SIZE};
        VkDescriptorBufferInfo br{im->crB.buf, 0, VK_WHOLE_SIZE};
        VkWriteDescriptorSet w[4]{};
        w[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        w[0].dstSet = ds;
        w[0].dstBinding = 0;
        w[0].descriptorCount = 1;
        w[0].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
        w[0].pImageInfo = &ii;
        const VkDescriptorBufferInfo* bis[3] = {&by, &bb, &br};
        for (int i = 0; i < 3; ++i) {
            w[i + 1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            w[i + 1].dstSet = ds;
            w[i + 1].dstBinding = static_cast<uint32_t>(i + 1);
            w[i + 1].descriptorCount = 1;
            w[i + 1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
            w[i + 1].pBufferInfo = bis[i];
        }
        vkUpdateDescriptorSets(im->device, 4, w, 0, nullptr);
        im->sBrIn = ds;
    }
    // Bridge-out layout: (y=0, cb=1, cr=2, dstImage=3, yNoisy=4,
    // cbNoisy=5, crNoisy=6). Buffer-only guides: in-place src==dst safe.
    {
        VkDescriptorSet ds = allocOne(K_BR_OUT);
        VkDescriptorBufferInfo by{im->yLin.buf, 0, VK_WHOLE_SIZE};
        VkDescriptorBufferInfo bb{im->cbDen.buf, 0, VK_WHOLE_SIZE};
        VkDescriptorBufferInfo br{im->crDen.buf, 0, VK_WHOLE_SIZE};
        VkDescriptorImageInfo io{};
        io.imageView = dstView;
        io.imageLayout = VK_IMAGE_LAYOUT_GENERAL;
        VkDescriptorBufferInfo bn{im->yNoisySnap.buf, 0, VK_WHOLE_SIZE};
        VkDescriptorBufferInfo bcn{im->cbB.buf, 0, VK_WHOLE_SIZE};
        VkDescriptorBufferInfo brn{im->crB.buf, 0, VK_WHOLE_SIZE};
        const VkDescriptorBufferInfo* bis[6] = {&by, &bb, &br, &bn, &bcn, &brn};
        // Explicit binding map (bis order -> dstBinding): (0,1,2,4,5,6).
        VkWriteDescriptorSet v[7]{};
        const uint32_t dstB[6] = {0, 1, 2, 4, 5, 6};
        for (int i = 0; i < 6; ++i) {
            v[i].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            v[i].dstSet = ds;
            v[i].dstBinding = dstB[i];
            v[i].descriptorCount = 1;
            v[i].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
            v[i].pBufferInfo = bis[i];
        }
        v[6].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        v[6].dstSet = ds;
        v[6].dstBinding = 3;
        v[6].descriptorCount = 1;
        v[6].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
        v[6].pImageInfo = &io;
        vkUpdateDescriptorSets(im->device, 7, v, 0, nullptr);
        im->sBrOut = ds;
    }
}

}  // namespace

// TU-local execution state (NOT nested: the free helpers above need access;
// the header keeps Impl opaque and Impl is a one-field wrapper over this).

struct GaloshYuvPipeline::Impl {
    YuvContext ctx;
};

GaloshYuvPipeline::GaloshYuvPipeline(VkPhysicalDevice physicalDevice, VkDevice device,
                                     uint32_t queueFamily, GaloshShaderMap shaders)
    : impl_(new Impl()) {
    YuvContext& im = impl_->ctx;
    im.physical = physicalDevice;
    im.device = device;
    im.queueFamily = queueFamily;

    VkCommandPoolCreateInfo cpci{};
    cpci.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    cpci.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    cpci.queueFamilyIndex = queueFamily;
    vk::check(vkCreateCommandPool(device, &cpci, nullptr, &im.pool), "yuv pool");

    VkCommandBufferAllocateInfo cai{};
    cai.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    cai.commandPool = im.pool;
    cai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    cai.commandBufferCount = 1;
    vk::check(vkAllocateCommandBuffers(device, &cai, &im.cmd), "yuv cmd");

    VkFenceCreateInfo fci{};
    fci.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    vk::check(vkCreateFence(device, &fci, nullptr, &im.fence), "yuv fence");

    im.staging = vk::makeBuffer(physicalDevice, device, 4096,
                                VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                                VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                                "yuv staging");
    vk::check(vkMapMemory(device, im.staging.mem, 0, VK_WHOLE_SIZE, 0, &im.stagingMap),
              "yuv map staging");

    for (int k = 0; k < K_COUNT; ++k) {
        const KernelDesc& desc = kKernels[k];
        auto it = shaders.find(desc.name);
        if (it == shaders.end() || it->second.words == nullptr || it->second.wordCount == 0) {
            checkImpl(false, std::string("yuv missing shader: ") + desc.name);
            continue;
        }
        BuiltKernel& bk = im.kernels[static_cast<size_t>(k)];
        std::vector<VkDescriptorSetLayoutBinding> binds(static_cast<size_t>(desc.nbind));
        for (int i = 0; i < desc.nbind; ++i) {
            binds[static_cast<size_t>(i)].binding = static_cast<uint32_t>(i);
            const bool isImage = (static_cast<Kernel>(k) == K_BR_IN && i == 0) ||
                                 (static_cast<Kernel>(k) == K_BR_OUT && i == 3);
            binds[static_cast<size_t>(i)].descriptorType =
                isImage ? VK_DESCRIPTOR_TYPE_STORAGE_IMAGE : VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
            binds[static_cast<size_t>(i)].descriptorCount = 1;
            binds[static_cast<size_t>(i)].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
        }
        VkDescriptorSetLayoutCreateInfo dsli{};
        dsli.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
        dsli.bindingCount = static_cast<uint32_t>(desc.nbind);
        dsli.pBindings = binds.data();
        vk::check(vkCreateDescriptorSetLayout(device, &dsli, nullptr, &bk.dsl), "yuv dsl");

        VkPushConstantRange pcr{};
        pcr.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
        pcr.offset = 0;
        pcr.size = static_cast<uint32_t>(desc.pushBytes ? desc.pushBytes : 4);
        VkPipelineLayoutCreateInfo pli{};
        pli.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
        pli.setLayoutCount = 1;
        pli.pSetLayouts = &bk.dsl;
        pli.pushConstantRangeCount = desc.pushBytes ? 1u : 0u;
        pli.pPushConstantRanges = &pcr;
        vk::check(vkCreatePipelineLayout(device, &pli, nullptr, &bk.pl), "yuv layout");

        VkShaderModuleCreateInfo sm{};
        sm.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
        sm.codeSize = it->second.wordCount * 4;
        sm.pCode = it->second.words;
        VkShaderModule mod = VK_NULL_HANDLE;
        vk::check(vkCreateShaderModule(device, &sm, nullptr, &mod), "yuv module");
        VkComputePipelineCreateInfo cpi{};
        cpi.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
        cpi.flags = VK_PIPELINE_CREATE_DISPATCH_BASE_BIT;
        cpi.stage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        cpi.stage.stage = VK_SHADER_STAGE_COMPUTE_BIT;
        cpi.stage.module = mod;
        cpi.stage.pName = "main";
        cpi.layout = bk.pl;
        vk::check(vkCreateComputePipelines(device, VK_NULL_HANDLE, 1, &cpi, nullptr, &bk.pipe),
                  "yuv pipe");
        vkDestroyShaderModule(device, mod, nullptr);
        bk.nbind = desc.nbind;
        bk.pushBytes = desc.pushBytes;
        bk.present = true;
    }
}

GaloshYuvPipeline::~GaloshYuvPipeline() {
    if (impl_ == nullptr) return;
    YuvContext& im = impl_->ctx;
    for (auto& bk : im.kernels) {
        if (bk.pipe != VK_NULL_HANDLE) vkDestroyPipeline(im.device, bk.pipe, nullptr);
        if (bk.pl != VK_NULL_HANDLE) vkDestroyPipelineLayout(im.device, bk.pl, nullptr);
        if (bk.dsl != VK_NULL_HANDLE) vkDestroyDescriptorSetLayout(im.device, bk.dsl, nullptr);
    }
    vk::freeBuffer(im.device, im.yLin);
    vk::freeBuffer(im.device, im.yNoisySnap);
    vk::freeBuffer(im.device, im.yStab);
    vk::freeBuffer(im.device, im.yDen);
    vk::freeBuffer(im.device, im.ySnap);
    vk::freeBuffer(im.device, im.cbB);
    vk::freeBuffer(im.device, im.crB);
    vk::freeBuffer(im.device, im.cbDen);
    vk::freeBuffer(im.device, im.crDen);
    vk::freeBuffer(im.device, im.neScr);
    vk::freeBuffer(im.device, im.params);
    vk::freeBuffer(im.device, im.lutD);
    vk::freeBuffer(im.device, im.lutX);
    vk::freeBuffer(im.device, im.lutP);
    vk::freeBuffer(im.device, im.envBm);
    vk::freeBuffer(im.device, im.envBv);
    vk::freeBuffer(im.device, im.envH1);
    vk::freeBuffer(im.device, im.envH2);
    if (im.dpool != VK_NULL_HANDLE) vkDestroyDescriptorPool(im.device, im.dpool, nullptr);
    if (im.stagingMap != nullptr) vkUnmapMemory(im.device, im.staging.mem);
    vk::freeBuffer(im.device, im.staging);
    if (im.fence != VK_NULL_HANDLE) vkDestroyFence(im.device, im.fence, nullptr);
    if (im.pool != VK_NULL_HANDLE) vkDestroyCommandPool(im.device, im.pool, nullptr);
    delete impl_;
    impl_ = nullptr;
}

uint64_t GaloshYuvPipeline::scratchBytes(uint32_t width, uint32_t height) const noexcept {
    const uint64_t npix = static_cast<uint64_t>(width) * height;
    // yLin f32 + 7 f16 planes + chroma/den f16 + scratch + pyramid-less O-lane.
    return npix * 4 + npix * 2 * 7 + (200000 + 64) * 4 + 4096 * 4 * 2 + 128 + 65536;
}

float GaloshYuvPipeline::lastAlpha() const noexcept { return impl_ ? impl_->ctx.lastAlpha : 0.0f; }

float GaloshYuvPipeline::lastSigmaSq() const noexcept { return impl_ ? impl_->ctx.lastSigmaSq : 0.0f; }

float GaloshYuvPipeline::lastSigmaGat() const noexcept { return impl_ ? impl_->ctx.lastSigmaGat : 0.0f; }

float GaloshYuvPipeline::lastDegen() const noexcept { return impl_ ? impl_->ctx.lastDegen : 0.0f; }

void GaloshYuvPipeline::process(VkQueue queue, std::mutex& queueMutex, VkImageView srcView,
                                VkImageView dstView, uint32_t width, uint32_t height,
                                const GaloshYuvParams& params, VkQueryPool timingPool) {
    if (params.mode == GaloshYuvMode::Off) return;  // Safety net; caller skips.
    if (srcView == VK_NULL_HANDLE || dstView == VK_NULL_HANDLE || queue == VK_NULL_HANDLE) {
        throw std::invalid_argument("galosh: yuv null views/queue");
    }
    if (width == 0 || height == 0) throw std::invalid_argument("galosh: yuv bad geometry");
    YuvContext& im = impl_->ctx;
    std::lock_guard<std::mutex> lock(queueMutex);

    Call c{};
    c.im = &im;
    c.queue = queue;
    c.timing = timingPool;
    c.W = width;
    c.H = height;
    c.npix = static_cast<uint64_t>(width) * height;
    c.strengthY = params.strengthY;
    c.strengthC = params.strengthC;
    c.chromaOnly = params.mode == GaloshYuvMode::ChromaOnly || params.strengthY <= 0.0f;
    c.kneeLo = 1.0f;
    c.kneeHi = 1.5f;

    ensureResources(im, c);
    bindBridges(c, srcView, dstView);

    PcW pc[8];
#define PCI(k, v) pc[k].i = (v)
#define PCF(k, v) pc[k].f = (v)
    const uint32_t npixI = static_cast<uint32_t>(c.npix);
    const int envNbx = static_cast<int>(width / 16), envNby = static_cast<int>(height / 16);
    const int envNblk = envNbx * envNby;
    const int hw3 = (static_cast<int>(width) + 2) / 3, hh3 = (static_cast<int>(height) + 2) / 3;
    const int envNpos = static_cast<int>(height) * (static_cast<int>(width) - 4) +
                        (static_cast<int>(height) - 4) * static_cast<int>(width);

    // ---- SEG 1: bridge + envelope estimator + GAT + norm + LUT ----
    float hParams[kParamsSize] = {0};
    beginCb(c);
    writeTimestamp(c, kQueryBegin, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT);
    uploadParams(c, hParams);
    PCI(0, static_cast<int>(width));
    PCI(1, static_cast<int>(height));
    dispatchKid(c, K_BR_IN, im.sBrIn, pc, 2, aup(width, 16), aup(height, 16));
    fillBuf(c, im.envH1, 4096 * 4);
    fillBuf(c, im.envH2, 4096 * 4);
    PCI(0, static_cast<int>(width));
    PCI(1, static_cast<int>(height));
    PCI(2, envNbx);
    PCI(3, envNby);
    dispatchKid(c, K_ENV_BLK, im.sEnvBlk, pc, 4, aup(static_cast<uint32_t>(envNblk), 64), 1);
    PCI(0, static_cast<int>(width));
    PCI(1, static_cast<int>(height));
    PCI(2, envNblk);
    dispatchKid(c, K_ENV_FIN, im.sEnvFin, pc, 3, 1, 1);
    PCI(0, 0);
    dispatchKid(c, K_ENV_SEL, im.sEnvSel, pc, 1, 1, 1);
    PCI(0, static_cast<int>(width));
    PCI(1, static_cast<int>(height));
    dispatchKid(c, K_ENV_DTH, im.sEnvDth, pc, 2, aup(static_cast<uint32_t>(hw3), 16),
                aup(static_cast<uint32_t>(hh3), 16));
    PCI(0, kPEnvDarkThresh);
    dispatchKid(c, K_ENV_DTF, im.sEnvDtf, pc, 1, 1, 1);
    PCI(0, static_cast<int>(width));
    PCI(1, static_cast<int>(height));
    PCI(2, kPEnvDarkThresh);
    dispatchKid(c, K_ENV_DLH, im.sEnvDlh, pc, 3, aup(static_cast<uint32_t>(envNpos), 64), 1);
    PCI(0, kPEnvDarkThresh);
    dispatchKid(c, K_ENV_DFN, im.sEnvDfn, pc, 1, 1, 1);
    // Fallback MAD pair (spare slots; adopted only when degenerate).
    PCI(0, static_cast<int>(width));
    PCI(1, static_cast<int>(height));
    PCI(2, 3);
    PCI(3, kPSigmaY);
    dispatchKid(c, K_LAP_MAD, im.sMad, pc, 4, 1, 1);
    PCI(0, kPSigmaY);
    PCI(1, kPAlphaMad);
    PCI(2, kPSigmaSqMad);
    dispatchKid(c, K_SYNTH, im.sSyn, pc, 3, 1, 1);
    PCI(0, 1);
    dispatchKid(c, K_ENV_SEL, im.sEnvSel, pc, 1, 1, 1);
    PCI(0, npixI);
    dispatchKid(c, K_GAT_FWD, im.sGat, pc, 1, aup(npixI, 256), 1);
    PCI(0, static_cast<int>(width));
    PCI(1, static_cast<int>(height));
    PCI(2, 3);
    PCI(3, kPSigmaGat);
    dispatchKid(c, K_LAP_MAD_H16, im.sMad16, pc, 4, 1, 1);
    PCI(0, kPSigmaGat);
    PCI(1, npixI);
    dispatchKid(c, K_NORM, im.sNorm, pc, 2, aup(npixI, 256), 1);
    // Noisy-guide snapshots BEFORE pass12 (blueprint guide-domain rule)
    // plus the linear-Y snapshot for the HDR knee (yLin is overwritten by
    // makitalo later; cbB/crB stay pristine and are bound directly).
    submitWait(c);
    beginCb(c);
    VkBufferCopy snap{};
    snap.srcOffset = 0;
    snap.dstOffset = 0;
    snap.size = c.npix * 4;
    vkCmdCopyBuffer(im.cmd, im.yLin.buf, im.yNoisySnap.buf, 1, &snap);
    vk::computeBarrier(im.cmd);
    VkBufferCopy snap2{};
    snap2.srcOffset = 0;
    snap2.dstOffset = 0;
    snap2.size = c.npix * 2;
    vkCmdCopyBuffer(im.cmd, im.yStab.buf, im.ySnap.buf, 1, &snap2);
    vk::computeBarrier(im.cmd);
    submitWait(c);
    beginCb(c);
    dispatchKid(c, K_LUT_BUILD, im.sLut, nullptr, 0, kGatLutSize / 256, 1);
    dispatchKid(c, K_LUT_FIN, im.sLutFin, nullptr, 0, 1, 1);
    submitWait(c);

    // ---- pass12 (banded) or chroma-only lane bypass ----
    if (c.chromaOnly) {
        beginCb(c);
        VkBufferCopy cpy{};
        cpy.srcOffset = 0;
        cpy.dstOffset = 0;
        cpy.size = c.npix * 2;
        vkCmdCopyBuffer(im.cmd, im.yStab.buf, im.yDen.buf, 1, &cpy);
        vk::computeBarrier(im.cmd);
        submitWait(c);
    } else {
        PCI(0, static_cast<int>(width));
        PCI(1, static_cast<int>(height));
        PCF(2, c.strengthY);
        PCI(3, 1);
        banded(c, K_PASS12, im.sP12, pc, 4, aup(width, kO32Tile), aup(height, kO32Tile));
    }

    // ---- SEG 2: denorm + inverse + LOESS + recompose ----
    beginCb(c);
    PCI(0, kPSigmaGat);
    PCI(1, npixI);
    dispatchKid(c, K_DENORM, im.sDen, pc, 2, aup(npixI, 256), 1);
    PCI(0, npixI);
    dispatchKid(c, K_MAKITALO, im.sMak, pc, 1, aup(npixI, 256), 1);
    submitWait(c);
    {
        const float strengthReg = std::max(c.strengthC, 1.0f);
        const float blendW = std::min(c.strengthC, 1.0f);
        PCI(0, static_cast<int>(width));
        PCI(1, static_cast<int>(height));
        PCF(2, strengthReg);
        PCI(3, 0);
        PCI(4, 0);
        PCF(5, blendW);
        banded(c, K_LOESS, im.sLo, pc, 6, aup(width, 16), aup(height, 16));
    }
    beginCb(c);
    PCI(0, static_cast<int>(width));
    PCI(1, static_cast<int>(height));
    PCF(2, c.kneeLo);
    PCF(3, c.kneeHi);
    dispatchKid(c, K_BR_OUT, im.sBrOut, pc, 4, aup(width, 16), aup(height, 16));
    writeTimestamp(c, kQueryEnd, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT);
    submitWait(c);

    // Fit readback for logging (after the frame, like upstream).
    {
        float est[kParamsSize] = {0};
        downloadParams(c, est);
        im.lastAlpha = est[kPAlpha];
        im.lastSigmaSq = est[kPSigmaSq];
        im.lastSigmaGat = est[kPSigmaGat];
        im.lastDegen = est[kPEnvDegen];
    }
#undef PCI
#undef PCF
}

}  // namespace galosh
