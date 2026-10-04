#include <algorithm>
#include <chrono>
#include <cstring>
#include <fcc/Fcc.hpp>
#include <fstream>
#include <iostream>
#include <memory>
#include <numeric>
#include <stdexcept>
#include <string>
#include <vector>

#include "../../vk_common/testing/vk_test_common.hpp"
using namespace vktest;
static std::vector<uint8_t> rb(const std::string& p) {
    std::ifstream f(p, std::ios::binary | std::ios::ate);
    if (!f) throw std::runtime_error("open " + p);
    auto n = f.tellg();
    f.seekg(0);
    std::vector<uint8_t> b;
    b.resize(static_cast<size_t>(n));
    f.read((char*)b.data(), n);
    return b;
}
static fcc::ShaderProvider prov(std::string d) {
    return [d = std::move(d)](std::string_view n) {
        auto b = rb(d + "/" + std::string(n) + ".spv");
        std::vector<uint32_t> w(b.size() / 4);
        std::memcpy(w.data(), b.data(), b.size());
        return w;
    };
}
static VkImageMemoryBarrier bar(VkImage i, VkAccessFlags s, VkAccessFlags d, VkImageLayout o, VkImageLayout n) {
    VkImageMemoryBarrier b{};
    b.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    b.srcAccessMask = s;
    b.dstAccessMask = d;
    b.oldLayout = o;
    b.newLayout = n;
    b.srcQueueFamilyIndex = b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    b.image = i;
    b.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    return b;
}
static double pct(std::vector<double> v, double p) {
    std::sort(v.begin(), v.end());
    double x = p * (v.size() - 1);
    size_t a = size_t(x), b = std::min(a + 1, v.size() - 1);
    return v[a] + (v[b] - v[a]) * (x - a);
}
int main(int argc, char** argv) {
    try {
        std::string sd;
        uint32_t W = 4080, H = 3072, steps = 1, warm = 10, iters = 30;
        float edgeSigma = 0.0f, chromaBound = 0.0f;
        for (int i = 1; i < argc; ++i) {
            std::string a = argv[i];
            if (a == "--shader-dir")
                sd = argv[++i];
            else if (a == "--width")
                W = std::stoul(argv[++i]);
            else if (a == "--height")
                H = std::stoul(argv[++i]);
            else if (a == "--steps")
                steps = std::stoul(argv[++i]);
            else if (a == "--warmup")
                warm = std::stoul(argv[++i]);
            else if (a == "--iterations")
                iters = std::stoul(argv[++i]);
            else if (a == "--edge-sigma")
                edgeSigma = std::stof(argv[++i]);
            else if (a == "--chroma-bound")
                chromaBound = std::stof(argv[++i]);
        }
        if (sd.empty() || steps < 1 || steps > 8) throw std::runtime_error("bad args");
        Ctx c = ctx();
        std::cout << "Using GPU: " << c.prop.deviceName << "\n";
        VkPhysicalDeviceProperties pp{};
        vkGetPhysicalDeviceProperties(c.pd, &pp);
        uint32_t nq = 0;
        vkGetPhysicalDeviceQueueFamilyProperties(c.pd, &nq, nullptr);
        std::vector<VkQueueFamilyProperties> qp(nq);
        vkGetPhysicalDeviceQueueFamilyProperties(c.pd, &nq, qp.data());
        if (!qp[c.qf].timestampValidBits) throw std::runtime_error("timestamps unsupported");
        VkCommandPoolCreateInfo pci{};
        pci.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
        pci.queueFamilyIndex = c.qf;
        VkCommandPool pool{};
        ck(vkCreateCommandPool(c.dev, &pci, nullptr, &pool), "pool");
        Img src = mkImg(c.pd, c.dev, W, H, VK_FORMAT_R16G16B16A16_SFLOAT,
                        VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT);
        Img out = mkImg(c.pd, c.dev, W, H, VK_FORMAT_R16G16B16A16_SFLOAT, VK_IMAGE_USAGE_STORAGE_BIT);
        VkQueryPoolCreateInfo qi{};
        qi.sType = VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO;
        qi.queryType = VK_QUERY_TYPE_TIMESTAMP;
        qi.queryCount = 2;
        VkQueryPool q{};
        ck(vkCreateQueryPool(c.dev, &qi, nullptr, &q), "queries");
        auto pipe = std::make_unique<fcc::FalseColorCorrectionPipeline>(
            fcc::VulkanContext{c.pd, c.dev, c.qf, nullptr}, prov(sd),
            fcc::PipelineConfig{W, H, steps, true, edgeSigma, chromaBound});
        std::cout << "FCC_BENCH_STAGE pipeline_created steps=" << steps << " edgeSigma=" << edgeSigma
                  << " chromaBound=" << chromaBound << std::endl;
        auto run = [&]() {
            VkCommandBuffer cb{};
            VkCommandBufferAllocateInfo ai{};
            ai.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
            ai.commandPool = pool;
            ai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
            ai.commandBufferCount = 1;
            ck(vkAllocateCommandBuffers(c.dev, &ai, &cb), "cmd");
            VkCommandBufferBeginInfo bi{};
            bi.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
            ck(vkBeginCommandBuffer(cb, &bi), "begin");
            static bool first = true;
            if (first) {
                VkImageMemoryBarrier bsrc = bar(src.i, 0, VK_ACCESS_TRANSFER_WRITE_BIT, VK_IMAGE_LAYOUT_UNDEFINED,
                                                VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
                vkCmdPipelineBarrier(cb, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0,
                                     nullptr, 0, nullptr, 1, &bsrc);
                VkImageMemoryBarrier bout =
                    bar(out.i, 0, VK_ACCESS_SHADER_WRITE_BIT, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL);
                vkCmdPipelineBarrier(cb, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 0,
                                     nullptr, 0, nullptr, 1, &bout);
                VkClearColorValue cv{};
                cv.float32[0] = .7f;
                cv.float32[1] = .9f;
                cv.float32[2] = .55f;
                cv.float32[3] = 1.f;
                VkImageSubresourceRange rr{VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
                vkCmdClearColorImage(cb, src.i, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &cv, 1, &rr);
                auto b = bar(src.i, VK_ACCESS_TRANSFER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT,
                             VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_GENERAL);
                vkCmdPipelineBarrier(cb, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 0,
                                     nullptr, 0, nullptr, 1, &b);
                first = false;
            }
            vkCmdResetQueryPool(cb, q, 0, 2);
            vkCmdWriteTimestamp(cb, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, q, 0);
            pipe->record(cb, {src.i, src.v, VK_FORMAT_R16G16B16A16_SFLOAT, VK_IMAGE_LAYOUT_GENERAL, W, H},
                         {out.i, out.v, VK_FORMAT_R16G16B16A16_SFLOAT, VK_IMAGE_LAYOUT_GENERAL, W, H}, steps);
            vkCmdWriteTimestamp(cb, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, q, 1);
            ck(vkEndCommandBuffer(cb), "end");
            VkSubmitInfo si{};
            si.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
            si.commandBufferCount = 1;
            si.pCommandBuffers = &cb;
            ck(vkQueueSubmit(c.q, 1, &si, VK_NULL_HANDLE), "submit");
            ck(vkQueueWaitIdle(c.q), "wait");
            uint64_t t[2]{};
            ck(vkGetQueryPoolResults(c.dev, q, 0, 2, sizeof(t), t, sizeof(uint64_t),
                                     VK_QUERY_RESULT_64_BIT | VK_QUERY_RESULT_WAIT_BIT),
               "results");
            vkFreeCommandBuffers(c.dev, pool, 1, &cb);
            return double(t[1] - t[0]) * double(pp.limits.timestampPeriod) / 1e6;
        };
        for (uint32_t i = 0; i < warm; ++i) run();
        std::vector<double> v;
        v.reserve(iters);
        for (uint32_t i = 0; i < iters; ++i) v.push_back(run());
        double mean = std::accumulate(v.begin(), v.end(), 0.0) / v.size();
        std::cout << "FCC_BENCH_PASS width=" << W << " height=" << H << " steps=" << steps
                  << " edgeSigma=" << edgeSigma << " chromaBound=" << chromaBound << " mean_ms=" << mean
                  << " median_ms=" << pct(v, .5) << " p95_ms=" << pct(v, .95)
                  << " min_ms=" << *std::min_element(v.begin(), v.end())
                  << " max_ms=" << *std::max_element(v.begin(), v.end())
                  << " allocated=" << pipe->currentAllocatedBytes() << " peak=" << pipe->peakAllocatedBytes()
                  << std::endl;
        pipe.reset();
        std::cout << "FCC_BENCH_STAGE pipeline_destroyed" << std::endl;
        vkDestroyQueryPool(c.dev, q, nullptr);
        delImg(c.dev, out);
        delImg(c.dev, src);
        vkDestroyCommandPool(c.dev, pool, nullptr);
        delCtx(c);
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "FCC_BENCH_FAIL: " << e.what() << "\n";
        return 1;
    }
}
