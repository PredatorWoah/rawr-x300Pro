// galosh_validate: MoltenVK data runner for the native RAW port.
// Uploads float Bayer (.bin [0,1]) as R16_UINT codes, runs
// GaloshRawPipeline::process(), downloads codes to <out>_u16.bin.
// The driver (tools/validate_native_raw.py) converts back with the same
// black/white and checks parity vs the CPU oracle (>= 69 dB) plus the
// chroma-only self-consistency gates.
//
// Usage: galosh_validate --spv-dir DIR --in rawfloat.bin --out PREFIX
//   --w W --h H --black B --white Wh [--strength S] [--luma L] [--chroma C]
//   [--mode full|chroma-only]

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <fstream>
#include <iostream>
#include <mutex>
#include <stdexcept>
#include <string>
#include <vector>
#include <vulkan/vulkan.h>

#include "galosh/GaloshRawPipeline.hpp"
#include "galosh_vk_test.h"

namespace {

struct Args {
    std::string spvDir, inPath, outPrefix;
    uint32_t w = 0, h = 0;
    float black = 1024.0f, white = 8712.0f;
    float strength = 1.0f, luma = 1.0f, chroma = 1.0f;
    galosh::GaloshRawMode mode = galosh::GaloshRawMode::Full;
};

Args parse(int argc, char** argv) {
    Args a;
    auto need = [&](int i) {
        if (i + 1 >= argc) throw std::runtime_error("missing value");
        return std::string(argv[i + 1]);
    };
    for (int i = 1; i < argc; ++i) {
        const std::string k = argv[i];
        if (k == "--spv-dir") a.spvDir = need(i++);
        else if (k == "--in") a.inPath = need(i++);
        else if (k == "--out") a.outPrefix = need(i++);
        else if (k == "--w") a.w = static_cast<uint32_t>(std::stoul(need(i++)));
        else if (k == "--h") a.h = static_cast<uint32_t>(std::stoul(need(i++)));
        else if (k == "--black") a.black = std::stof(need(i++));
        else if (k == "--white") a.white = std::stof(need(i++));
        else if (k == "--strength") a.strength = std::stof(need(i++));
        else if (k == "--luma") a.luma = std::stof(need(i++));
        else if (k == "--chroma") a.chroma = std::stof(need(i++));
        else if (k == "--mode") {
            const std::string m = need(i++);
            a.mode = (m == "chroma-only") ? galosh::GaloshRawMode::ChromaOnly
                                          : galosh::GaloshRawMode::Full;
        }
    }
    if (a.spvDir.empty() || a.inPath.empty() || a.outPrefix.empty() || !a.w || !a.h)
        throw std::runtime_error("usage: see header comment");
    return a;
}

void transition(VkCommandBuffer cb, VkImage img, VkImageLayout oldL, VkImageLayout newL,
                VkAccessFlags srcA, VkAccessFlags dstA, VkPipelineStageFlags srcS,
                VkPipelineStageFlags dstS) {
    VkImageMemoryBarrier b{};
    b.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    b.oldLayout = oldL;
    b.newLayout = newL;
    b.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    b.image = img;
    b.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    b.srcAccessMask = srcA;
    b.dstAccessMask = dstA;
    vkCmdPipelineBarrier(cb, srcS, dstS, 0, 0, nullptr, 0, nullptr, 1, &b);
}

}  // namespace

int main(int argc, char** argv) {
    try {
        const Args a = parse(argc, argv);
        const size_t npix = static_cast<size_t>(a.w) * a.h;
        std::vector<float> hostF(npix);
        {
            std::ifstream f(a.inPath, std::ios::binary);
            if (!f) throw std::runtime_error("cannot open input");
            f.read(reinterpret_cast<char*>(hostF.data()), hostF.size() * sizeof(float));
        }

        galoshtest::Ctx ctx = galoshtest::ctx();
        VkDevice dev = ctx.dev;

        // R16_UINT images + host staging (codes).
        auto mkImg = [&](const char* what) {
            (void)what;
            return galoshtest::mkImg(ctx, a.w, a.h, VK_FORMAT_R16_UINT,
                                     VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT |
                                         VK_IMAGE_USAGE_TRANSFER_DST_BIT);
        };
        galoshtest::Img src = mkImg("src"), dst = mkImg("dst");
        galoshtest::Buf staging =
            galoshtest::mkBuf(ctx, npix * 2,
                              VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                              VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
        void* map = nullptr;
        galoshtest::ck(vkMapMemory(dev, staging.m, 0, npix * 2, 0, &map), "map staging");

        VkCommandPool pool = VK_NULL_HANDLE;
        {
            VkCommandPoolCreateInfo cp{};
            cp.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
            cp.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
            cp.queueFamilyIndex = ctx.qf;
            galoshtest::ck(vkCreateCommandPool(dev, &cp, nullptr, &pool), "pool");
        }
        VkCommandBuffer cb = VK_NULL_HANDLE;
        {
            VkCommandBufferAllocateInfo ca{};
            ca.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
            ca.commandPool = pool;
            ca.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
            ca.commandBufferCount = 1;
            galoshtest::ck(vkAllocateCommandBuffers(dev, &ca, &cb), "cb");
        }
        VkFence fence = VK_NULL_HANDLE;
        {
            VkFenceCreateInfo fc{};
            fc.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
            galoshtest::ck(vkCreateFence(dev, &fc, nullptr, &fence), "fence");
        }
        auto submitWait = [&] {
            galoshtest::ck(vkEndCommandBuffer(cb), "end");
            VkSubmitInfo si{};
            si.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
            si.commandBufferCount = 1;
            si.pCommandBuffers = &cb;
            galoshtest::ck(vkResetFences(dev, 1, &fence), "reset fence");
            galoshtest::ck(vkQueueSubmit(ctx.q, 1, &si, fence), "submit");
            galoshtest::ck(vkWaitForFences(dev, 1, &fence, VK_TRUE, UINT64_MAX), "wait");
        };
        auto begin = [&] {
            VkCommandBufferBeginInfo bi{};
            bi.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
            bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
            galoshtest::ck(vkResetCommandBuffer(cb, 0), "reset cb");
            galoshtest::ck(vkBeginCommandBuffer(cb, &bi), "begin");
        };

        // Host quantize float -> codes, upload to src, GENERAL layout.
        {
            auto* codes = static_cast<uint16_t*>(map);
            const float range = std::max(a.white - a.black, 1e-6f);
            for (size_t i = 0; i < npix; ++i) {
                const float v = std::min(std::max(hostF[i], 0.0f), 1.0f);
                codes[i] = static_cast<uint16_t>(std::min(std::max(std::round(v * range + a.black),
                                                                                  0.0f),
                                                          65535.0f));
            }
            begin();
            transition(cb, src.i, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                       0, VK_ACCESS_TRANSFER_WRITE_BIT, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                       VK_PIPELINE_STAGE_TRANSFER_BIT);
            VkBufferImageCopy cp{};
            cp.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
            cp.imageExtent = {a.w, a.h, 1};
            vkCmdCopyBufferToImage(cb, staging.b, src.i, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1,
                                   &cp);
            transition(cb, src.i, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_GENERAL,
                       VK_ACCESS_TRANSFER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT,
                       VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT);
            transition(cb, dst.i, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL, 0,
                       VK_ACCESS_SHADER_WRITE_BIT, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                       VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT);
            submitWait();
        }

        VkQueryPool qpool = VK_NULL_HANDLE;
        {
            VkQueryPoolCreateInfo qc{};
            qc.sType = VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO;
            qc.queryType = VK_QUERY_TYPE_TIMESTAMP;
            qc.queryCount = 16;
            galoshtest::ck(vkCreateQueryPool(dev, &qc, nullptr, &qpool), "qpool");
        }
        std::mutex mutex;
        galosh::GaloshRawParams p;
        p.mode = a.mode;
        p.strength = a.strength;
        p.lumaStrength = a.luma;
        p.chromaStrength = a.chroma;
        p.black = a.black;
        p.white = a.white;
        p.cfa = galosh::GaloshCfa::Rggb;
        float alpha = 0, sigmaSq = 0;
        {
            // Scoped before delCtx: pipeline teardown needs a live device.
            std::vector<std::vector<char>> keep;
            galosh::GaloshShaderMap shaders = galoshtest::spvMap(a.spvDir, keep);
            galosh::GaloshRawPipeline pipe(ctx.pd, dev, ctx.qf, shaders);
            pipe.process(ctx.q, mutex, src.v, dst.v, a.w, a.h, p, qpool);
            alpha = pipe.lastAlpha();
            sigmaSq = pipe.lastSigmaSq();
        }

        // Download codes.
        begin();
        transition(cb, dst.i, VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                   VK_ACCESS_SHADER_WRITE_BIT, VK_ACCESS_TRANSFER_READ_BIT,
                   VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT);
        VkBufferImageCopy cp{};
        cp.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
        cp.imageExtent = {a.w, a.h, 1};
        vkCmdCopyImageToBuffer(cb, dst.i, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, staging.b, 1, &cp);
        submitWait();
        {
            std::ofstream f(a.outPrefix + "_u16.bin", std::ios::binary);
            f.write(static_cast<const char*>(map), static_cast<std::streamsize>(npix * 2));
        }
        uint64_t ts[16] = {0};
        vkGetQueryPoolResults(dev, qpool, 12, 2, sizeof(ts), ts, sizeof(uint64_t),
                              VK_QUERY_RESULT_64_BIT | VK_QUERY_RESULT_WAIT_BIT);
        const double ms = static_cast<double>(ts[1] > ts[0] ? ts[1] - ts[0] : 0) * ctx.tsPeriod * 1e-6;
        std::cout << "VALIDATE: done ms=" << ms << " alpha=" << alpha << " sigmaSq=" << sigmaSq << "\n";

        vkDestroyQueryPool(dev, qpool, nullptr);
        vkDestroyFence(dev, fence, nullptr);
        vkDestroyCommandPool(dev, pool, nullptr);
        galoshtest::delBuf(ctx, staging);
        galoshtest::delImg(ctx, src);
        galoshtest::delImg(ctx, dst);
        galoshtest::delCtx(ctx);
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "VALIDATE: FAIL " << e.what() << "\n";
        return 1;
    }
}
