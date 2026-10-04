// Runs two single-dispatch compute shaders that map one RGBA16F storage
// image (binding 0) to another (binding 1) on the same synthetic frame, then
// reports bit differences and GPU time for each. Used to prove that an
// optimized rewrite of a per-pixel pass (e.g. still_defringe.comp) is exact.
//
// usage: image_pass_parity old.spv new.spv W H iterations localSize push...
//   push words: u<int> or f<float>, packed as 32-bit push constants.
#include <algorithm>
#include <cmath>
#include <functional>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iostream>
#include <random>
#include <stdexcept>
#include <string>
#include <vector>

#include "vk_test_common.hpp"

using namespace vktest;
namespace {
std::vector<uint32_t> readSpirv(const std::string& path) {
    std::ifstream f(path, std::ios::binary | std::ios::ate);
    if (!f) throw std::runtime_error("cannot read " + path);
    const auto n = size_t(f.tellg());
    std::vector<uint32_t> w(n / 4);
    f.seekg(0);
    f.read(reinterpret_cast<char*>(w.data()), std::streamsize(n));
    return w;
}
uint16_t floatToHalf(float v) {
    uint32_t x;
    std::memcpy(&x, &v, 4);
    const uint32_t sign = (x >> 16) & 0x8000u;
    int e = int((x >> 23) & 0xFF) - 127 + 15;
    uint32_t m = x & 0x7FFFFFu;
    if (e <= 0) return uint16_t(sign);
    if (e >= 31) return uint16_t(sign | 0x7C00u);
    return uint16_t(sign | (uint32_t(e) << 10) | ((m + 0x1000u) >> 13));
}
// Linear camera-RGB-like test frame: gradients, high-contrast edges, purple
// fringes along bright edges, wide purple patches and noise.
std::vector<uint16_t> makeFrame(uint32_t W, uint32_t H) {
    std::vector<uint16_t> px(size_t(W) * H * 4);
    std::mt19937 rng(7);
    std::normal_distribution<float> noise(0.0f, 0.01f);
    for (uint32_t y = 0; y < H; ++y)
        for (uint32_t x = 0; x < W; ++x) {
            float base = 0.05f + 0.4f * float(x) / float(W);
            float r = base, g = base, b = base * 0.9f;
            const bool bright = ((x / 97) + (y / 61)) % 2 == 0;
            if (bright) r = g = b = 0.9f;
            const uint32_t dx = x % 97;
            if (!bright && (dx < 3 || dx > 93)) { r += 0.3f; b += 0.35f; g -= 0.02f; }
            if (x > W / 2 && x < W / 2 + 300 && y > H / 3 && y < H / 3 + 300) { r = 0.4f; b = 0.45f; g = 0.1f; }
            const float v[4] = {std::max(r + noise(rng), 0.0f), std::max(g + noise(rng), 0.0f),
                                std::max(b + noise(rng), 0.0f), 1.0f};
            for (int c = 0; c < 4; ++c) px[(size_t(y) * W + x) * 4 + size_t(c)] = floatToHalf(v[c]);
        }
    return px;
}
struct Pass {
    VkShaderModule module{};
    VkPipeline pipeline{};
};
}  // namespace

int main(int argc, char** argv) try {
    if (argc < 7) {
        std::cerr << "usage: image_pass_parity old.spv new.spv W H iterations localSize [u<int>|f<float>]...\n";
        return 2;
    }
    const uint32_t W = std::stoul(argv[3]), H = std::stoul(argv[4]);
    const int iterations = std::stoi(argv[5]);
    const uint32_t local = std::stoul(argv[6]);
    std::vector<uint32_t> push;
    for (int i = 7; i < argc; ++i) {
        const std::string t = argv[i];
        if (t[0] == 'u') push.push_back(uint32_t(std::stoul(t.substr(1))));
        else {
            const float f = std::stof(t.substr(1));
            uint32_t u;
            std::memcpy(&u, &f, 4);
            push.push_back(u);
        }
    }
    Ctx c = ctx();
    VkCommandPoolCreateInfo pi{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
    pi.queueFamilyIndex = c.qf;
    pi.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    VkCommandPool pool{};
    ck(vkCreateCommandPool(c.dev, &pi, nullptr, &pool), "pool");
    const size_t bytes = size_t(W) * H * 8;
    const auto frame = makeFrame(W, H);
    Img src = mkImg(c.pd, c.dev, W, H, VK_FORMAT_R16G16B16A16_SFLOAT,
                    VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT);
    Img dst = mkImg(c.pd, c.dev, W, H, VK_FORMAT_R16G16B16A16_SFLOAT,
                    VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT);
    Buf staging = mkBuf(c.pd, c.dev, bytes, VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    void* mapped = nullptr;
    ck(vkMapMemory(c.dev, staging.m, 0, bytes, 0, &mapped), "map");

    VkDescriptorSetLayoutBinding b[2]{};
    for (uint32_t i = 0; i < 2; ++i) b[i] = {i, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr};
    VkDescriptorSetLayoutCreateInfo li{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
    li.bindingCount = 2;
    li.pBindings = b;
    VkDescriptorSetLayout setLayout{};
    ck(vkCreateDescriptorSetLayout(c.dev, &li, nullptr, &setLayout), "set layout");
    VkPushConstantRange range{VK_SHADER_STAGE_COMPUTE_BIT, 0, uint32_t(push.size() * 4)};
    VkPipelineLayoutCreateInfo pli{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
    pli.setLayoutCount = 1;
    pli.pSetLayouts = &setLayout;
    pli.pushConstantRangeCount = push.empty() ? 0 : 1;
    pli.pPushConstantRanges = &range;
    VkPipelineLayout layout{};
    ck(vkCreatePipelineLayout(c.dev, &pli, nullptr, &layout), "pipeline layout");
    VkDescriptorPoolSize ps{VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 2};
    VkDescriptorPoolCreateInfo dpi{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
    dpi.maxSets = 1;
    dpi.poolSizeCount = 1;
    dpi.pPoolSizes = &ps;
    VkDescriptorPool dpool{};
    ck(vkCreateDescriptorPool(c.dev, &dpi, nullptr, &dpool), "descriptor pool");
    VkDescriptorSetAllocateInfo dai{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
    dai.descriptorPool = dpool;
    dai.descriptorSetCount = 1;
    dai.pSetLayouts = &setLayout;
    VkDescriptorSet set{};
    ck(vkAllocateDescriptorSets(c.dev, &dai, &set), "descriptor set");
    VkDescriptorImageInfo ii[2]{{VK_NULL_HANDLE, src.v, VK_IMAGE_LAYOUT_GENERAL},
                                {VK_NULL_HANDLE, dst.v, VK_IMAGE_LAYOUT_GENERAL}};
    VkWriteDescriptorSet w[2]{};
    for (uint32_t i = 0; i < 2; ++i) {
        w[i].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        w[i].dstSet = set;
        w[i].dstBinding = i;
        w[i].descriptorCount = 1;
        w[i].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
        w[i].pImageInfo = &ii[i];
    }
    vkUpdateDescriptorSets(c.dev, 2, w, 0, nullptr);

    Pass passes[2];
    for (int i = 0; i < 2; ++i) {
        const auto code = readSpirv(argv[1 + i]);
        VkShaderModuleCreateInfo mi{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
        mi.codeSize = code.size() * 4;
        mi.pCode = code.data();
        ck(vkCreateShaderModule(c.dev, &mi, nullptr, &passes[i].module), "module");
        VkComputePipelineCreateInfo ci{VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO};
        ci.stage = {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0, VK_SHADER_STAGE_COMPUTE_BIT,
                    passes[i].module, "main", nullptr};
        ci.layout = layout;
        ck(vkCreateComputePipelines(c.dev, VK_NULL_HANDLE, 1, &ci, nullptr, &passes[i].pipeline), "pipeline");
    }
    VkQueryPoolCreateInfo qi{VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO};
    qi.queryType = VK_QUERY_TYPE_TIMESTAMP;
    qi.queryCount = 2;
    VkQueryPool queries{};
    ck(vkCreateQueryPool(c.dev, &qi, nullptr, &queries), "queries");
    VkCommandBufferAllocateInfo ai{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
    ai.commandPool = pool;
    ai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    ai.commandBufferCount = 1;
    VkCommandBuffer cmd{};
    ck(vkAllocateCommandBuffers(c.dev, &ai, &cmd), "cmd");
    auto barrier = [&](VkImage image, VkImageLayout from, VkImageLayout to, VkAccessFlags srcA, VkAccessFlags dstA) {
        VkImageMemoryBarrier m{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
        m.oldLayout = from;
        m.newLayout = to;
        m.srcAccessMask = srcA;
        m.dstAccessMask = dstA;
        m.srcQueueFamilyIndex = m.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        m.image = image;
        m.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
        vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, 0, 0,
                             nullptr, 0, nullptr, 1, &m);
    };
    auto run = [&](const std::function<void()>& body) {
        ck(vkResetCommandBuffer(cmd, 0), "reset");
        VkCommandBufferBeginInfo bi{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
        ck(vkBeginCommandBuffer(cmd, &bi), "begin");
        body();
        ck(vkEndCommandBuffer(cmd), "end");
        VkSubmitInfo si{VK_STRUCTURE_TYPE_SUBMIT_INFO};
        si.commandBufferCount = 1;
        si.pCommandBuffers = &cmd;
        ck(vkQueueSubmit(c.q, 1, &si, VK_NULL_HANDLE), "submit");
        ck(vkQueueWaitIdle(c.q), "wait");
    };
    std::memcpy(mapped, frame.data(), bytes);
    VkBufferImageCopy copy{};
    copy.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
    copy.imageExtent = {W, H, 1};
    run([&] {
        barrier(src.i, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 0, VK_ACCESS_TRANSFER_WRITE_BIT);
        vkCmdCopyBufferToImage(cmd, staging.b, src.i, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copy);
        barrier(src.i, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_GENERAL, VK_ACCESS_TRANSFER_WRITE_BIT,
                VK_ACCESS_SHADER_READ_BIT);
        barrier(dst.i, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL, 0, VK_ACCESS_SHADER_WRITE_BIT);
    });
    std::vector<std::vector<uint16_t>> outputs(2);
    for (int p = 0; p < 2; ++p) {
        std::vector<double> ms;
        for (int it = 0; it < iterations + 2; ++it) {
            run([&] {
                vkCmdResetQueryPool(cmd, queries, 0, 2);
                vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, passes[p].pipeline);
                vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, layout, 0, 1, &set, 0, nullptr);
                if (!push.empty())
                    vkCmdPushConstants(cmd, layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, uint32_t(push.size() * 4),
                                       push.data());
                vkCmdWriteTimestamp(cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, queries, 0);
                vkCmdDispatch(cmd, (W + local - 1) / local, (H + local - 1) / local, 1);
                vkCmdWriteTimestamp(cmd, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, queries, 1);
            });
            uint64_t t[2]{};
            ck(vkGetQueryPoolResults(c.dev, queries, 0, 2, sizeof(t), t, 8,
                                     VK_QUERY_RESULT_64_BIT | VK_QUERY_RESULT_WAIT_BIT), "timestamps");
            if (it >= 2) ms.push_back(double(t[1] - t[0]) * c.prop.limits.timestampPeriod / 1e6);
        }
        run([&] {
            barrier(dst.i, VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_ACCESS_SHADER_WRITE_BIT,
                    VK_ACCESS_TRANSFER_READ_BIT);
            vkCmdCopyImageToBuffer(cmd, dst.i, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, staging.b, 1, &copy);
            barrier(dst.i, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_IMAGE_LAYOUT_GENERAL, VK_ACCESS_TRANSFER_READ_BIT,
                    VK_ACCESS_SHADER_WRITE_BIT);
        });
        outputs[p].resize(bytes / 2);
        std::memcpy(outputs[p].data(), mapped, bytes);
        std::sort(ms.begin(), ms.end());
        std::printf("%s: median %.3f ms  min %.3f ms\n", p == 0 ? "old" : "new", ms[ms.size() / 2], ms.front());
    }
    size_t differing = 0, changedByPass = 0;
    float maxAbs = 0.0f;
    for (size_t i = 0; i < outputs[0].size(); ++i) {
        if (outputs[0][i] != frame[i]) ++changedByPass;
        if (outputs[0][i] == outputs[1][i]) continue;
        ++differing;
        maxAbs = std::max(maxAbs, std::fabs(halfToFloat(outputs[0][i]) - halfToFloat(outputs[1][i])));
    }
    std::printf("channels changed by old pass: %zu of %zu\n", changedByPass, outputs[0].size());
    std::printf("old vs new differing channels: %zu (max abs %.6g)\n", differing, maxAbs);
    return differing == 0 ? 0 : 1;
} catch (const std::exception& e) {
    std::cerr << "FAIL " << e.what() << "\n";
    return 2;
}
