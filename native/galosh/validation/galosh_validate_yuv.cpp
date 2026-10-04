// galosh_validate_yuv: MoltenVK data runner for the native YUV port.
// Linear RGB float (.bin HWC) -> RGBA16F image -> GaloshYuvPipeline ->
// RGBA16F download (<out>_rgba16.bin, half bits). The driver
// (tools/validate_native_yuv.py) widens to float and gates: identity
// preservation (strengths 0), determinism, chroma effect, dropout census.
//
// Usage: galosh_validate_yuv --spv-dir DIR --in rgbfloat.bin --out PREFIX
//   --w W --h H [--sy Y] [--sc C] [--mode full|chroma-only]

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

#include "galosh/GaloshYuvPipeline.hpp"
#include "galosh_vk_test.h"

namespace {

// Round-to-nearest float32 -> binary16 (host upload to RGBA16F staging).
uint16_t f32ToF16(float f) {
    uint32_t bits;
    std::memcpy(&bits, &f, 4);
    const uint32_t s = (bits >> 16) & 0x8000u;
    const int32_t e = static_cast<int32_t>((bits >> 23) & 0xFFu) - 127 + 15;
    uint32_t m = bits & 0x7FFFFFu;
    if (e <= 0) {
        if (e < -10) return static_cast<uint16_t>(s);
        m |= 0x800000u;
        const uint32_t t = m >> (14 - e);
        return static_cast<uint16_t>(s | ((t + 1) >> 1));
    }
    if (e >= 31) return static_cast<uint16_t>(s | 0x7BFFu);
    return static_cast<uint16_t>(s | (static_cast<uint32_t>(e) << 10) | (m >> 13));
}

struct Args {
    std::string spvDir, inPath, outPrefix;
    uint32_t w = 0, h = 0;
    float sy = 1.0f, sc = 1.0f;
    galosh::GaloshYuvMode mode = galosh::GaloshYuvMode::Full;
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
        else if (k == "--sy") a.sy = std::stof(need(i++));
        else if (k == "--sc") a.sc = std::stof(need(i++));
        else if (k == "--mode") {
            const std::string m = need(i++);
            a.mode = (m == "chroma-only") ? galosh::GaloshYuvMode::ChromaOnly
                                          : galosh::GaloshYuvMode::Full;
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
        std::vector<float> hostF(npix * 3);
        {
            std::ifstream f(a.inPath, std::ios::binary);
            if (!f) throw std::runtime_error("cannot open input");
            f.read(reinterpret_cast<char*>(hostF.data()), hostF.size() * sizeof(float));
        }

        galoshtest::Ctx ctx = galoshtest::ctx();
        VkDevice dev = ctx.dev;

        auto mkImg = [&] {
            return galoshtest::mkImg(ctx, a.w, a.h, VK_FORMAT_R16G16B16A16_SFLOAT,
                                     VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT |
                                         VK_IMAGE_USAGE_TRANSFER_DST_BIT);
        };
        galoshtest::Img src = mkImg(), dst = mkImg();
        galoshtest::Buf staging =
            galoshtest::mkBuf(ctx, npix * 4 * 2,
                              VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                              VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
        void* map = nullptr;
        galoshtest::ck(vkMapMemory(dev, staging.m, 0, npix * 4 * 2, 0, &map), "map staging");

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

        // Host pack float RGB -> RGBA16F codes, upload to src.
        {
            auto* codes = static_cast<uint16_t*>(map);
            for (size_t i = 0; i < npix; ++i) {
                codes[4 * i + 0] = f32ToF16(hostF[3 * i + 0]);
                codes[4 * i + 1] = f32ToF16(hostF[3 * i + 1]);
                codes[4 * i + 2] = f32ToF16(hostF[3 * i + 2]);
                codes[4 * i + 3] = f32ToF16(1.0f);
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
        double ms = 0;
        float alpha = 0, sigmaSq = 0, sigmaGat = 0, degen = 0;
        {
            // Scoped before teardown: pipeline dtor needs a live device.
            std::vector<std::vector<char>> keep;
            galosh::GaloshShaderMap shaders = galoshtest::spvMap(a.spvDir, keep);
            galosh::GaloshYuvPipeline pipe(ctx.pd, dev, ctx.qf, shaders);
            std::mutex mutex;
            galosh::GaloshYuvParams p;
            p.mode = a.mode;
            p.strengthY = a.sy;
            p.strengthC = a.sc;
            pipe.process(ctx.q, mutex, src.v, dst.v, a.w, a.h, p, qpool);
            alpha = pipe.lastAlpha();
            sigmaSq = pipe.lastSigmaSq();
            sigmaGat = pipe.lastSigmaGat();
            degen = pipe.lastDegen();
        }

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
            std::ofstream f(a.outPrefix + "_rgba16.bin", std::ios::binary);
            f.write(static_cast<const char*>(map), static_cast<std::streamsize>(npix * 4 * 2));
        }
        uint64_t ts[16] = {0};
        vkGetQueryPoolResults(dev, qpool, 14, 2, sizeof(ts), ts, sizeof(uint64_t),
                              VK_QUERY_RESULT_64_BIT | VK_QUERY_RESULT_WAIT_BIT);
        ms = static_cast<double>(ts[1] > ts[0] ? ts[1] - ts[0] : 0) * ctx.tsPeriod * 1e-6;
        std::cout << "VALIDATE-YUV: done ms=" << ms << " alpha=" << alpha << " sigmaSq=" << sigmaSq
                  << " sigmaGat=" << sigmaGat << " degen=" << degen << "\n";

        vkDestroyQueryPool(dev, qpool, nullptr);
        vkDestroyFence(dev, fence, nullptr);
        vkDestroyCommandPool(dev, pool, nullptr);
        galoshtest::delBuf(ctx, staging);
        galoshtest::delImg(ctx, src);
        galoshtest::delImg(ctx, dst);
        galoshtest::delCtx(ctx);
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "VALIDATE-YUV: FAIL " << e.what() << "\n";
        return 1;
    }
}
