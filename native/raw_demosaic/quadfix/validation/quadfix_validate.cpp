// Host harness: run the real QuadfixPipeline (Vulkan) over a float32 Bayer
// tile and write the filtered float32 tile. Mirrors
// native/raw_demosaic/rcd/validation/rcd_validate.cpp.
// Usage: grid_quadfix --shader-dir DIR --input-f32 IN --output-f32 OUT
//        --width W --height H [--fast]
#include <cstring>
#include <fstream>
#include <iostream>
#include <quadfix/Pipeline.hpp>
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
    std::vector<uint8_t> b(static_cast<size_t>(n), uint8_t{});
    if (n > 0) f.read((char*)b.data(), n);
    return b;
}
static quadfix::ShaderProvider provider(std::string dir) {
    return [dir = std::move(dir)](std::string_view n) {
        auto b = readBytes(dir + "/" + std::string(n) + ".spv");
        if (b.size() % 4) throw std::runtime_error("Bad SPIR-V size");
        std::vector<uint32_t> w(b.size() / 4);
        std::memcpy(w.data(), b.data(), b.size());
        return w;
    };
}
int main(int argc, char** argv) {
    try {
        std::string sd, in, out;
        uint32_t W = 0, H = 0;
        bool fast = false;
        for (int i = 1; i < argc; i++) {
            std::string a = argv[i];
            if (a == "--shader-dir")
                sd = argv[++i];
            else if (a == "--input-f32")
                in = argv[++i];
            else if (a == "--output-f32")
                out = argv[++i];
            else if (a == "--width")
                W = uint32_t(std::stoul(argv[++i]));
            else if (a == "--height")
                H = uint32_t(std::stoul(argv[++i]));
            else if (a == "--fast")
                fast = true;
        }
        if (sd.empty() || in.empty() || out.empty() || !W || !H)
            throw std::runtime_error(
                "usage: grid_quadfix --shader-dir DIR --input-f32 IN --output-f32 OUT --width W --height H [--fast]");
        auto bytes = readBytes(in);
        if (bytes.size() != size_t(W) * H * 4) throw std::runtime_error("input size mismatch");
        Ctx c = ctx();
        std::cout << "Using GPU: " << c.prop.deviceName << "\n";
        VkCommandPoolCreateInfo pci{};
        pci.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
        pci.queueFamilyIndex = c.qf;
        VkCommandPool pool{};
        ck(vkCreateCommandPool(c.dev, &pci, nullptr, &pool), "pool");
        auto memHost = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
        Buf ib = mkBuf(c.pd, c.dev, bytes.size(), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, memHost);
        Buf ob = mkBuf(c.pd, c.dev, bytes.size(),
                       VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT, memHost);
        void* m = nullptr;
        ck(vkMapMemory(c.dev, ib.m, 0, bytes.size(), 0, &m), "map in");
        std::memcpy(m, bytes.data(), bytes.size());
        vkUnmapMemory(c.dev, ib.m);
        quadfix::PipelineConfig cfg{};
        cfg.width = W;
        cfg.height = H;
        cfg.fastMedian = fast;
        auto pipe = std::make_unique<quadfix::QuadfixPipeline>(quadfix::VulkanContext{c.pd, c.dev, c.qf, nullptr},
                                                               provider(sd), cfg, quadfix::PipelineAssets{});
        VkCommandBuffer cmd{};
        VkCommandBufferAllocateInfo cai{};
        cai.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        cai.commandPool = pool;
        cai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        cai.commandBufferCount = 1;
        ck(vkAllocateCommandBuffers(c.dev, &cai, &cmd), "cmd");
        VkCommandBufferBeginInfo cbi{};
        cbi.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        ck(vkBeginCommandBuffer(cmd, &cbi), "begin");
        quadfix::BayerBufferView iv{{ib.b, 0, bytes.size()}, W, H};
        quadfix::BayerBufferView ov{{ob.b, 0, bytes.size()}, W, H};
        pipe->record(cmd, iv, ov);
        ck(vkEndCommandBuffer(cmd), "end");
        VkSubmitInfo si{};
        si.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        si.commandBufferCount = 1;
        si.pCommandBuffers = &cmd;
        ck(vkQueueSubmit(c.q, 1, &si, VK_NULL_HANDLE), "submit");
        ck(vkQueueWaitIdle(c.q), "wait");
        void* mo = nullptr;
        ck(vkMapMemory(c.dev, ob.m, 0, bytes.size(), 0, &mo), "map out");
        std::vector<uint8_t> obytes(bytes.size());
        std::memcpy(obytes.data(), mo, bytes.size());
        vkUnmapMemory(c.dev, ob.m);
        std::ofstream f(out, std::ios::binary);
        f.write((char*)obytes.data(), std::streamsize(obytes.size()));
        f.close();
        std::cout << "QUADFIX_VULKAN_PASS pixels=" << size_t(W) * H << (fast ? " fast" : " exact") << "\n";
        pipe.reset();
        vkFreeCommandBuffers(c.dev, pool, 1, &cmd);
        delBuf(c.dev, ib);
        delBuf(c.dev, ob);
        vkDestroyCommandPool(c.dev, pool, nullptr);
        delCtx(c);
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "QUADFIX_VULKAN_FAIL: " << e.what() << "\n";
        return 1;
    }
}
