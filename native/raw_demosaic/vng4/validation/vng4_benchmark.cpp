#include <algorithm>
#include <array>
#include <chrono>
#include <cstring>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <numeric>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>
#include <vng4/Vng4.hpp>

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
static vng4::ShaderProvider provider(std::string d) {
    return [d = std::move(d)](std::string_view n) {
        auto b = readBytes(d + "/" + std::string(n) + ".spv");
        if (b.size() % 4) throw std::runtime_error("Bad SPIR-V size");
        std::vector<uint32_t> w(b.size() / 4);
        std::memcpy(w.data(), b.data(), b.size());
        return w;
    };
}
static void transition(VkCommandBuffer cmd, VkImage im) {
    VkImageMemoryBarrier b{};
    b.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    b.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    b.newLayout = VK_IMAGE_LAYOUT_GENERAL;
    b.srcQueueFamilyIndex = b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    b.image = im;
    b.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    b.dstAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 0, nullptr, 0,
                         nullptr, 1, &b);
}
struct Stats {
    double best{}, median{}, p95{}, p99{}, mean{}, worst{};
};
static double pct(const std::vector<double>& s, double q) {
    double p = q * double(s.size() - 1);
    size_t i = size_t(p);
    double f = p - double(i);
    return i + 1 < s.size() ? s[i] * (1 - f) + s[i + 1] * f : s[i];
}
static Stats stat(std::vector<double> v) {
    std::sort(v.begin(), v.end());
    return {
        v.front(), pct(v, .5), pct(v, .95), pct(v, .99), std::accumulate(v.begin(), v.end(), 0.0) / double(v.size()),
        v.back()};
}
static void print(const char* n, const Stats& s) {
    std::cout << "VNG4_BENCH_STAGE name=" << n << " best_ms=" << s.best << " median_ms=" << s.median
              << " p95_ms=" << s.p95 << " p99_ms=" << s.p99 << " mean_ms=" << s.mean << " worst_ms=" << s.worst << "\n";
}
int main(int argc, char** argv) {
    try {
        std::string sd, in;
        uint32_t W = 0, H = 0, warm = 20, iters = 100, greenWgx = 16, greenWgy = 16, exportWgx = 16, exportWgy = 16;
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
                warm = uint32_t(std::stoul(argv[++i]));
            else if (a == "--iterations")
                iters = uint32_t(std::stoul(argv[++i]));
            else if (a == "--green-wg-x")
                greenWgx = uint32_t(std::stoul(argv[++i]));
            else if (a == "--green-wg-y")
                greenWgy = uint32_t(std::stoul(argv[++i]));
            else if (a == "--export-wg-x")
                exportWgx = uint32_t(std::stoul(argv[++i]));
            else if (a == "--export-wg-y")
                exportWgy = uint32_t(std::stoul(argv[++i]));
            else if (a == "--green-wg-x")
                greenWgx = uint32_t(std::stoul(argv[++i]));
            else if (a == "--green-wg-y")
                greenWgy = uint32_t(std::stoul(argv[++i]));
            else if (a == "--export-wg-x")
                exportWgx = uint32_t(std::stoul(argv[++i]));
            else if (a == "--export-wg-y")
                exportWgy = uint32_t(std::stoul(argv[++i]));
        }
        if (sd.empty() || in.empty() || !W || !H || !iters)
            throw std::runtime_error(
                "usage: vng4_benchmark --shader-dir DIR --input-f32 FILE --width W --height H [--warmup N] "
                "[--iterations N]");
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
        vng4::PipelineConfig cfg{};
        cfg.width = W;
        cfg.height = H;
        cfg.pattern = vng4::BayerPattern::RGGB;
        cfg.inputMode = vng4::InputMode::NormalizedFloatBuffer;
        cfg.telemetry = true;
        cfg.greenWorkgroupX = greenWgx;
        cfg.greenWorkgroupY = greenWgy;
        cfg.exportWorkgroupX = exportWgx;
        cfg.exportWorkgroupY = exportWgy;
        auto pipe = std::make_unique<vng4::Vng4Pipeline>(vng4::VulkanContext{c.pd, c.dev, c.qf, nullptr}, provider(sd),
                                                         cfg, vng4::PipelineAssets{});
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
        vng4::NormalizedBayerBufferView iv{{ib.b, 0, bytes.size()}, W, H, vng4::BayerPattern::RGGB};
        vng4::LinearRgbImage ov{oi.i, oi.v, VK_FORMAT_R16G16B16A16_SFLOAT, VK_IMAGE_LAYOUT_GENERAL, W, H};
        std::array<std::vector<double>, 4> s;
        for (auto& v : s) v.reserve(iters);
        std::vector<double> cpu;
        cpu.reserve(iters);
        const char* names[4] = {"vng4.linear", "vng4.green", "vng4.export", "vng4.total"};
        for (uint32_t n = 0; n < warm + iters; ++n) {
            VkCommandBufferBeginInfo bi{};
            bi.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
            ck(vkBeginCommandBuffer(cmd, &bi), "begin");
            pipe->record(cmd, iv, ov);
            ck(vkEndCommandBuffer(cmd), "end");
            VkSubmitInfo si{};
            si.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
            si.commandBufferCount = 1;
            si.pCommandBuffers = &cmd;
            auto t0 = std::chrono::steady_clock::now();
            ck(vkQueueSubmit(c.q, 1, &si, VK_NULL_HANDLE), "submit");
            ck(vkQueueWaitIdle(c.q), "wait");
            auto t1 = std::chrono::steady_clock::now();
            vng4::FrameTelemetry tel{};
            if (!pipe->collectTelemetry(tel)) throw std::runtime_error("timestamp query collection failed");
            if (n >= warm) {
                for (size_t k = 0; k < 4; ++k) {
                    auto it = std::find_if(tel.gpuEvents.begin(), tel.gpuEvents.end(),
                                           [&](const auto& e) { return e.name == names[k]; });
                    if (it == tel.gpuEvents.end()) throw std::runtime_error("missing telemetry");
                    s[k].push_back(it->milliseconds);
                }
                cpu.push_back(std::chrono::duration<double, std::milli>(t1 - t0).count());
            }
            ck(vkResetCommandBuffer(cmd, 0), "reset");
        }
        std::cout << std::fixed << std::setprecision(6);
        std::cout << "VNG4_BENCHMARK_BEGIN width=" << W << " height=" << H << " warmup=" << warm
                  << " iterations=" << iters
                  << " input=NormalizedFloatBuffer_LINEAR255 wg_linear=" << cfg.linearWorkgroupX << "x"
                  << cfg.linearWorkgroupY << " wg_green=" << cfg.greenWorkgroupX << "x" << cfg.greenWorkgroupY
                  << " wg_export=" << cfg.exportWorkgroupX << "x" << cfg.exportWorkgroupY << "\n";
        for (size_t k = 0; k < 4; ++k) print(names[k], stat(s[k]));
        print("cpu_submit_wait", stat(cpu));
        std::cout << "VNG4_BENCHMARK_PASS width=" << W << " height=" << H << " iterations=" << iters << "\n";
        pipe.reset();
        vkFreeCommandBuffers(c.dev, pool, 1, &cmd);
        delImg(c.dev, oi);
        delBuf(c.dev, ib);
        vkDestroyCommandPool(c.dev, pool, nullptr);
        delCtx(c);
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "VNG4_BENCHMARK_FAIL: " << e.what() << "\n";
        return 1;
    }
}
