#include <cstring>
#include <fcc/Fcc.hpp>
#include <fstream>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

#include "../../vk_common/testing/vk_test_common.hpp"
using namespace vktest;

static std::vector<uint8_t> readBytes(const std::string& p) {
    std::ifstream f(p, std::ios::binary | std::ios::ate);
    if (!f) throw std::runtime_error("Cannot open " + p);
    auto n = f.tellg();
    if (n < 0) throw std::runtime_error("tellg failed");
    f.seekg(0);
    std::vector<uint8_t> b(static_cast<size_t>(n));
    if (n > 0) f.read(reinterpret_cast<char*>(b.data()), n);
    return b;
}
static fcc::ShaderProvider provider(std::string dir) {
    return [dir = std::move(dir)](std::string_view n) {
        auto b = readBytes(dir + "/" + std::string(n) + ".spv");
        if (b.size() % 4) throw std::runtime_error("bad SPIR-V");
        std::vector<uint32_t> w(b.size() / 4);
        std::memcpy(w.data(), b.data(), b.size());
        return w;
    };
}
static VkImageMemoryBarrier bar(VkImage im, VkAccessFlags s, VkAccessFlags d, VkImageLayout o, VkImageLayout n) {
    VkImageMemoryBarrier b{};
    b.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    b.srcAccessMask = s;
    b.dstAccessMask = d;
    b.oldLayout = o;
    b.newLayout = n;
    b.srcQueueFamilyIndex = b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    b.image = im;
    b.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    return b;
}
static VkCommandBuffer cmdAlloc(const Ctx& c, VkCommandPool p) {
    VkCommandBuffer x{};
    VkCommandBufferAllocateInfo a{};
    a.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    a.commandPool = p;
    a.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    a.commandBufferCount = 1;
    ck(vkAllocateCommandBuffers(c.dev, &a, &x), "cmd alloc");
    return x;
}
static void begin(VkCommandBuffer c) {
    VkCommandBufferBeginInfo b{};
    b.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    ck(vkBeginCommandBuffer(c, &b), "cmd begin");
}
static void submit(const Ctx& c, VkCommandBuffer x) {
    ck(vkEndCommandBuffer(x), "cmd end");
    VkSubmitInfo s{};
    s.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    s.commandBufferCount = 1;
    s.pCommandBuffers = &x;
    ck(vkQueueSubmit(c.q, 1, &s, VK_NULL_HANDLE), "submit");
    ck(vkQueueWaitIdle(c.q), "wait");
}
static Img upload(const Ctx& c, VkCommandPool p, const std::vector<uint8_t>& bytes, uint32_t W, uint32_t H) {
    Buf st = mkBuf(c.pd, c.dev, bytes.size(), VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                   VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    void* m = nullptr;
    ck(vkMapMemory(c.dev, st.m, 0, bytes.size(), 0, &m), "map");
    std::memcpy(m, bytes.data(), bytes.size());
    vkUnmapMemory(c.dev, st.m);
    Img im = mkImg(c.pd, c.dev, W, H, VK_FORMAT_R16G16B16A16_SFLOAT,
                   VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT);
    auto cb = cmdAlloc(c, p);
    begin(cb);
    auto b0 =
        bar(im.i, 0, VK_ACCESS_TRANSFER_WRITE_BIT, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
    vkCmdPipelineBarrier(cb, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0,
                         nullptr, 1, &b0);
    VkBufferImageCopy cp{};
    cp.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
    cp.imageExtent = {W, H, 1};
    vkCmdCopyBufferToImage(cb, st.b, im.i, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &cp);
    auto b1 = bar(im.i, VK_ACCESS_TRANSFER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                  VK_IMAGE_LAYOUT_GENERAL);
    vkCmdPipelineBarrier(cb, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 0, nullptr, 0,
                         nullptr, 1, &b1);
    submit(c, cb);
    vkFreeCommandBuffers(c.dev, p, 1, &cb);
    delBuf(c.dev, st);
    return im;
}
static void initOut(VkCommandBuffer cb, VkImage im) {
    auto b = bar(im, 0, VK_ACCESS_SHADER_WRITE_BIT, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL);
    vkCmdPipelineBarrier(cb, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 0, nullptr, 0,
                         nullptr, 1, &b);
}
static std::vector<uint8_t> readback(const Ctx& c, VkCommandPool p, VkImage im, uint32_t W, uint32_t H) {
    const VkDeviceSize n = VkDeviceSize(size_t(W) * H * 8);
    Buf st = mkBuf(c.pd, c.dev, n, VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                   VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    auto cb = cmdAlloc(c, p);
    begin(cb);
    auto b = bar(im, VK_ACCESS_SHADER_WRITE_BIT, VK_ACCESS_TRANSFER_READ_BIT, VK_IMAGE_LAYOUT_GENERAL,
                 VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
    vkCmdPipelineBarrier(cb, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0,
                         nullptr, 1, &b);
    VkBufferImageCopy cp{};
    cp.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
    cp.imageExtent = {W, H, 1};
    vkCmdCopyImageToBuffer(cb, im, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, st.b, 1, &cp);
    submit(c, cb);
    void* m = nullptr;
    ck(vkMapMemory(c.dev, st.m, 0, n, 0, &m), "map out");
    std::vector<uint8_t> o;
    o.resize(static_cast<size_t>(n));
    std::memcpy(o.data(), m, size_t(n));
    vkUnmapMemory(c.dev, st.m);
    vkFreeCommandBuffers(c.dev, p, 1, &cb);
    delBuf(c.dev, st);
    return o;
}
int main(int argc, char** argv) {
    try {
        std::string sd, in, out;
        uint32_t W = 0, H = 0, steps = 1;
        bool normalized = false;
        float edgeSigma = 0.0f, chromaBound = 0.0f;
        for (int i = 1; i < argc; ++i) {
            std::string a = argv[i];
            if (a == "--shader-dir")
                sd = argv[++i];
            else if (a == "--input")
                in = argv[++i];
            else if (a == "--output")
                out = argv[++i];
            else if (a == "--width")
                W = std::stoul(argv[++i]);
            else if (a == "--height")
                H = std::stoul(argv[++i]);
            else if (a == "--steps")
                steps = std::stoul(argv[++i]);
            else if (a == "--normalized")
                normalized = std::stoul(argv[++i]) != 0;
            else if (a == "--edge-sigma")
                edgeSigma = std::stof(argv[++i]);
            else if (a == "--chroma-bound")
                chromaBound = std::stof(argv[++i]);
        }
        if (sd.empty() || in.empty() || out.empty() || !W || !H || steps < 1 || steps > 8)
            throw std::runtime_error(
                "usage: fcc_validate --shader-dir DIR --input rgba16f --output rgba16f --width W --height H --steps "
                "1..8");
        auto bytes = readBytes(in);
        if (bytes.size() != size_t(W) * H * 8) throw std::runtime_error("input byte size mismatch");
        Ctx c = ctx();
        std::cout << "Using GPU: " << c.prop.deviceName << "\n";
        VkCommandPoolCreateInfo pi{};
        pi.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
        pi.queueFamilyIndex = c.qf;
        VkCommandPool pool{};
        ck(vkCreateCommandPool(c.dev, &pi, nullptr, &pool), "pool");
        Img src = upload(c, pool, bytes, W, H);
        Img dst = mkImg(c.pd, c.dev, W, H, VK_FORMAT_R16G16B16A16_SFLOAT,
                        VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT);
        // Defaults match the legacy absolute-chroma CPU reference. The production
        // video/still configuration is --normalized 1 --edge-sigma 0.08 --chroma-bound 1.
        fcc::PipelineConfig cfg{W, H, steps, normalized, edgeSigma, chromaBound};
        auto pipe = std::make_unique<fcc::FalseColorCorrectionPipeline>(fcc::VulkanContext{c.pd, c.dev, c.qf, nullptr},
                                                                        provider(sd), cfg);
        std::cout << "FCC_VALIDATE_STAGE pipeline_created steps=" << steps << std::endl;
        auto cb = cmdAlloc(c, pool);
        begin(cb);
        initOut(cb, dst.i);
        pipe->record(cb, {src.i, src.v, VK_FORMAT_R16G16B16A16_SFLOAT, VK_IMAGE_LAYOUT_GENERAL, W, H},
                     {dst.i, dst.v, VK_FORMAT_R16G16B16A16_SFLOAT, VK_IMAGE_LAYOUT_GENERAL, W, H}, steps);
        submit(c, cb);
        vkFreeCommandBuffers(c.dev, pool, 1, &cb);
        auto o = readback(c, pool, dst.i, W, H);
        std::ofstream f(out, std::ios::binary);
        f.write((char*)o.data(), o.size());
        std::cout << "FCC_VULKAN_VALIDATE_PASS steps=" << steps << " allocated=" << pipe->currentAllocatedBytes()
                  << " peak=" << pipe->peakAllocatedBytes() << std::endl;
        pipe.reset();
        std::cout << "FCC_VALIDATE_STAGE pipeline_destroyed" << std::endl;
        delImg(c.dev, dst);
        delImg(c.dev, src);
        vkDestroyCommandPool(c.dev, pool, nullptr);
        delCtx(c);
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "FCC_VULKAN_VALIDATE_FAIL: " << e.what() << "\n";
        return 1;
    }
}
