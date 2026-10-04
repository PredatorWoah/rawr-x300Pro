#include <algorithm>
#include <chrono>
#include <cmath>
#include <iostream>
#include <numeric>
#include <string>
#include <thread>
#include <vector>

#include "../../vk_common/testing/vk_test_common.hpp"
#include "raw_preview/RawPreview.hpp"
using namespace vktest;
struct Slot {
    VkCommandPool pool{};
    VkCommandBuffer cmd{};
    VkFence f{};
    VkQueryPool qp{};
    Img raw{}, out{};
};
static std::vector<uint16_t> pattern(uint32_t W, uint32_t H, bool stress) {
    std::vector<uint16_t> r(size_t(W) * H);
    for (uint32_t y = 0; y < H; y++)
        for (uint32_t x = 0; x < W; x++) {
            float fx = float(x) / W, fy = float(y) / H;
            float v = stress ? (.55f + 1.15f * fx + .35f * std::sin(fy * 20.f)) : (.15f + .65f * fx);
            if (stress && fx > .60f && fy > .18f && fy < .82f) v += .65f;
            v = std::max(0.f, std::min(1.f, v));
            r[size_t(y) * W + x] = (uint16_t)std::lround(64 + v * (4095 - 64));
        }
    return r;
}
static void upload(Ctx& c, VkCommandBuffer cmd, Img& im, const std::vector<uint16_t>& raw, uint32_t W, uint32_t H) {
    VkDeviceSize n = raw.size() * 2;
    Buf b = mkBuf(c.pd, c.dev, n, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                  VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    void* m;
    ck(vkMapMemory(c.dev, b.m, 0, n, 0, &m), "map");
    std::memcpy(m, raw.data(), n);
    vkUnmapMemory(c.dev, b.m);
    VkCommandBufferBeginInfo bi{};
    bi.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    vkBeginCommandBuffer(cmd, &bi);
    VkImageMemoryBarrier a{};
    a.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    a.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    a.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    a.srcQueueFamilyIndex = a.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    a.image = im.i;
    a.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    a.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0,
                         nullptr, 1, &a);
    VkBufferImageCopy bc{};
    bc.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
    bc.imageExtent = {W, H, 1};
    vkCmdCopyBufferToImage(cmd, b.b, im.i, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &bc);
    VkImageMemoryBarrier g{};
    g.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    g.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    g.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    g.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    g.newLayout = VK_IMAGE_LAYOUT_GENERAL;
    g.srcQueueFamilyIndex = g.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    g.image = im.i;
    g.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 0, nullptr, 0,
                         nullptr, 1, &g);
    vkEndCommandBuffer(cmd);
    VkSubmitInfo si{};
    si.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    si.commandBufferCount = 1;
    si.pCommandBuffers = &cmd;
    vkQueueSubmit(c.q, 1, &si, VK_NULL_HANDLE);
    vkQueueWaitIdle(c.q);
    vkResetCommandBuffer(cmd, 0);
    delBuf(c.dev, b);
}
static double pct(const std::vector<double>& v, double p) {
    return v[std::min(v.size() - 1, size_t(p * (v.size() - 1)))];
}
static void run(Ctx& c, const std::string& shader, uint32_t W, uint32_t H, uint32_t frames, double fps, bool stress) {
    Slot s[3];
    for (int k = 0; k < 3; k++) {
        VkCommandPoolCreateInfo pi{};
        pi.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
        pi.queueFamilyIndex = c.qf;
        pi.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
        ck(vkCreateCommandPool(c.dev, &pi, nullptr, &s[k].pool), "pool");
        VkCommandBufferAllocateInfo ai{};
        ai.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        ai.commandPool = s[k].pool;
        ai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        ai.commandBufferCount = 1;
        ck(vkAllocateCommandBuffers(c.dev, &ai, &s[k].cmd), "cmd");
        VkFenceCreateInfo fi{};
        fi.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
        fi.flags = VK_FENCE_CREATE_SIGNALED_BIT;
        ck(vkCreateFence(c.dev, &fi, nullptr, &s[k].f), "fence");
        VkQueryPoolCreateInfo qi{};
        qi.sType = VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO;
        qi.queryType = VK_QUERY_TYPE_TIMESTAMP;
        qi.queryCount = 2;
        ck(vkCreateQueryPool(c.dev, &qi, nullptr, &s[k].qp), "qp");
        s[k].raw =
            mkImg(c.pd, c.dev, W, H, VK_FORMAT_R16_UINT, VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_STORAGE_BIT);
        s[k].out = mkImg(c.pd, c.dev, W / 2, H / 2, VK_FORMAT_R16G16B16A16_SFLOAT, VK_IMAGE_USAGE_STORAGE_BIT);
        auto raw = pattern(W, H, stress);
        upload(c, s[k].cmd, s[k].raw, raw, W, H);
        VkCommandBufferBeginInfo bi{};
        bi.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        vkBeginCommandBuffer(s[k].cmd, &bi);
        VkImageMemoryBarrier og{};
        og.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        og.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        og.newLayout = VK_IMAGE_LAYOUT_GENERAL;
        og.srcQueueFamilyIndex = og.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        og.image = s[k].out.i;
        og.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
        og.dstAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
        vkCmdPipelineBarrier(s[k].cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 0,
                             nullptr, 0, nullptr, 1, &og);
        vkEndCommandBuffer(s[k].cmd);
        VkSubmitInfo si{};
        si.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        si.commandBufferCount = 1;
        si.pCommandBuffers = &s[k].cmd;
        vkQueueSubmit(c.q, 1, &si, VK_NULL_HANDLE);
        vkQueueWaitIdle(c.q);
        vkResetCommandBuffer(s[k].cmd, 0);
    }
    raw_preview::RawPreviewCreateInfo ci{};
    ci.physicalDevice = c.pd;
    ci.device = c.dev;
    ci.shaderPath = shader;
    raw_preview::RawPreview pr(ci);
    raw_preview::RawFrameParameters rp{};
    rp.blackLevel[0] = rp.blackLevel[1] = rp.blackLevel[2] = rp.blackLevel[3] = 64;
    rp.whiteLevel = 4095;
    rp.clipThreshold = .995f;
    rp.edgeStrength = 8;
    rp.chromaBlend = .2f;
    const uint32_t warm = 60, total = warm + frames;
    std::vector<uint64_t> pf(3, UINT64_MAX);
    std::vector<double> ms;
    ms.reserve(frames);
    auto harvest = [&](int k) {
        if (pf[k] == UINT64_MAX) return;
        vkWaitForFences(c.dev, 1, &s[k].f, VK_TRUE, UINT64_MAX);
        uint64_t t[2];
        vkGetQueryPoolResults(c.dev, s[k].qp, 0, 2, sizeof(t), t, sizeof(uint64_t),
                              VK_QUERY_RESULT_64_BIT | VK_QUERY_RESULT_WAIT_BIT);
        if (pf[k] >= warm) ms.push_back(double(t[1] - t[0]) * c.prop.limits.timestampPeriod / 1e6);
        pf[k] = UINT64_MAX;
    };
    auto start = std::chrono::steady_clock::now();
    auto per = std::chrono::duration<double>(1.0 / fps);
    for (uint64_t f = 0; f < total; f++) {
        int k = f % 3;
        harvest(k);
        vkResetFences(c.dev, 1, &s[k].f);
        vkResetCommandBuffer(s[k].cmd, 0);
        VkCommandBufferBeginInfo bi{};
        bi.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        vkBeginCommandBuffer(s[k].cmd, &bi);
        vkCmdResetQueryPool(s[k].cmd, s[k].qp, 0, 2);
        vkCmdWriteTimestamp(s[k].cmd, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, s[k].qp, 0);
        raw_preview::RawPreviewRecordInfo ri{};
        ri.commandBuffer = s[k].cmd;
        ri.inputRawR16UintView = s[k].raw.v;
        ri.outputLinearRgba16fView = s[k].out.v;
        ri.width = W;
        ri.height = H;
        ri.parameters = rp;
        pr.record(ri);
        vkCmdWriteTimestamp(s[k].cmd, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, s[k].qp, 1);
        vkEndCommandBuffer(s[k].cmd);
        VkSubmitInfo si{};
        si.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        si.commandBufferCount = 1;
        si.pCommandBuffers = &s[k].cmd;
        vkQueueSubmit(c.q, 1, &si, s[k].f);
        pf[k] = f;
        std::this_thread::sleep_until(
            start + std::chrono::duration_cast<std::chrono::steady_clock::duration>(per * double(f + 1)));
    }
    for (int k = 0; k < 3; k++) harvest(k);
    std::sort(ms.begin(), ms.end());
    double mean = std::accumulate(ms.begin(), ms.end(), 0.0) / ms.size();
    std::cout << (stress ? "HIGHLIGHT-STRESS" : "NORMAL") << " " << W << "x" << H << "  best " << ms.front() << " med "
              << pct(ms, .5) << " mean " << mean << " p95 " << pct(ms, .95) << " p99 " << pct(ms, .99) << " worst "
              << ms.back() << " ms  " << (pct(ms, .5) <= 1.0 ? "PASS" : "FAIL") << "\n";
    std::cout << "RAW_PREVIEW_MS stress=" << (stress ? 1 : 0) << " width=" << W << " height=" << H
              << " best=" << ms.front() << " median=" << pct(ms, .5) << " mean=" << mean << " p95=" << pct(ms, .95)
              << " p99=" << pct(ms, .99) << " worst=" << ms.back() << "\n";
    for (int k = 0; k < 3; k++) {
        vkDestroyQueryPool(c.dev, s[k].qp, nullptr);
        vkDestroyFence(c.dev, s[k].f, nullptr);
        delImg(c.dev, s[k].out);
        delImg(c.dev, s[k].raw);
        vkDestroyCommandPool(c.dev, s[k].pool, nullptr);
    }
}
int main(int argc, char** argv) {
    std::cout.setf(std::ios::unitbuf);
    std::string shader = "./raw_preview.comp.spv";
    uint32_t frames = 600, W = 4080, H = 3064;
    double fps = 60;
    int stressMode = -1;
    for (int i = 1; i < argc; i++) {
        std::string a = argv[i];
        if (a == "--shader" && i + 1 < argc)
            shader = argv[++i];
        else if (a == "--frames" && i + 1 < argc)
            frames = std::stoul(argv[++i]);
        else if (a == "--fps" && i + 1 < argc)
            fps = std::stod(argv[++i]);
        else if (a == "--width" && i + 1 < argc)
            W = std::stoul(argv[++i]);
        else if (a == "--height" && i + 1 < argc)
            H = std::stoul(argv[++i]);
        else if (a == "--stress-only")
            stressMode = 1;
        else if (a == "--normal-only")
            stressMode = 0;
    }
    try {
        Ctx c = ctx();
        std::cout << "============================================================\nRAW_PREVIEW 1.2.0 RGB-ONLY "
                     "BENCHMARK\n============================================================\nGPU                  : "
                  << c.prop.deviceName << "\nWorkgroup            : 8 x 8\nFrames               : " << frames
                  << " measured + 60 warmup / case\nCadence              : " << fps << " fps paced\n";
        if (stressMode <= 0) run(c, shader, W, H, frames, fps, false);
        if (stressMode != 0) run(c, shader, W, H, frames, fps, true);
        std::cout << "THREE_FRAMES_IN_FLIGHT_PASS slots=3\n";
        delCtx(c);
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "ERROR: " << e.what() << "\n";
        return 2;
    }
}
