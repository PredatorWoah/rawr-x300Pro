// GaloshRawPipeline: app-native Vulkan port of GALOSH-RAW (o32 engine).
//
// Faithful to tmp/GALOSH/standalone/vk/galosh_vk.c section by section
// (same kernels, bindings, push layouts, dispatch grids, barriers).
// Adaptations vs upstream ([PORT] marks):
//  - No instance/device/file/env: caller-owned device + queue; stills
//    fit-mode only (no hold/every/ema state files, no dumps, no CLI).
//  - Synchronous stage (own submits + fence waits): upstream needs two
//    mid-pipe host syncs (blind-fit readback, LUT/state), which a
//    record-only single submit cannot serve. Matches the demosaic-worker
//    pattern; record-only fusion is a P2 optimization, not P1a.
//  - Buffer graph upstream-verbatim; R16_UINT image bridges at the edges
//    (app-authored galosh_bridge_{norm,quant}.comp): input_ codes are
//    sensor integers, the graph needs normalized floats.
//  - Wiener-NaN guards (UPSTREAM_PATCHES.patch, reimplemented): luma<=0
//    in Full = no-op safety net; ChromaOnly copies L_cs -> L_cs_den.
//  - Subgroup-cooperative pass12 disabled: it needs
//    VK_EXT_subgroup_size_control enabled at device creation, which the
//    app device does not carry. Classic o32_pass12 serves all GPUs.
//  - Timestamps 12->13 on the caller pool (nullable); pool expansion
//    12->16 lands with the app wiring (P1c).
//  - RGGB only (upstream bakes quad phase 0); other CFA codes throw.

#include "galosh/GaloshRawPipeline.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <initializer_list>
#include <mutex>
#include <stdexcept>
#include <string>
#include <vector>

#include "galosh/GaloshVkUtils.hpp"

namespace galosh {
namespace {

// Upstream blueprint constants (galosh_vk.c).
constexpr size_t kParamsSize = 32;
constexpr size_t kGatLutSize = 4096;
constexpr int kNReduceWg = 64;
constexpr int kO32Tile = 28;
constexpr int kPScale = 10;
constexpr int kPLumaStr = 11;
constexpr int kPChromaStr = 12;
constexpr int kPAlpha = 13;
constexpr int kPSigmaSq = 14;
constexpr int kPDarkThresh = 15;

// Query slots on the caller pool (app expands 12 -> 16 in P1c).
constexpr uint32_t kQueryBegin = 12;
constexpr uint32_t kQueryEnd = 13;

constexpr uint32_t aup(uint32_t n, uint32_t a) { return (n + a - 1) / a; }

enum Kernel {
    K_NE_STATS, K_NE_FIN, K_NE_DT_HIST, K_NE_DT_FIN, K_NE_DL_HIST, K_NE_D_FIN,
    K_LUT_BUILD, K_LUT_FIN, K_GAT_FWD, K_SIGMA_CFA, K_UNIFIED, K_NORMALIZE,
    K_DR_REDUCE, K_DR_FIN, K_DR_RREDUCE, K_DR_RFIN, K_DARK_SUB,
    K_FWD_L, K_CHROMA_EX, K_PASS12, K_P6_FUSED,
    K_BOX2, K_BOX2_3P, K_LOESS_T, K_CROP, K_K16, K_PAD, K_SMOOTH, K_INV,
    K_PASS12_W4, K_FASTUP,
    K_BOX2_H16, K_CROP_H16, K_LOESS_T_G16, K_K16_F16, K_FASTUP_F16,
    K_SIGMA_HIST, K_SIGMA_FIN,
    K_PAD3, K_K16_INV_F, K_FASTUP_INV_F,
    K_BR_NORM, K_BR_QUANT,  // [PORT] app-authored R16U bridges.
    K_COUNT
};

struct KernelDesc {
    const char* name;
    int nbind;
    int pushBytes;
    bool optional;  // Missing from the map: skip (never dispatched).
};

// nbind/push mirror upstream g_k[] exactly.
const KernelDesc kKernels[K_COUNT] = {
    {"o32_ne_block_stats", 3, 20, false},
    {"o32_ne_finalize", 4, 12, false},
    {"o32_ne_dark_thresh_hist", 2, 8, false},
    {"o32_ne_dark_thresh_finalize", 2, 4, false},
    {"o32_ne_dark_lap_hist", 3, 12, false},
    {"o32_ne_dark_finalize", 2, 4, false},
    {"o32_build_inv_lut", 4, 0, false},
    {"o32_lut_finalize", 2, 0, false},
    {"o32_gat_forward_full", 7, 8, false},
    {"o32_sigma_per_cfa", 2, 8, true},  // [PORT] two-stage HIST/FIN is production.
    {"o32_unified_sigma", 1, 0, false},
    {"o32_normalize_apply", 6, 8, false},
    {"o32_dark_ref_reduce_mwg", 4, 8, false},
    {"o32_dark_ref_finalize_mwg", 2, 4, false},
    {"o32_dark_resid_reduce_mwg", 4, 8, false},
    {"o32_dark_resid_finalize_mwg", 2, 12, false},
    {"o32_dark_sub_full", 6, 8, false},
    {"o32_forward_l_stride1", 2, 8, false},
    {"o32_chroma_extract_halfres", 4, 16, false},
    {"o32_pass12", 2, 16, false},
    {"o32_lpixel_lh_den_fused", 3, 12, false},
    {"o32_box_downsample_2x", 2, 8, false},
    {"o32_box_downsample_2x_3p", 6, 8, false},
    {"o32_loess_chroma_3p_tiled", 7, 12, false},
    {"o32_crop_2d_topleft", 2, 16, false},
    {"o32_k16_jbu_3p", 7, 12, false},
    {"o32_pad_2d_edge", 2, 16, true},  // [PORT] K_PAD3 supersedes; dump-path only.
    {"o32_smoothstep_blend_3p", 15, 12, false},
    {"o32_inverse_wht_dark_gat", 9, 8, true},  // [PORT] fused final is production.
    {"o32_pass12_wht4", 2, 16, false},
    {"o32_fastup_3p", 7, 12, false},
    {"o32_box_downsample_2x_h16", 2, 8, false},
    {"o32_crop_2d_topleft_h16", 2, 16, false},
    {"o32_loess_chroma_3p_tiled_g16", 7, 12, false},
    {"o32_k16_jbu_3p_f16", 7, 12, false},
    {"o32_fastup_3p_f16", 7, 12, false},
    {"o32_sigma_hist_mwg", 2, 12, false},
    {"o32_sigma_fin_mwg", 2, 0, false},
    {"o32_pad_2d_edge_3p", 6, 16, false},
    {"o32_k16_inverse_fused", 9, 12, false},
    {"o32_fastup_inverse_fused", 9, 12, false},
    {"galosh_bridge_norm", 3, 8, false},
    {"galosh_bridge_quant", 2, 16, false},
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

// TU-local execution state (NOT nested: free helpers below need access and
// the header keeps Impl opaque; Impl is a one-field wrapper over this).
struct RawContext {
    VkPhysicalDevice physical = VK_NULL_HANDLE;
    VkDevice device = VK_NULL_HANDLE;
    uint32_t queueFamily = 0;

    VkCommandPool pool = VK_NULL_HANDLE;
    VkCommandBuffer cmd = VK_NULL_HANDLE;  // One reusable CB; see submitWait().
    VkFence fence = VK_NULL_HANDLE;
    VkDescriptorPool dpool = VK_NULL_HANDLE;  // Rebuilt per geometry epoch.

    std::array<BuiltKernel, K_COUNT> kernels{};

    // Geometry epoch (buffers + sets bound to these).
    uint32_t epochW = 0;
    uint32_t epochH = 0;

    // Transient graph buffers (upstream TRI/DEVBUF names + port additions).
    vk::Buffer inFloat, ch0, ch1, ch2, ch3, params;
    vk::Buffer lutD, lutX, lutP, part, partR, blkM, blkV;
    vk::Buffer dtHist, dlHist, sigmaHist, inGat;
    vk::Buffer lCs, lCsDen, lPixel, lHDen;
    vk::Buffer c1H, c2H, c3H;
    vk::Buffer lQ, lE, lForQ, lForE;
    vk::Buffer cQ1, cQ2, cQ3, cE1, cE2, cE3;
    vk::Buffer clH1, clH2, clH3, clQ1, clQ2, clQ3, clE1, clE2, clE3;
    vk::Buffer cqupR1, cqupR2, cqupR3, cqup1, cqup2, cqup3;
    vk::Buffer ceqR1, ceqR2, ceqR3, ceq1, ceq2, ceq3;
    vk::Buffer ceupR1, ceupR2, ceupR3, ceup1, ceup2, ceup3;
    vk::Buffer cden1, cden2, cden3, cal1, cal2, cal3;
    vk::Buffer outFloat;  // [PORT] final float plane for the quant bridge.
    vk::Buffer staging;   // Host-visible: params upload + SYNC#1 readback.
    void* stagingMap = nullptr;  // Persistent map of staging (coherent).
    vk::Buffer normLut;   // 65536-entry host-exact normalize LUT (see shader).
    float lutBlack = 0.0f, lutWhite = 0.0f;
    bool lutValid = false;
    float lastAlpha = 0.0f;      // SYNC#1 fit results (validation logging).
    float lastSigmaSq = 0.0f;

    // Per-site descriptor sets (upstream s_* names).
    VkDescriptorSet sBrNorm = VK_NULL_HANDLE, sBrQuant = VK_NULL_HANDLE;
    VkDescriptorSet sNeStats = VK_NULL_HANDLE, sNeFin = VK_NULL_HANDLE;
    VkDescriptorSet sDtHist = VK_NULL_HANDLE, sDtFin = VK_NULL_HANDLE;
    VkDescriptorSet sDlHist = VK_NULL_HANDLE, sDFin = VK_NULL_HANDLE;
    VkDescriptorSet sLut = VK_NULL_HANDLE, sLutFin = VK_NULL_HANDLE;
    VkDescriptorSet sGat = VK_NULL_HANDLE, sSigH = VK_NULL_HANDLE, sSigF = VK_NULL_HANDLE;
    VkDescriptorSet sUnified = VK_NULL_HANDLE, sNorm = VK_NULL_HANDLE;
    VkDescriptorSet sDrRed = VK_NULL_HANDLE, sDrFin = VK_NULL_HANDLE;
    VkDescriptorSet sDrRred = VK_NULL_HANDLE, sDrRfin = VK_NULL_HANDLE;
    VkDescriptorSet sDsub = VK_NULL_HANDLE, sFwdL = VK_NULL_HANDLE, sCex = VK_NULL_HANDLE;
    VkDescriptorSet sP12 = VK_NULL_HANDLE, sP6 = VK_NULL_HANDLE;
    VkDescriptorSet sLq = VK_NULL_HANDLE, sLe = VK_NULL_HANDLE;
    VkDescriptorSet sCq = VK_NULL_HANDLE, sCe = VK_NULL_HANDLE;
    VkDescriptorSet sLoH = VK_NULL_HANDLE, sLoQ = VK_NULL_HANDLE, sLoE = VK_NULL_HANDLE;
    VkDescriptorSet sCropQ = VK_NULL_HANDLE, sK16Q2H = VK_NULL_HANDLE, sPadQ3 = VK_NULL_HANDLE;
    VkDescriptorSet sCropE = VK_NULL_HANDLE, sK16E2Q = VK_NULL_HANDLE, sPadE3 = VK_NULL_HANDLE;
    VkDescriptorSet sK16EQ2H = VK_NULL_HANDLE, sPadEU3 = VK_NULL_HANDLE;
    VkDescriptorSet sSmooth = VK_NULL_HANDLE, sFinFused = VK_NULL_HANDLE;
};

struct GaloshRawPipeline::Impl {
    RawContext ctx;
};

namespace {

// Push-constant word: int or float view of 4 bytes (upstream PcW).
union PcW {
    int32_t i;
    float f;
};

void checkImpl(bool ok, const std::string& what) {
    if (!ok) throw std::runtime_error("galosh: " + what);
}

// --- per-process() recording state (mirrors upstream main() locals) ---
struct Call {
    RawContext* im = nullptr;
    VkQueue queue = VK_NULL_HANDLE;
    VkQueryPool timing = VK_NULL_HANDLE;
    bool recording = false;  // im.cmd currently recording (submitWait clears).
    uint32_t W = 0, H = 0, hw = 0, hh = 0;
    uint32_t cqW = 0, cqH = 0, ceW = 0, ceH = 0, kqW = 0, kqH = 0, keW = 0, keH = 0;
    uint64_t npix = 0, chsize = 0;
    uint32_t nePerCh = 0;
    uint64_t neTotal = 0;
    int hwA = 0, hhA = 0, hw3 = 0, hh3 = 0;
    float strength = 1.0f, luma = 1.0f, chroma = 1.0f;
    float alphaExt = 0.0f, sigExt = 0.0f;
    bool extModel = false;
    bool chromaOnly = false;
    int wht = 8;
    bool fastUp = false;
    float black = 0.0f, white = 1.0f;
};

void beginCb(Call& c) {
    VkCommandBufferBeginInfo bi{};
    bi.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vk::check(vkResetCommandBuffer(c.im->cmd, 0), "raw reset cmd");
    vk::check(vkBeginCommandBuffer(c.im->cmd, &bi), "raw begin cmd");
    c.recording = true;
}

void submitWait(Call& c) {
    vk::check(vkEndCommandBuffer(c.im->cmd), "raw end cmd");
    VkSubmitInfo si{};
    si.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    si.commandBufferCount = 1;
    si.pCommandBuffers = &c.im->cmd;
    // Caller holds the queue mutex for the whole process() call.
    vk::check(vkResetFences(c.im->device, 1, &c.im->fence), "raw reset fence");
    vk::check(vkQueueSubmit(c.queue, 1, &si, c.im->fence), "raw submit");
    vk::check(vkWaitForFences(c.im->device, 1, &c.im->fence, VK_TRUE, UINT64_MAX), "raw fence");
    c.recording = false;
}

void dispatchKid(Call& c, Kernel kid, VkDescriptorSet set, PcW* pc, int npc, uint32_t gx,
                 uint32_t gy, uint32_t gz = 1) {
    const BuiltKernel& k = c.im->kernels[static_cast<size_t>(kid)];
    checkImpl(k.present, "raw kernel not built");
    vkCmdBindPipeline(c.im->cmd, VK_PIPELINE_BIND_POINT_COMPUTE, k.pipe);
    vkCmdBindDescriptorSets(c.im->cmd, VK_PIPELINE_BIND_POINT_COMPUTE, k.pl, 0, 1, &set, 0, nullptr);
    if (npc > 0) {
        vkCmdPushConstants(c.im->cmd, k.pl, VK_SHADER_STAGE_COMPUTE_BIT, 0,
                           static_cast<uint32_t>(npc * 4), pc);
    }
    vkCmdDispatch(c.im->cmd, gx, gy, gz);
    vk::computeBarrier(c.im->cmd);
}

void fillBuf(Call& c, vk::Buffer& b, uint32_t v, VkDeviceSize size) {
    vkCmdFillBuffer(c.im->cmd, b.buf, 0, size, v);
    vk::computeBarrier(c.im->cmd);
}

void updateTrio(Call& c, float alpha, float sigmaSq) {
    // [PORT] upstream pend_trio path (ext overrides only; blind-fit values
    // already live in params from P0 finalize). 4-byte vkCmdUpdateBuffer units.
    const float trio[3] = {alpha, sigmaSq, sigmaSq / std::max(alpha, 1e-12f)};
    vkCmdUpdateBuffer(c.im->cmd, c.im->params.buf, kPAlpha * 4, 4, &trio[0]);
    vkCmdUpdateBuffer(c.im->cmd, c.im->params.buf, kPSigmaSq * 4, 4, &trio[1]);
    vkCmdUpdateBuffer(c.im->cmd, c.im->params.buf, kPScale * 4, 4, &trio[2]);
    vk::computeBarrier(c.im->cmd);
}

// Adaptive banded pass12 (upstream dispatch_k_banded, submission-per-band
// preserved for the TDR preemption property; internal profiling dropped,
// the caller pool carries only the 12->13 span).
//
// Band sizing (Adreno watchdog): o32_pass12 carries a ~2000-op selection
// sort per block (~16 GFLOP for a 12 MP frame in ONE submit). A single
// remainder submit runs past the driver watchdog on mobile GPUs (device
// lost ~5 s in, while chroma-only — which skips pass12 — and the much
// lighter YUV LOESS survive on the same device). Bands are independent
// by construction (halo comes from the input buffer, accumulators live
// in workgroup-shared memory), so finer banding is bit-identical math
// with bounded per-submit work. 2 rows x 48 groups per submit keeps the
// heaviest piece at ~1/3 of the (surviving) probe cost.
void pass12Banded(Call& c, Kernel kid, VkDescriptorSet set, PcW* pc, uint32_t gx, uint32_t gy) {
    const uint32_t probeRows = (gy > 2) ? 2u : gy;
    constexpr uint32_t kBandRows = 2;
    constexpr uint32_t kBandGroupsX = 48;
    auto piece = [&](uint32_t x0, uint32_t y0, uint32_t nx, uint32_t rows) {
        beginCb(c);
        const BuiltKernel& k = c.im->kernels[static_cast<size_t>(kid)];
        vkCmdBindPipeline(c.im->cmd, VK_PIPELINE_BIND_POINT_COMPUTE, k.pipe);
        vkCmdBindDescriptorSets(c.im->cmd, VK_PIPELINE_BIND_POINT_COMPUTE, k.pl, 0, 1, &set, 0,
                                nullptr);
        vkCmdPushConstants(c.im->cmd, k.pl, VK_SHADER_STAGE_COMPUTE_BIT, 0, 16, pc);
        vkCmdDispatchBase(c.im->cmd, x0, y0, 0, nx, rows, 1);
        vk::computeBarrier(c.im->cmd);
        submitWait(c);
    };
    for (uint32_t y = 0; y < probeRows; y += kBandRows) {
        const uint32_t rows = std::min(kBandRows, probeRows - y);
        for (uint32_t x = 0; x < gx; x += kBandGroupsX) {
            piece(x, y, std::min(kBandGroupsX, gx - x), rows);
        }
    }
    for (uint32_t y = probeRows; y < gy; y += kBandRows) {
        const uint32_t rows = std::min(kBandRows, gy - y);
        for (uint32_t x = 0; x < gx; x += kBandGroupsX) {
            piece(x, y, std::min(kBandGroupsX, gx - x), rows);
        }
    }
}

void downloadParams(Call& c, float* out128) {
    // [PORT] upstream HOST SYNC #1 (params readback for alpha/sigma).
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
    RawContext* im = c.im;
    const BuiltKernel& k = im->kernels[static_cast<size_t>(kid)];
    checkImpl(bufs.size() == static_cast<size_t>(k.nbind), "raw set arity");
    VkDescriptorSetAllocateInfo ai{};
    ai.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    ai.descriptorPool = im->dpool;
    ai.descriptorSetCount = 1;
    ai.pSetLayouts = &k.dsl;
    VkDescriptorSet ds = VK_NULL_HANDLE;
    vk::check(vkAllocateDescriptorSets(im->device, &ai, &ds), "raw alloc set");
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

}  // namespace

GaloshRawPipeline::GaloshRawPipeline(VkPhysicalDevice physicalDevice, VkDevice device,
                                     uint32_t queueFamily, GaloshShaderMap shaders)
    : impl_(new Impl()) {
    RawContext& im = impl_->ctx;
    im.physical = physicalDevice;
    im.device = device;
    im.queueFamily = queueFamily;

    VkCommandPoolCreateInfo cpci{};
    cpci.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    cpci.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    cpci.queueFamilyIndex = queueFamily;
    vk::check(vkCreateCommandPool(device, &cpci, nullptr, &im.pool), "raw pool");

    VkCommandBufferAllocateInfo cai{};
    cai.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    cai.commandPool = im.pool;
    cai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    cai.commandBufferCount = 1;
    vk::check(vkAllocateCommandBuffers(device, &cai, &im.cmd), "raw cmd");

    VkFenceCreateInfo fci{};
    fci.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    vk::check(vkCreateFence(device, &fci, nullptr, &im.fence), "raw fence");

    im.staging = vk::makeBuffer(physicalDevice, device, 4096,
                                VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                                VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                                "raw staging");
    vk::check(vkMapMemory(device, im.staging.mem, 0, VK_WHOLE_SIZE, 0, &im.stagingMap),
              "raw map staging");

    // Pipelines up front (correctness first; a pipeline cache is a P2 item).
    for (int k = 0; k < K_COUNT; ++k) {
        const KernelDesc& desc = kKernels[k];
        auto it = shaders.find(desc.name);
        if (it == shaders.end() || it->second.words == nullptr || it->second.wordCount == 0) {
            checkImpl(desc.optional, std::string("raw missing shader: ") + desc.name);
            continue;
        }
        BuiltKernel& bk = im.kernels[static_cast<size_t>(k)];
        std::vector<VkDescriptorSetLayoutBinding> binds(static_cast<size_t>(desc.nbind));
        for (int i = 0; i < desc.nbind; ++i) {
            binds[static_cast<size_t>(i)].binding = static_cast<uint32_t>(i);
            binds[static_cast<size_t>(i)].descriptorType = (static_cast<Kernel>(k) == K_BR_NORM && i == 0) ||
                                                                   (static_cast<Kernel>(k) == K_BR_QUANT && i == 1)
                                                               ? VK_DESCRIPTOR_TYPE_STORAGE_IMAGE
                                                               : VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
            binds[static_cast<size_t>(i)].descriptorCount = 1;
            binds[static_cast<size_t>(i)].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
        }
        VkDescriptorSetLayoutCreateInfo dsli{};
        dsli.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
        dsli.bindingCount = static_cast<uint32_t>(desc.nbind);
        dsli.pBindings = binds.data();
        vk::check(vkCreateDescriptorSetLayout(device, &dsli, nullptr, &bk.dsl), "raw dsl");

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
        vk::check(vkCreatePipelineLayout(device, &pli, nullptr, &bk.pl), "raw layout");

        VkShaderModuleCreateInfo sm{};
        sm.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
        sm.codeSize = it->second.wordCount * 4;
        sm.pCode = it->second.words;
        VkShaderModule mod = VK_NULL_HANDLE;
        vk::check(vkCreateShaderModule(device, &sm, nullptr, &mod), "raw module");
        VkComputePipelineCreateInfo cpi{};
        cpi.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
        cpi.flags = VK_PIPELINE_CREATE_DISPATCH_BASE_BIT;
        cpi.stage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        cpi.stage.stage = VK_SHADER_STAGE_COMPUTE_BIT;
        cpi.stage.module = mod;
        cpi.stage.pName = "main";
        cpi.layout = bk.pl;
        vk::check(vkCreateComputePipelines(device, VK_NULL_HANDLE, 1, &cpi, nullptr, &bk.pipe),
                  "raw pipe");
        vkDestroyShaderModule(device, mod, nullptr);
        bk.nbind = desc.nbind;
        bk.pushBytes = desc.pushBytes;
        bk.present = true;
    }
}

GaloshRawPipeline::~GaloshRawPipeline() {
    if (impl_ == nullptr) return;
    RawContext& im = impl_->ctx;
    // Caller-idle contract: no in-flight use at destruction (still fence).
    for (auto& bk : im.kernels) {
        if (bk.pipe != VK_NULL_HANDLE) vkDestroyPipeline(im.device, bk.pipe, nullptr);
        if (bk.pl != VK_NULL_HANDLE) vkDestroyPipelineLayout(im.device, bk.pl, nullptr);
        if (bk.dsl != VK_NULL_HANDLE) vkDestroyDescriptorSetLayout(im.device, bk.dsl, nullptr);
    }
    auto freeBufs = [&] {
        vk::freeBuffer(im.device, im.inFloat);
        vk::freeBuffer(im.device, im.ch0);
        vk::freeBuffer(im.device, im.ch1);
        vk::freeBuffer(im.device, im.ch2);
        vk::freeBuffer(im.device, im.ch3);
        vk::freeBuffer(im.device, im.params);
        vk::freeBuffer(im.device, im.lutD);
        vk::freeBuffer(im.device, im.lutX);
        vk::freeBuffer(im.device, im.lutP);
        vk::freeBuffer(im.device, im.part);
        vk::freeBuffer(im.device, im.partR);
        vk::freeBuffer(im.device, im.blkM);
        vk::freeBuffer(im.device, im.blkV);
        vk::freeBuffer(im.device, im.dtHist);
        vk::freeBuffer(im.device, im.dlHist);
        vk::freeBuffer(im.device, im.sigmaHist);
        vk::freeBuffer(im.device, im.inGat);
        vk::freeBuffer(im.device, im.lCs);
        vk::freeBuffer(im.device, im.lCsDen);
        vk::freeBuffer(im.device, im.lPixel);
        vk::freeBuffer(im.device, im.lHDen);
        vk::freeBuffer(im.device, im.c1H);
        vk::freeBuffer(im.device, im.c2H);
        vk::freeBuffer(im.device, im.c3H);
        vk::freeBuffer(im.device, im.lQ);
        vk::freeBuffer(im.device, im.lE);
        vk::freeBuffer(im.device, im.lForQ);
        vk::freeBuffer(im.device, im.lForE);
        vk::freeBuffer(im.device, im.cQ1);
        vk::freeBuffer(im.device, im.cQ2);
        vk::freeBuffer(im.device, im.cQ3);
        vk::freeBuffer(im.device, im.cE1);
        vk::freeBuffer(im.device, im.cE2);
        vk::freeBuffer(im.device, im.cE3);
        vk::freeBuffer(im.device, im.clH1);
        vk::freeBuffer(im.device, im.clH2);
        vk::freeBuffer(im.device, im.clH3);
        vk::freeBuffer(im.device, im.clQ1);
        vk::freeBuffer(im.device, im.clQ2);
        vk::freeBuffer(im.device, im.clQ3);
        vk::freeBuffer(im.device, im.clE1);
        vk::freeBuffer(im.device, im.clE2);
        vk::freeBuffer(im.device, im.clE3);
        vk::freeBuffer(im.device, im.cqupR1);
        vk::freeBuffer(im.device, im.cqupR2);
        vk::freeBuffer(im.device, im.cqupR3);
        vk::freeBuffer(im.device, im.cqup1);
        vk::freeBuffer(im.device, im.cqup2);
        vk::freeBuffer(im.device, im.cqup3);
        vk::freeBuffer(im.device, im.ceqR1);
        vk::freeBuffer(im.device, im.ceqR2);
        vk::freeBuffer(im.device, im.ceqR3);
        vk::freeBuffer(im.device, im.ceq1);
        vk::freeBuffer(im.device, im.ceq2);
        vk::freeBuffer(im.device, im.ceq3);
        vk::freeBuffer(im.device, im.ceupR1);
        vk::freeBuffer(im.device, im.ceupR2);
        vk::freeBuffer(im.device, im.ceupR3);
        vk::freeBuffer(im.device, im.ceup1);
        vk::freeBuffer(im.device, im.ceup2);
        vk::freeBuffer(im.device, im.ceup3);
        vk::freeBuffer(im.device, im.cden1);
        vk::freeBuffer(im.device, im.cden2);
        vk::freeBuffer(im.device, im.cden3);
        vk::freeBuffer(im.device, im.cal1);
        vk::freeBuffer(im.device, im.cal2);
        vk::freeBuffer(im.device, im.cal3);
        vk::freeBuffer(im.device, im.outFloat);
        vk::freeBuffer(im.device, im.normLut);
        im.lutValid = false;
    };
    freeBufs();
    if (im.dpool != VK_NULL_HANDLE) vkDestroyDescriptorPool(im.device, im.dpool, nullptr);
    if (im.stagingMap != nullptr) vkUnmapMemory(im.device, im.staging.mem);
    vk::freeBuffer(im.device, im.staging);
    if (im.fence != VK_NULL_HANDLE) vkDestroyFence(im.device, im.fence, nullptr);
    if (im.pool != VK_NULL_HANDLE) vkDestroyCommandPool(im.device, im.pool, nullptr);
    delete impl_;
    impl_ = nullptr;
}

uint64_t GaloshRawPipeline::scratchBytes(uint32_t width, uint32_t height) const noexcept {    // Mirrors the ensureResources sizing (f32 planes + f16 contract planes).
    const uint64_t npix = static_cast<uint64_t>(width) * height;
    const uint64_t hw = width / 2u, hh = height / 2u;
    const uint64_t ch = hw * hh;
    // inFloat+inGat+outFloat (3x f32 full) + L f16 planes (4x full/2) +
    // chroma f32 half planes (~30x ch) + pyramid f32 + histograms/params.
    return npix * 12 + npix * 2 + ch * 4 * 30 + ch + 1024 * 1024;
}

namespace {

// (Re)allocates the transient graph + descriptor pool/sets for a geometry
// epoch. Caller-idle contract (same as demosaicer configure()).
void ensureResources(RawContext& im, Call& c) {
    if (im.epochW == c.W && im.epochH == c.H && im.dpool != VK_NULL_HANDLE) return;

    if (im.dpool != VK_NULL_HANDLE) {
        vkDestroyDescriptorPool(im.device, im.dpool, nullptr);
        im.dpool = VK_NULL_HANDLE;
        // Pool destruction invalidates all sets. Epoch-bound sets below are
        // overwritten; per-call sets must be nulled so the per-call bind
        // paths don't vkFree stale handles on the new pool.
        im.sP12 = VK_NULL_HANDLE;
        im.sBrNorm = VK_NULL_HANDLE;
        im.sBrQuant = VK_NULL_HANDLE;
    }
    auto mk = [&](vk::Buffer& b, VkDeviceSize size) {
        vk::freeBuffer(im.device, b);
        b = vk::makeBuffer(im.physical, im.device, size,
                           VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT |
                               VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                           VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, "raw graph");
    };
    const size_t npix = static_cast<size_t>(c.W) * c.H;
    const size_t chsize = static_cast<size_t>(c.hw) * c.hh;
    const size_t fullF = npix * 4, chF = chsize * 4;
    const size_t fullH16 = npix * 2, chH16 = chsize * 2;
    const size_t cqF = static_cast<size_t>(c.cqW) * c.cqH * 4;
    const size_t ceF = static_cast<size_t>(c.ceW) * c.ceH * 4;
    const size_t kqF = static_cast<size_t>(2 * c.cqW) * (2 * c.cqH) * 4;
    const size_t keF = static_cast<size_t>(2 * c.ceW) * (2 * c.ceH) * 4;
    auto nz = [](size_t s) { return s > 4 ? s : 4; };
    mk(im.inFloat, fullF);
    mk(im.ch0, chF);
    mk(im.ch1, chF);
    mk(im.ch2, chF);
    mk(im.ch3, chF);
    mk(im.params, kParamsSize * 4);
    mk(im.lutD, kGatLutSize * 4);
    mk(im.lutX, kGatLutSize * 4);
    mk(im.lutP, 32);
    mk(im.part, kNReduceWg * 5u * 2u * 4u);
    mk(im.partR, kNReduceWg * 2u * 2u * 4u);
    mk(im.blkM, c.neTotal * 4);
    mk(im.blkV, c.neTotal * 4);
    mk(im.dtHist, 4096 * 4);
    mk(im.dlHist, 4096 * 4);
    mk(im.sigmaHist, 4 * 4096 * 4);
    mk(im.inGat, fullF);
    mk(im.lCs, fullH16);
    mk(im.lCsDen, fullH16);
    mk(im.lPixel, fullH16);
    mk(im.lHDen, chH16);
    mk(im.c1H, chF);
    mk(im.c2H, chF);
    mk(im.c3H, chF);
    mk(im.lQ, nz(cqF));
    mk(im.lE, nz(ceF));
    mk(im.lForQ, nz(kqF));
    mk(im.lForE, nz(keF));
    // Pyramid planes (upstream TRI sizes; kept one-per-line: merging these
    // caused a 4x undersize of Ceq (cq_f, not ce_f) and heap corruption).
    for (vk::Buffer* b : {&im.cQ1, &im.cQ2, &im.cQ3}) mk(*b, nz(cqF));
    for (vk::Buffer* b : {&im.cE1, &im.cE2, &im.cE3}) mk(*b, nz(ceF));
    for (vk::Buffer* b : {&im.ceq1, &im.ceq2, &im.ceq3}) mk(*b, nz(cqF));
    for (vk::Buffer* b : {&im.clH1, &im.clH2, &im.clH3, &im.cqup1, &im.cqup2, &im.cqup3}) mk(*b, chF);
    for (vk::Buffer* b : {&im.clQ1, &im.clQ2, &im.clQ3}) mk(*b, nz(cqF));
    for (vk::Buffer* b : {&im.clE1, &im.clE2, &im.clE3}) mk(*b, nz(ceF));
    for (vk::Buffer* b : {&im.cqupR1, &im.cqupR2, &im.cqupR3, &im.ceupR1, &im.ceupR2,
                           &im.ceupR3})
        mk(*b, nz(kqF));
    for (vk::Buffer* b : {&im.ceqR1, &im.ceqR2, &im.ceqR3}) mk(*b, nz(keF));
    for (vk::Buffer* b : {&im.ceup1, &im.ceup2, &im.ceup3}) mk(*b, chF);
    for (vk::Buffer* b : {&im.cden1, &im.cden2, &im.cden3}) mk(*b, chH16);
    for (vk::Buffer* b : {&im.cal1, &im.cal2, &im.cal3}) mk(*b, fullH16);
    mk(im.outFloat, fullF);
    if (im.normLut.buf == VK_NULL_HANDLE) {
        mk(im.normLut, 65536 * 4);
        im.lutValid = false;  // Force (re)build on first use.
    }

    VkDescriptorPoolSize dps[2]{};
    dps[0].type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    dps[0].descriptorCount = 512;
    dps[1].type = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    dps[1].descriptorCount = 8;
    VkDescriptorPoolCreateInfo dpi{};
    dpi.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    dpi.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
    dpi.maxSets = 96;
    dpi.poolSizeCount = 2;
    dpi.pPoolSizes = dps;
    vk::check(vkCreateDescriptorPool(im.device, &dpi, nullptr, &im.dpool), "raw dpool");

    Call tmp{};
    tmp.im = &im;
    im.sNeStats = allocSet(tmp, K_NE_STATS, {&im.inFloat, &im.blkM, &im.blkV});
    im.sNeFin = allocSet(tmp, K_NE_FIN, {&im.blkM, &im.blkV, &im.inFloat, &im.params});
    im.sDtHist = allocSet(tmp, K_NE_DT_HIST, {&im.inFloat, &im.dtHist});
    im.sDtFin = allocSet(tmp, K_NE_DT_FIN, {&im.dtHist, &im.params});
    im.sDlHist = allocSet(tmp, K_NE_DL_HIST, {&im.inFloat, &im.params, &im.dlHist});
    im.sDFin = allocSet(tmp, K_NE_D_FIN, {&im.dlHist, &im.params});
    im.sLut = allocSet(tmp, K_LUT_BUILD, {&im.params, &im.lutD, &im.lutX, &im.lutP});
    im.sLutFin = allocSet(tmp, K_LUT_FIN, {&im.lutD, &im.lutP});
    im.sGat = allocSet(tmp, K_GAT_FWD,
                       {&im.inFloat, &im.inGat, &im.ch0, &im.ch1, &im.ch2, &im.ch3, &im.params});
    im.sSigH = allocSet(tmp, K_SIGMA_HIST, {&im.inGat, &im.sigmaHist});
    im.sSigF = allocSet(tmp, K_SIGMA_FIN, {&im.sigmaHist, &im.params});
    im.sUnified = allocSet(tmp, K_UNIFIED, {&im.params});
    im.sNorm = allocSet(tmp, K_NORMALIZE,
                        {&im.inGat, &im.ch0, &im.ch1, &im.ch2, &im.ch3, &im.params});
    im.sDrRed = allocSet(tmp, K_DR_REDUCE, {&im.inGat, &im.inFloat, &im.params, &im.part});
    im.sDrFin = allocSet(tmp, K_DR_FIN, {&im.part, &im.params});
    im.sDrRred = allocSet(tmp, K_DR_RREDUCE, {&im.inGat, &im.inFloat, &im.params, &im.partR});
    im.sDrRfin = allocSet(tmp, K_DR_RFIN, {&im.partR, &im.params});
    im.sDsub = allocSet(tmp, K_DARK_SUB,
                        {&im.inGat, &im.ch0, &im.ch1, &im.ch2, &im.ch3, &im.params});
    im.sFwdL = allocSet(tmp, K_FWD_L, {&im.inGat, &im.lCs});
    im.sCex = allocSet(tmp, K_CHROMA_EX, {&im.inGat, &im.c1H, &im.c2H, &im.c3H});
    im.sP6 = allocSet(tmp, K_P6_FUSED, {&im.lCsDen, &im.lPixel, &im.lHDen});
    im.sLq = allocSet(tmp, K_BOX2_H16, {&im.lHDen, &im.lQ});
    im.sLe = allocSet(tmp, K_BOX2, {&im.lQ, &im.lE});
    im.sCq = allocSet(tmp, K_BOX2_3P, {&im.c1H, &im.c2H, &im.c3H, &im.cQ1, &im.cQ2, &im.cQ3});
    im.sCe = allocSet(tmp, K_BOX2_3P, {&im.cQ1, &im.cQ2, &im.cQ3, &im.cE1, &im.cE2, &im.cE3});
    im.sLoH = allocSet(tmp, K_LOESS_T_G16,
                       {&im.lHDen, &im.c1H, &im.c2H, &im.c3H, &im.clH1, &im.clH2, &im.clH3});
    im.sLoQ = allocSet(tmp, K_LOESS_T,
                       {&im.lQ, &im.cQ1, &im.cQ2, &im.cQ3, &im.clQ1, &im.clQ2, &im.clQ3});
    im.sLoE = allocSet(tmp, K_LOESS_T,
                       {&im.lE, &im.cE1, &im.cE2, &im.cE3, &im.clE1, &im.clE2, &im.clE3});
    im.sCropQ = allocSet(tmp, K_CROP_H16, {&im.lHDen, &im.lForQ});
    im.sK16Q2H = allocSet(tmp, K_K16,
                          {&im.clQ1, &im.clQ2, &im.clQ3, &im.lForQ, &im.cqupR1, &im.cqupR2,
                           &im.cqupR3});
    im.sPadQ3 = allocSet(tmp, K_PAD3,
                         {&im.cqupR1, &im.cqupR2, &im.cqupR3, &im.cqup1, &im.cqup2, &im.cqup3});
    im.sCropE = allocSet(tmp, K_CROP, {&im.lQ, &im.lForE});
    im.sK16E2Q = allocSet(tmp, K_K16,
                          {&im.clE1, &im.clE2, &im.clE3, &im.lForE, &im.ceqR1, &im.ceqR2,
                           &im.ceqR3});
    im.sPadE3 = allocSet(tmp, K_PAD3,
                         {&im.ceqR1, &im.ceqR2, &im.ceqR3, &im.ceq1, &im.ceq2, &im.ceq3});
    im.sK16EQ2H = allocSet(tmp, K_K16,
                           {&im.ceq1, &im.ceq2, &im.ceq3, &im.lForQ, &im.ceupR1, &im.ceupR2,
                            &im.ceupR3});
    im.sPadEU3 = allocSet(tmp, K_PAD3,
                          {&im.ceupR1, &im.ceupR2, &im.ceupR3, &im.ceup1, &im.ceup2, &im.ceup3});
    im.sSmooth = allocSet(tmp, K_SMOOTH,
                          {&im.c1H, &im.c2H, &im.c3H, &im.clH1, &im.clH2, &im.clH3, &im.cqup1,
                           &im.cqup2, &im.cqup3, &im.ceup1, &im.ceup2, &im.ceup3, &im.cden1,
                           &im.cden2, &im.cden3});
    im.sFinFused = allocSet(tmp, K_K16_INV_F,
                            {&im.cden1, &im.cden2, &im.cden3, &im.lPixel, &im.outFloat, &im.lutD,
                             &im.lutX, &im.lutP, &im.params});
    // NOTE: sP12 (pass12 L_cs -> L_cs_den) is selected per call (wht4 vs
    // classic) in process(); the norm/quant bridge image bindings are bound
    // per call in bindBridges(). K_FASTUP* selected the same way (P2).
    im.epochW = c.W;
    im.epochH = c.H;
}

// Builds + uploads the host-exact normalize LUT on (black, white) change.
// Plain float ops => bit-identical to the numpy oracle conversion (both
// IEEE correctly-rounded; all operands exactly representable).
void ensureNormLut(Call& c) {
    RawContext* im = c.im;
    if (im->lutValid && im->lutBlack == c.black && im->lutWhite == c.white) return;
    const float range = std::max(c.white - c.black, 1e-6f);
    float* lut = static_cast<float*>(im->stagingMap);
    for (uint32_t chunk = 0; chunk < 64; ++chunk) {
        for (uint32_t i = 0; i < 1024; ++i) {
            const uint32_t code = chunk * 1024 + i;
            float v = (static_cast<float>(code) - c.black) / range;
            lut[i] = v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v);
        }
        beginCb(c);
        VkBufferCopy cp{};
        cp.srcOffset = 0;
        cp.dstOffset = static_cast<VkDeviceSize>(chunk) * 4096;
        cp.size = 4096;
        vkCmdCopyBuffer(im->cmd, im->staging.buf, im->normLut.buf, 1, &cp);
        submitWait(c);
    }
    im->lutBlack = c.black;
    im->lutWhite = c.white;
    im->lutValid = true;
}

// Binds the two bridge sets to the caller views (per call: views vary).
// Prior bridge sets are freed first (pool carries FREE_DESCRIPTOR_SET_BIT;
// all other sets are epoch-bound and untouched).
void bindBridges(Call& c, VkImageView srcView, VkImageView dstView) {
    RawContext* im = c.im;
    if (im->sBrNorm != VK_NULL_HANDLE) vkFreeDescriptorSets(im->device, im->dpool, 1, &im->sBrNorm);
    if (im->sBrQuant != VK_NULL_HANDLE) vkFreeDescriptorSets(im->device, im->dpool, 1, &im->sBrQuant);
    im->sBrNorm = VK_NULL_HANDLE;
    im->sBrQuant = VK_NULL_HANDLE;
    auto bindImageSet = [&](Kernel kid, VkImageView view, uint32_t imageBinding,
                            vk::Buffer& buf, uint32_t bufBinding) {
        const BuiltKernel& k = im->kernels[static_cast<size_t>(kid)];
        checkImpl(k.present, "raw bridge kernel not built");
        VkDescriptorSetAllocateInfo ai{};
        ai.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
        ai.descriptorPool = im->dpool;
        ai.descriptorSetCount = 1;
        ai.pSetLayouts = &k.dsl;
        VkDescriptorSet ds = VK_NULL_HANDLE;
        vk::check(vkAllocateDescriptorSets(im->device, &ai, &ds), "raw alloc bridge set");
        VkDescriptorImageInfo ii{};
        ii.imageView = view;
        ii.imageLayout = VK_IMAGE_LAYOUT_GENERAL;
        VkDescriptorBufferInfo bi{buf.buf, 0, VK_WHOLE_SIZE};
        VkWriteDescriptorSet w[2]{};
        w[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        w[0].dstSet = ds;
        w[0].dstBinding = imageBinding;
        w[0].descriptorCount = 1;
        w[0].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
        w[0].pImageInfo = &ii;
        w[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        w[1].dstSet = ds;
        w[1].dstBinding = bufBinding;
        w[1].descriptorCount = 1;
        w[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        w[1].pBufferInfo = &bi;
        // Binding order in the write array is irrelevant; dstBinding routes.
        if (imageBinding == 0) {
            vkUpdateDescriptorSets(im->device, 2, w, 0, nullptr);
            im->sBrNorm = ds;
        } else {
            // Quant layout is (buffer=0, image=1): swap write order target.
            VkWriteDescriptorSet wq[2] = {w[1], w[0]};
            wq[0].dstBinding = 0;
            wq[1].dstBinding = 1;
            vkUpdateDescriptorSets(im->device, 2, wq, 0, nullptr);
            im->sBrQuant = ds;
        }
    };
    // Norm layout is (image=0, lut=1, out=2): dedicated 3-write path.
    {
        const BuiltKernel& k = im->kernels[static_cast<size_t>(K_BR_NORM)];
        checkImpl(k.present, "raw bridge kernel not built");
        VkDescriptorSetAllocateInfo ai{};
        ai.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
        ai.descriptorPool = im->dpool;
        ai.descriptorSetCount = 1;
        ai.pSetLayouts = &k.dsl;
        VkDescriptorSet ds = VK_NULL_HANDLE;
        vk::check(vkAllocateDescriptorSets(im->device, &ai, &ds), "raw alloc norm set");
        VkDescriptorImageInfo ii{};
        ii.imageView = srcView;
        ii.imageLayout = VK_IMAGE_LAYOUT_GENERAL;
        VkDescriptorBufferInfo biLut{im->normLut.buf, 0, VK_WHOLE_SIZE};
        VkDescriptorBufferInfo biOut{im->inFloat.buf, 0, VK_WHOLE_SIZE};
        VkWriteDescriptorSet w[3]{};
        w[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        w[0].dstSet = ds;
        w[0].dstBinding = 0;
        w[0].descriptorCount = 1;
        w[0].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
        w[0].pImageInfo = &ii;
        w[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        w[1].dstSet = ds;
        w[1].dstBinding = 1;
        w[1].descriptorCount = 1;
        w[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        w[1].pBufferInfo = &biLut;
        w[2].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        w[2].dstSet = ds;
        w[2].dstBinding = 2;
        w[2].descriptorCount = 1;
        w[2].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        w[2].pBufferInfo = &biOut;
        vkUpdateDescriptorSets(im->device, 3, w, 0, nullptr);
        im->sBrNorm = ds;
    }
    bindImageSet(K_BR_QUANT, dstView, 1, im->outFloat, 0);
}

}  // namespace

void GaloshRawPipeline::process(VkQueue queue, std::mutex& queueMutex, VkImageView srcView,
                                VkImageView dstView, uint32_t width, uint32_t height,
                                const GaloshRawParams& params, VkQueryPool timingPool) {
    if (params.mode == GaloshRawMode::Off) return;  // Safety net; caller skips.
    if (params.lumaStrength <= 0.0f && params.mode == GaloshRawMode::Full) return;  // CPU-identical.
    if (params.cfa != GaloshCfa::Rggb) throw std::invalid_argument("galosh: CFA phase (P1b)");
    if (width < 64 || height < 64 || (width & 1u) || (height & 1u) || srcView == VK_NULL_HANDLE ||
        dstView == VK_NULL_HANDLE || queue == VK_NULL_HANDLE) {
        throw std::invalid_argument("galosh: bad geometry/views (even, >=64)");
    }
    RawContext& im = impl_->ctx;
    std::lock_guard<std::mutex> lock(queueMutex);

    Call c{};
    c.im = &im;
    c.queue = queue;
    c.timing = timingPool;
    c.W = width;
    c.H = height;
    c.hw = width / 2u;
    c.hh = height / 2u;
    c.cqW = c.hw / 2u;
    c.cqH = c.hh / 2u;
    c.ceW = c.cqW / 2u;
    c.ceH = c.cqH / 2u;
    c.kqW = 2 * c.cqW;
    c.kqH = 2 * c.cqH;
    c.keW = 2 * c.ceW;
    c.keH = 2 * c.ceH;
    c.npix = static_cast<uint64_t>(width) * height;
    c.chsize = static_cast<uint64_t>(c.hw) * c.hh;
    const uint32_t neBx = c.hw / 8u, neBy = c.hh / 8u;
    c.nePerCh = neBx * neBy;
    c.neTotal = static_cast<uint64_t>(4) * c.nePerCh;
    c.hwA = static_cast<int>((width + 1) / 2);
    c.hhA = static_cast<int>((height + 1) / 2);
    c.hw3 = (c.hwA + 2) / 3;
    c.hh3 = (c.hhA + 2) / 3;
    c.strength = params.strength;
    c.luma = params.lumaStrength;
    c.chroma = params.chromaStrength;
    c.alphaExt = params.alpha;
    c.sigExt = params.sigmaSq;
    c.extModel = params.alpha > 0.0f && params.sigmaSq > 0.0f;
    c.chromaOnly = params.mode == GaloshRawMode::ChromaOnly;
    c.wht = (params.whtBlock == 4) ? 4 : 8;
    c.fastUp = params.upsampleFast;
    c.black = params.black;
    c.white = params.white;

    ensureResources(im, c);
    bindBridges(c, srcView, dstView);
    ensureNormLut(c);
    const Kernel kidP12 = (c.wht == 4) ? K_PASS12_W4 : K_PASS12;
    // Per-call variant selection (wht4 vs classic): free the previous set
    // first — the pool carries FREE_DESCRIPTOR_SET_BIT. Without this one
    // descriptor set leaks per enabled shot until OUT_OF_POOL_MEMORY.
    if (im.sP12 != VK_NULL_HANDLE) {
        vkFreeDescriptorSets(im.device, im.dpool, 1, &im.sP12);
        im.sP12 = VK_NULL_HANDLE;
    }
    im.sP12 = allocSet(c, kidP12, {&im.lCs, &im.lCsDen});
    // NOTE: fast-upsample selection (K_FASTUP family) is P2; the quality
    // jinc path (K_K16/K_K16_INV_F) serves all P1a calls.

    PcW pc[8];
#define PCI(k, v) pc[k].i = (v)
#define PCF(k, v) pc[k].f = (v)

    // ---- Initial params + normalize bridge + SEG A (P0 fit), one submit ----
    float hParams[kParamsSize] = {0};
    hParams[kPLumaStr] = c.strength * c.luma;
    hParams[kPChromaStr] = c.strength * c.chroma;
    beginCb(c);
    writeTimestamp(c, kQueryBegin, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT);
    uploadParams(c, hParams);
    PCI(0, static_cast<int>(width));
    PCI(1, static_cast<int>(height));
    dispatchKid(c, K_BR_NORM, im.sBrNorm, pc, 2, aup(width, 16), aup(height, 16));
    PCI(0, static_cast<int>(width));
    PCI(1, static_cast<int>(height));
    PCI(2, static_cast<int>(neBx));
    PCI(3, static_cast<int>(neBy));
    PCI(4, static_cast<int>(c.nePerCh));
    dispatchKid(c, K_NE_STATS, im.sNeStats, pc, 5,
                aup(static_cast<uint32_t>(c.neTotal), 64), 1);
    PCI(0, static_cast<int>(width));
    PCI(1, static_cast<int>(height));
    PCI(2, static_cast<int>(c.neTotal));
    dispatchKid(c, K_NE_FIN, im.sNeFin, pc, 3, 1, 1);
    fillBuf(c, im.dtHist, 0, 4096 * 4);
    PCI(0, static_cast<int>(width));
    PCI(1, static_cast<int>(height));
    dispatchKid(c, K_NE_DT_HIST, im.sDtHist, pc, 2, aup(static_cast<uint32_t>(c.hw3), 16),
                aup(static_cast<uint32_t>(c.hh3), 16));
    PCI(0, kPDarkThresh);
    dispatchKid(c, K_NE_DT_FIN, im.sDtFin, pc, 1, 1, 1);
    fillBuf(c, im.dlHist, 0, 4096 * 4);
    PCI(0, static_cast<int>(width));
    PCI(1, static_cast<int>(height));
    PCI(2, kPDarkThresh);
    dispatchKid(c, K_NE_DL_HIST, im.sDlHist, pc, 3, aup(static_cast<uint32_t>(c.hwA), 16),
                aup(static_cast<uint32_t>(c.hhA), 16));
    PCI(0, kPDarkThresh);
    dispatchKid(c, K_NE_D_FIN, im.sDFin, pc, 1, 1, 1);
    submitWait(c);

    // ---- HOST SYNC #1: read back the fit ----
    float est[kParamsSize] = {0};
    downloadParams(c, est);
    float alpha = est[kPAlpha], sigmaSq = est[kPSigmaSq];
    if (c.extModel) {
        alpha = c.alphaExt;
        sigmaSq = c.sigExt;
    }
    im.lastAlpha = alpha;
    im.lastSigmaSq = sigmaSq;

    // ---- SEG BC: GAT/LUT/sigma/unified/normalize/IRLS/dark-sub/FWD/extract ----
    beginCb(c);
    if (c.extModel) updateTrio(c, alpha, sigmaSq);
    PCI(0, static_cast<int>(width));
    PCI(1, static_cast<int>(height));
    dispatchKid(c, K_GAT_FWD, im.sGat, pc, 2, aup(width, 16), aup(height, 16));
    vk::debugDumpPhase(c, im.staging, im.stagingMap, im.inGat, static_cast<size_t>(c.npix), false, "p1b_gat");
    dispatchKid(c, K_LUT_BUILD, im.sLut, nullptr, 0, kGatLutSize / 256, 1);
    dispatchKid(c, K_LUT_FIN, im.sLutFin, nullptr, 0, 1, 1);
    fillBuf(c, im.sigmaHist, 0, 4 * 4096 * 4);
    const int wgPerCh = std::clamp(static_cast<int>(c.hh / 512), 1, 8);
    PCI(0, static_cast<int>(width));
    PCI(1, static_cast<int>(height));
    PCI(2, wgPerCh);
    dispatchKid(c, K_SIGMA_HIST, im.sSigH, pc, 3, static_cast<uint32_t>(4 * wgPerCh), 1);
    dispatchKid(c, K_SIGMA_FIN, im.sSigF, nullptr, 0, 1, 1);
    dispatchKid(c, K_UNIFIED, im.sUnified, nullptr, 0, 1, 1);
    PCI(0, static_cast<int>(width));
    PCI(1, static_cast<int>(height));
    dispatchKid(c, K_NORMALIZE, im.sNorm, pc, 2, aup(width, 16), aup(height, 16));
    vk::debugDumpPhase(c, im.staging, im.stagingMap, im.inGat, static_cast<size_t>(c.npix), false, "p1d_norm");
    const float aForS = std::max(alpha, 1e-12f);
    const float sInit = sigmaSq / aForS;
    const float sMin = 0.05f * sInit, sMax = 50.0f * sInit;
    vkCmdUpdateBuffer(im.cmd, im.params.buf, kPScale * 4, 4, &sInit);
    vk::computeBarrier(im.cmd);
    for (int it = 0; it <= 2; ++it) {
        PCI(0, static_cast<int>(width));
        PCI(1, static_cast<int>(height));
        dispatchKid(c, K_DR_REDUCE, im.sDrRed, pc, 2, kNReduceWg, 1);
        PCI(0, kNReduceWg);
        dispatchKid(c, K_DR_FIN, im.sDrFin, pc, 1, 1, 1);
        if (it == 2) break;
        PCI(0, static_cast<int>(width));
        PCI(1, static_cast<int>(height));
        dispatchKid(c, K_DR_RREDUCE, im.sDrRred, pc, 2, kNReduceWg, 1);
        PCI(0, kNReduceWg);
        PCF(1, sMin);
        PCF(2, sMax);
        dispatchKid(c, K_DR_RFIN, im.sDrRfin, pc, 3, 1, 1);
    }
    PCI(0, static_cast<int>(width));
    PCI(1, static_cast<int>(height));
    dispatchKid(c, K_DARK_SUB, im.sDsub, pc, 2, aup(width, 16), aup(height, 16));
    vk::debugDumpPhase(c, im.staging, im.stagingMap, im.inGat, static_cast<size_t>(c.npix), false, "p2b_darksub");
    PCI(0, static_cast<int>(width));
    PCI(1, static_cast<int>(height));
    dispatchKid(c, K_FWD_L, im.sFwdL, pc, 2, aup(width, 16), aup(height, 16));
    PCI(0, static_cast<int>(width));
    PCI(1, static_cast<int>(height));
    PCI(2, static_cast<int>(c.hw));
    PCI(3, static_cast<int>(c.hh));
    dispatchKid(c, K_CHROMA_EX, im.sCex, pc, 4, aup(c.hw, 16), aup(c.hh, 16));
    submitWait(c);

    // ---- pass12 (banded) or chroma-only lane bypass ----
    if (c.chromaOnly) {
        beginCb(c);
        VkBufferCopy cpy{};
        cpy.srcOffset = 0;
        cpy.dstOffset = 0;
        cpy.size = static_cast<uint64_t>(width) * height * 2;  // full_h16.
        vkCmdCopyBuffer(im.cmd, im.lCs.buf, im.lCsDen.buf, 1, &cpy);
        vk::computeBarrier(im.cmd);
        submitWait(c);
    } else {
        PCI(0, static_cast<int>(width));
        PCI(1, static_cast<int>(height));
        PCF(2, c.strength * c.luma);
        PCI(3, 1);
        pass12Banded(c, kidP12, im.sP12, pc, aup(width, kO32Tile), aup(height, kO32Tile));
    }

    // ---- P6 + SEG D pyramid + fused final + quant bridge ----
    beginCb(c);
    PCI(0, static_cast<int>(width));
    PCI(1, static_cast<int>(height));
    PCI(2, static_cast<int>(c.hw));
    dispatchKid(c, K_P6_FUSED, im.sP6, pc, 3, aup(width, 16), aup(height, 16));
    PCI(0, static_cast<int>(c.hw));
    PCI(1, static_cast<int>(c.hh));
    dispatchKid(c, K_BOX2_H16, im.sLq, pc, 2, aup(c.cqW, 16), aup(c.cqH, 16));
    PCI(0, static_cast<int>(c.cqW));
    PCI(1, static_cast<int>(c.cqH));
    dispatchKid(c, K_BOX2, im.sLe, pc, 2, aup(c.ceW, 16), aup(c.ceH, 16));
    PCI(0, static_cast<int>(c.hw));
    PCI(1, static_cast<int>(c.hh));
    dispatchKid(c, K_BOX2_3P, im.sCq, pc, 2, aup(c.cqW, 16), aup(c.cqH, 16));
    PCI(0, static_cast<int>(c.cqW));
    PCI(1, static_cast<int>(c.cqH));
    dispatchKid(c, K_BOX2_3P, im.sCe, pc, 2, aup(c.ceW, 16), aup(c.ceH, 16));
    PCI(0, static_cast<int>(c.hw));
    PCI(1, static_cast<int>(c.hh));
    PCF(2, 1.0f);
    dispatchKid(c, K_LOESS_T_G16, im.sLoH, pc, 3, aup(c.hw, 24), aup(c.hh, 24));
    PCI(0, static_cast<int>(c.cqW));
    PCI(1, static_cast<int>(c.cqH));
    PCF(2, 1.0f);
    dispatchKid(c, K_LOESS_T, im.sLoQ, pc, 3, aup(c.cqW, 24), aup(c.cqH, 24));
    PCI(0, static_cast<int>(c.ceW));
    PCI(1, static_cast<int>(c.ceH));
    PCF(2, 1.0f);
    dispatchKid(c, K_LOESS_T, im.sLoE, pc, 3, aup(c.ceW, 24), aup(c.ceH, 24));
    PCI(0, static_cast<int>(c.hw));
    PCI(1, static_cast<int>(c.hh));
    PCI(2, static_cast<int>(c.kqW));
    PCI(3, static_cast<int>(c.kqH));
    dispatchKid(c, K_CROP_H16, im.sCropQ, pc, 4, aup(c.kqW, 16), aup(c.kqH, 16));
    PCI(0, static_cast<int>(c.cqW));
    PCI(1, static_cast<int>(c.cqH));
    PCF(2, 1.5f);
    dispatchKid(c, K_K16, im.sK16Q2H, pc, 3, aup(c.kqW, 16), aup(c.kqH, 16));
    PCI(0, static_cast<int>(c.kqW));
    PCI(1, static_cast<int>(c.kqH));
    PCI(2, static_cast<int>(c.hw));
    PCI(3, static_cast<int>(c.hh));
    dispatchKid(c, K_PAD3, im.sPadQ3, pc, 4, aup(c.hw, 16), aup(c.hh, 16));
    PCI(0, static_cast<int>(c.cqW));
    PCI(1, static_cast<int>(c.cqH));
    PCI(2, static_cast<int>(c.keW));
    PCI(3, static_cast<int>(c.keH));
    dispatchKid(c, K_CROP, im.sCropE, pc, 4, aup(c.keW, 16), aup(c.keH, 16));
    PCI(0, static_cast<int>(c.ceW));
    PCI(1, static_cast<int>(c.ceH));
    PCF(2, 1.5f);
    dispatchKid(c, K_K16, im.sK16E2Q, pc, 3, aup(c.keW, 16), aup(c.keH, 16));
    PCI(0, static_cast<int>(c.keW));
    PCI(1, static_cast<int>(c.keH));
    PCI(2, static_cast<int>(c.cqW));
    PCI(3, static_cast<int>(c.cqH));
    dispatchKid(c, K_PAD3, im.sPadE3, pc, 4, aup(c.cqW, 16), aup(c.cqH, 16));
    PCI(0, static_cast<int>(c.cqW));
    PCI(1, static_cast<int>(c.cqH));
    PCF(2, 1.5f);
    dispatchKid(c, K_K16, im.sK16EQ2H, pc, 3, aup(c.kqW, 16), aup(c.kqH, 16));
    PCI(0, static_cast<int>(c.kqW));
    PCI(1, static_cast<int>(c.kqH));
    PCI(2, static_cast<int>(c.hw));
    PCI(3, static_cast<int>(c.hh));
    dispatchKid(c, K_PAD3, im.sPadEU3, pc, 4, aup(c.hw, 16), aup(c.hh, 16));
    PCI(0, static_cast<int>(c.hw));
    PCI(1, static_cast<int>(c.hh));
    PCF(2, c.strength * c.chroma);
    dispatchKid(c, K_SMOOTH, im.sSmooth, pc, 3, aup(c.hw, 16), aup(c.hh, 16));
    PCI(0, static_cast<int>(c.hw));
    PCI(1, static_cast<int>(c.hh));
    PCF(2, 1.5f);
    dispatchKid(c, K_K16_INV_F, im.sFinFused, pc, 3, aup(width, 16), aup(height, 16));
    PCI(0, static_cast<int>(width));
    PCI(1, static_cast<int>(height));
    PCF(2, c.black);
    PCF(3, c.white);
    dispatchKid(c, K_BR_QUANT, im.sBrQuant, pc, 4, aup(width, 16), aup(height, 16));
    writeTimestamp(c, kQueryEnd, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT);
    submitWait(c);
    // [PORT] phase dumps for bisection vs upstream GALOSH_DUMP_DIR (names match).
    vk::debugDumpPhase(c, im.staging, im.stagingMap, im.inFloat, static_cast<size_t>(c.npix), false, "p1_norm");
    vk::debugDumpPhase(c, im.staging, im.stagingMap, im.inGat, static_cast<size_t>(c.npix), false, "p2_in_gat");
    vk::debugDumpPhase(c, im.staging, im.stagingMap, im.lCs, static_cast<size_t>(c.npix), true, "p3_L_cs");
    vk::debugDumpPhase(c, im.staging, im.stagingMap, im.c1H, static_cast<size_t>(c.chsize), false, "p4_C1_h");
    vk::debugDumpPhase(c, im.staging, im.stagingMap, im.lCsDen, static_cast<size_t>(c.npix), true, "p5_L_cs_den");
    vk::debugDumpPhase(c, im.staging, im.stagingMap, im.lPixel, static_cast<size_t>(c.npix), true, "p6_L_pixel");
    vk::debugDumpPhase(c, im.staging, im.stagingMap, im.lHDen, static_cast<size_t>(c.chsize), true, "p6_L_h_den");
    vk::debugDumpPhase(c, im.staging, im.stagingMap, im.cden1, static_cast<size_t>(c.chsize), true, "p8_C1_h_den");
    vk::debugDumpPhase(c, im.staging, im.stagingMap, im.lQ, static_cast<size_t>(c.cqW) * c.cqH, false, "sg_L_q");
    vk::debugDumpPhase(c, im.staging, im.stagingMap, im.lE, static_cast<size_t>(c.ceW) * c.ceH, false, "sg_L_e");
    vk::debugDumpPhase(c, im.staging, im.stagingMap, im.cQ1, static_cast<size_t>(c.cqW) * c.cqH, false, "sg_C_q1");
    vk::debugDumpPhase(c, im.staging, im.stagingMap, im.cE1, static_cast<size_t>(c.ceW) * c.ceH, false, "sg_C_e1");
    vk::debugDumpPhase(c, im.staging, im.stagingMap, im.lForQ, static_cast<size_t>(c.kqW) * c.kqH, false, "sg_L_for_q");
    vk::debugDumpPhase(c, im.staging, im.stagingMap, im.lForE, static_cast<size_t>(c.keW) * c.keH, false, "sg_L_for_e");
    vk::debugDumpPhase(c, im.staging, im.stagingMap, im.ceqR1, static_cast<size_t>(c.keW) * c.keH, false, "sg_Ceq_r1");
    vk::debugDumpPhase(c, im.staging, im.stagingMap, im.ceq1, static_cast<size_t>(c.cqW) * c.cqH, false, "sg_Ceq1");
    vk::debugDumpPhase(c, im.staging, im.stagingMap, im.ceupR1, static_cast<size_t>(c.kqW) * c.kqH, false, "sg_Ceup_r1");
    vk::debugDumpPhase(c, im.staging, im.stagingMap, im.clQ1, static_cast<size_t>(c.cqW) * c.cqH, false, "sg_Cl_q1");
    vk::debugDumpPhase(c, im.staging, im.stagingMap, im.clE1, static_cast<size_t>(c.ceW) * c.ceH, false, "sg_Cl_e1");
    vk::debugDumpPhase(c, im.staging, im.stagingMap, im.outFloat, static_cast<size_t>(c.npix), false, "sg_out");
    vk::debugDumpPhase(c, im.staging, im.stagingMap, im.clH1, static_cast<size_t>(c.chsize), false, "p7_C1_loess_h");
    vk::debugDumpPhase(c, im.staging, im.stagingMap, im.cqup1, static_cast<size_t>(c.chsize), false, "p7_C1_q_up");
    vk::debugDumpPhase(c, im.staging, im.stagingMap, im.ceup1, static_cast<size_t>(c.chsize), false, "p7_C1_e_up");
#undef PCI
#undef PCF
}

float GaloshRawPipeline::lastAlpha() const noexcept { return impl_ ? impl_->ctx.lastAlpha : 0.0f; }

float GaloshRawPipeline::lastSigmaSq() const noexcept { return impl_ ? impl_->ctx.lastSigmaSq : 0.0f; }

}  // namespace galosh
