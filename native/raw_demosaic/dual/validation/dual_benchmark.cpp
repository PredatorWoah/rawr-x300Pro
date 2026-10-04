#include <algorithm>
#include <chrono>
#include <cstring>
#include <dual/Dual.hpp>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <numeric>
#include <stdexcept>
#include <string>
#include <vector>

#include "../../../vk_common/testing/vk_test_common.hpp"
using namespace vktest;
static std::vector<uint8_t> readBytes(const std::string& p) {
    std::ifstream f(p, std::ios::binary | std::ios::ate);
    if (!f) throw std::runtime_error("Cannot open " + p);
    auto n = f.tellg();
    f.seekg(0);
    std::vector<uint8_t> b(static_cast<size_t>(n));
    if (n > 0) f.read(reinterpret_cast<char*>(b.data()), n);
    return b;
}
static dual::ShaderProvider provider(std::string dir) {
    return [dir = std::move(dir)](std::string_view n) {
        auto b = readBytes(dir + "/" + std::string(n) + ".spv");
        if (b.size() % 4) throw std::runtime_error("Bad SPIR-V size");
        std::vector<uint32_t> w(b.size() / 4);
        std::memcpy(w.data(), b.data(), b.size());
        return w;
    };
}
static void transition(VkCommandBuffer cmd, VkImage image) {
    VkImageMemoryBarrier b{};
    b.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    b.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    b.newLayout = VK_IMAGE_LAYOUT_GENERAL;
    b.srcQueueFamilyIndex = b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    b.image = image;
    b.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    b.dstAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 0, nullptr, 0,
                         nullptr, 1, &b);
}
struct Stats {
    double best{}, median{}, p95{}, mean{}, worst{};
};
static double pct(const std::vector<double>& s, double q) {
    double p = q * double(s.size() - 1);
    size_t i = size_t(p);
    double f = p - i;
    return i + 1 < s.size() ? s[i] * (1 - f) + s[i + 1] * f : s[i];
}
static Stats stats(std::vector<double> v) {
    std::sort(v.begin(), v.end());
    return {v.front(), pct(v, .5), pct(v, .95), std::accumulate(v.begin(), v.end(), 0.0) / v.size(), v.back()};
}
int main(int argc, char** argv) {
    try {
        std::string sd, in;
        uint32_t W = 0, H = 0, warmup = 10, iters = 50;
        float contrast = 20.f;
        uint32_t mode = 1;
        bool autoContrast = false;
        for (int i = 1; i < argc; ++i) {
            std::string a = argv[i];
            if (a == "--shader-dir")
                sd = argv[++i];
            else if (a == "--input-f32")
                in = argv[++i];
            else if (a == "--width")
                W = uint32_t(std::stoul(argv[++i]));
            else if (a == "--height")
                H = uint32_t(std::stoul(argv[++i]));
            else if (a == "--warmup")
                warmup = uint32_t(std::stoul(argv[++i]));
            else if (a == "--iterations")
                iters = uint32_t(std::stoul(argv[++i]));
            else if (a == "--contrast-percent")
                contrast = std::stof(argv[++i]);
            else if (a == "--optimization-mode")
                mode = uint32_t(std::stoul(argv[++i]));
            else if (a == "--auto-contrast")
                autoContrast = true;
        }
        if (sd.empty() || in.empty() || !W || !H || !iters)
            throw std::runtime_error(
                "usage: dual_benchmark --shader-dir DIR --input-f32 FILE --width W --height H [--contrast-percent P "
                "--warmup N --iterations N]");
        auto bytes = readBytes(in);
        if (bytes.size() != size_t(W) * H * 4) throw std::runtime_error("input size mismatch");
        Ctx c = ctx();
        std::cout << "Using GPU: " << c.prop.deviceName << "\n";
        VkCommandPoolCreateInfo pci{};
        pci.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
        pci.queueFamilyIndex = c.qf;
        pci.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
        VkCommandPool pool{};
        ck(vkCreateCommandPool(c.dev, &pci, nullptr, &pool), "pool");
        Buf ib = mkBuf(c.pd, c.dev, bytes.size(), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
                       VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
        void* m = nullptr;
        ck(vkMapMemory(c.dev, ib.m, 0, bytes.size(), 0, &m), "map");
        std::memcpy(m, bytes.data(), bytes.size());
        vkUnmapMemory(c.dev, ib.m);
        Img oi = mkImg(c.pd, c.dev, W, H, VK_FORMAT_R16G16B16A16_SFLOAT, VK_IMAGE_USAGE_STORAGE_BIT);
        dual::PipelineConfig cfg{};
        cfg.width = W;
        cfg.height = H;
        cfg.pattern = dual::BayerPattern::RGGB;
        cfg.inputMode = dual::InputMode::NormalizedFloatBuffer;
        cfg.contrastPercent = contrast;
        cfg.autoContrast = autoContrast;
        cfg.optimizationMode = static_cast<dual::OptimizationMode>(mode);
        cfg.telemetry = true;
        auto pipe = std::make_unique<dual::DualDemosaicPipeline>(dual::VulkanContext{c.pd, c.dev, c.qf, nullptr},
                                                                 provider(sd), cfg);
        VkCommandBuffer cmd{};
        VkCommandBufferAllocateInfo cai{};
        cai.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        cai.commandPool = pool;
        cai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        cai.commandBufferCount = 1;
        ck(vkAllocateCommandBuffers(c.dev, &cai, &cmd), "cmd");
        {
            VkCommandBufferBeginInfo bi{};
            bi.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
            ck(vkBeginCommandBuffer(cmd, &bi), "begin init");
            transition(cmd, oi.i);
            ck(vkEndCommandBuffer(cmd), "end init");
            VkSubmitInfo si{};
            si.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
            si.commandBufferCount = 1;
            si.pCommandBuffers = &cmd;
            ck(vkQueueSubmit(c.q, 1, &si, VK_NULL_HANDLE), "submit init");
            ck(vkQueueWaitIdle(c.q), "wait init");
            ck(vkResetCommandBuffer(cmd, 0), "reset init");
        }
        dual::NormalizedBayerBufferView iv{{ib.b, 0, bytes.size()}, W, H, dual::BayerPattern::RGGB};
        dual::LinearRgbImage ov{oi.i, oi.v, VK_FORMAT_R16G16B16A16_SFLOAT, VK_IMAGE_LAYOUT_GENERAL, W, H};
        std::vector<double> total;
        total.reserve(iters);
        std::vector<dual::GpuEvent> lastEvents;
        for (uint32_t n = 0; n < warmup + iters; ++n) {
            VkCommandBufferBeginInfo bi{};
            bi.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
            ck(vkBeginCommandBuffer(cmd, &bi), "begin");
            auto t0 = std::chrono::steady_clock::now();
            pipe->record(cmd, iv, ov);
            ck(vkEndCommandBuffer(cmd), "end");
            VkSubmitInfo si{};
            si.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
            si.commandBufferCount = 1;
            si.pCommandBuffers = &cmd;
            ck(vkQueueSubmit(c.q, 1, &si, VK_NULL_HANDLE), "submit");
            ck(vkQueueWaitIdle(c.q), "wait");
            auto t1 = std::chrono::steady_clock::now();
            dual::FrameTelemetry tel{};
            if (!pipe->collectTelemetry(tel)) throw std::runtime_error("telemetry failed");
            lastEvents = tel.gpuEvents;
            if (n >= warmup) total.push_back(std::chrono::duration<double, std::milli>(t1 - t0).count());
            ck(vkResetCommandBuffer(cmd, 0), "reset");
        }
        auto s = stats(total);
        std::cout << std::fixed << std::setprecision(6) << "DUAL_BENCHMARK_RESULT width=" << W << " height=" << H
                  << " contrast_percent=" << contrast << " auto_contrast=" << (autoContrast ? 1 : 0)
                  << " optimization_mode=" << mode << " best_ms=" << s.best << " median_ms=" << s.median
                  << " p95_ms=" << s.p95 << " mean_ms=" << s.mean << " worst_ms=" << s.worst << "\n";
        for (const auto& e : lastEvents)
            std::cout << "DUAL_BENCHMARK_STAGE name=" << e.name << " last_ms=" << e.milliseconds << "\n";
        std::cout << "DUAL_BENCHMARK_PASS\n";
        pipe.reset();
        vkFreeCommandBuffers(c.dev, pool, 1, &cmd);
        delImg(c.dev, oi);
        delBuf(c.dev, ib);
        vkDestroyCommandPool(c.dev, pool, nullptr);
        delCtx(c);
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "DUAL_BENCHMARK_FAIL: " << e.what() << "\n";
        return 1;
    }
}
