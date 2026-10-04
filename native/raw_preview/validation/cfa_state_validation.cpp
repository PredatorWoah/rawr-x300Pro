#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

#include "../../vk_common/testing/vk_test_common.hpp"
#include "raw_preview/RawPreview.hpp"
using namespace vktest;

static int cfaChannel(raw_preview::BayerPattern p, int x, int y) {
    int ex = x & 1, ey = y & 1;
    switch (p) {
        case raw_preview::BayerPattern::RGGB:
            return ey == 0 ? (ex == 0 ? 0 : 1) : (ex == 0 ? 2 : 3);
        case raw_preview::BayerPattern::GRBG:
            return ey == 0 ? (ex == 0 ? 1 : 0) : (ex == 0 ? 3 : 2);
        case raw_preview::BayerPattern::GBRG:
            return ey == 0 ? (ex == 0 ? 2 : 3) : (ex == 0 ? 0 : 1);
        default:
            return ey == 0 ? (ex == 0 ? 3 : 2) : (ex == 0 ? 1 : 0);
    }
}
static const char* patternName(raw_preview::BayerPattern p) {
    return p == raw_preview::BayerPattern::RGGB   ? "RGGB"
           : p == raw_preview::BayerPattern::GRBG ? "GRBG"
           : p == raw_preview::BayerPattern::GBRG ? "GBRG"
                                                  : "BGGR";
}
static uint16_t expectedState(float raw, float normalized, int c, const raw_preview::RawFrameParameters& p) {
    uint16_t s = 0;
    if (normalized >= p.clipThreshold) s |= uint16_t(1u << c);
    if (normalized >= p.highlightWarningThreshold) s |= uint16_t(1u << (c + 4));
    if (normalized <= p.shadowWarningThreshold) s |= uint16_t(1u << (c + 8));
    if (raw <= p.blackLevel[c]) s |= uint16_t(1u << (c + 12));
    return s;
}
static uint16_t code(float n, int c, const raw_preview::RawFrameParameters& p) {
    n = std::max(0.f, std::min(1.f, n));
    return uint16_t(std::lround(p.blackLevel[c] + n * (p.whiteLevel - p.blackLevel[c])));
}
static void writeMaskPpm(const std::string& path, const std::vector<uint16_t>& mask, int W, int H) {
    std::ofstream f(path, std::ios::binary);
    f << "P6\n" << W << " " << H << "\n255\n";
    for (uint16_t s : mask) {
        bool hr = s & 0x0001, hg = s & 0x0006, hb = s & 0x0008;
        bool sr = s & 0x1000, sg = s & 0x6000, sb = s & 0x8000;
        bool hw = s & 0x00f0, sw = s & 0x0f00;
        unsigned char rgb[3] = {0, 0, 0};
        if (hr || hg || hb) {
            rgb[0] = hr ? 255 : 0;
            rgb[1] = hg ? 255 : 0;
            rgb[2] = hb ? 255 : 0;
        } else if (sr || sg || sb) {
            rgb[0] = sr ? 180 : 0;
            rgb[1] = sg ? 180 : 0;
            rgb[2] = sb ? 180 : 0;
        } else if (hw) {
            rgb[0] = 255;
            rgb[1] = 200;
            rgb[2] = 0;
        } else if (sw) {
            rgb[0] = 80;
            rgb[1] = 80;
            rgb[2] = 255;
        }
        f.write(reinterpret_cast<char*>(rgb), 3);
    }
}
int main(int argc, char** argv) {
    std::string shader = "./raw_preview.comp.spv", maskShader = "./raw_preview_cfa_state.comp.spv",
                outDir = "cfa_state_validation";
    int requestedW = 4080, requestedH = 3064;
    for (int i = 1; i < argc; i++) {
        std::string a = argv[i];
        if (a == "--shader" && i + 1 < argc)
            shader = argv[++i];
        else if (a == "--mask-shader" && i + 1 < argc)
            maskShader = argv[++i];
        else if (a == "--out" && i + 1 < argc)
            outDir = argv[++i];
        else if (a == "--width" && i + 1 < argc)
            requestedW = std::atoi(argv[++i]);
        else if (a == "--height" && i + 1 < argc)
            requestedH = std::atoi(argv[++i]);
    }
    std::system(("mkdir -p \"" + outDir + "\"").c_str());
    try {
        Ctx c = ctx();
        VkFormatProperties fp{};
        vkGetPhysicalDeviceFormatProperties(c.pd, VK_FORMAT_R16_UINT, &fp);
        if ((fp.optimalTilingFeatures & VK_FORMAT_FEATURE_STORAGE_IMAGE_BIT) == 0)
            throw std::runtime_error("VK_FORMAT_R16_UINT storage image unsupported");
        std::cout << "============================================================\nRAW_PREVIEW CFA STATE "
                     "VALIDATION\n============================================================\nGPU                  : "
                  << c.prop.deviceName << "\n";
        bool all = true;
        const int W = requestedW, H = requestedH, OW = W / 2, OH = H / 2;
        std::array<raw_preview::BayerPattern, 4> patterns = {
            raw_preview::BayerPattern::RGGB, raw_preview::BayerPattern::GRBG, raw_preview::BayerPattern::GBRG,
            raw_preview::BayerPattern::BGGR};
        for (auto pattern : patterns) {
            raw_preview::RawFrameParameters p{};
            p.pattern = pattern;
            p.blackLevel[0] = 64;
            p.blackLevel[1] = 72;
            p.blackLevel[2] = 80;
            p.blackLevel[3] = 96;
            p.whiteLevel = 4095;
            p.highlightWarningThreshold = .98f;
            p.shadowWarningThreshold = .01f;
            std::vector<uint16_t> raw(size_t(W) * H), expected(size_t(OW) * OH);
            for (int oy = 0; oy < OH; oy++)
                for (int ox = 0; ox < OW; ox++) {
                    int zone = (ox * 7) / OW;
                    float n[4] = {.50f, .50f, .50f, .50f};
                    if (zone == 1)
                        n[0] = .99f;  // R highlight warning
                    else if (zone == 2)
                        n[1] = 1.0f;  // G1 highlight clipped
                    else if (zone == 3)
                        n[3] = .005f;  // B shadow warning
                    else if (zone == 4)
                        n[2] = 0.0f;  // G2 shadow clipped
                    else if (zone == 5) {
                        n[0] = n[1] = n[2] = n[3] = 1.0f;
                    }  // all highlight clipped
                    else if (zone == 6) {
                        n[0] = n[1] = n[2] = n[3] = 0.0f;
                    }  // all shadow clipped
                    uint16_t state = 0;
                    for (int dy = 0; dy < 2; dy++)
                        for (int dx = 0; dx < 2; dx++) {
                            int X = ox * 2 + dx, Y = oy * 2 + dy, cfa = cfaChannel(pattern, X, Y);
                            uint16_t rv = code(n[cfa], cfa, p);
                            raw[size_t(Y) * W + X] = rv;
                            float norm = std::max(0.f, std::min(1.f, (float(rv) - p.blackLevel[cfa]) /
                                                                         (p.whiteLevel - p.blackLevel[cfa])));
                            state |= expectedState(float(rv), norm, cfa, p);
                        }
                    expected[size_t(oy) * OW + ox] = state;
                }
            VkDeviceSize rawBytes = raw.size() * 2, stateBytes = expected.size() * 2;
            Buf upload = mkBuf(c.pd, c.dev, rawBytes, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                               VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
            Buf download = mkBuf(c.pd, c.dev, stateBytes, VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                                 VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
            void* m = nullptr;
            ck(vkMapMemory(c.dev, upload.m, 0, rawBytes, 0, &m), "map");
            std::memcpy(m, raw.data(), rawBytes);
            vkUnmapMemory(c.dev, upload.m);
            Img input = mkImg(c.pd, c.dev, W, H, VK_FORMAT_R16_UINT,
                              VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_STORAGE_BIT);
            Img rgb = mkImg(c.pd, c.dev, OW, OH, VK_FORMAT_R16G16B16A16_SFLOAT, VK_IMAGE_USAGE_STORAGE_BIT);
            Img state = mkImg(c.pd, c.dev, OW, OH, VK_FORMAT_R16_UINT,
                              VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT);
            VkCommandPoolCreateInfo pci{};
            pci.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
            pci.queueFamilyIndex = c.qf;
            VkCommandPool pool{};
            ck(vkCreateCommandPool(c.dev, &pci, nullptr, &pool), "pool");
            VkCommandBufferAllocateInfo cai{};
            cai.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
            cai.commandPool = pool;
            cai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
            cai.commandBufferCount = 1;
            VkCommandBuffer cmd{};
            ck(vkAllocateCommandBuffers(c.dev, &cai, &cmd), "cmd");
            VkCommandBufferBeginInfo bi{};
            bi.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
            vkBeginCommandBuffer(cmd, &bi);
            VkImageMemoryBarrier ib{};
            ib.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
            ib.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
            ib.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
            ib.srcQueueFamilyIndex = ib.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            ib.image = input.i;
            ib.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
            ib.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
            vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr,
                                 0, nullptr, 1, &ib);
            VkBufferImageCopy copy{};
            copy.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
            copy.imageExtent = {(uint32_t)W, (uint32_t)H, 1};
            vkCmdCopyBufferToImage(cmd, upload.b, input.i, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copy);
            VkImageMemoryBarrier bars[3]{};
            for (auto& b : bars) {
                b.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
                b.srcQueueFamilyIndex = b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
                b.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
                b.newLayout = VK_IMAGE_LAYOUT_GENERAL;
            }
            bars[0].image = input.i;
            bars[0].oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
            bars[0].srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
            bars[0].dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
            bars[1].image = rgb.i;
            bars[1].oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
            bars[1].dstAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
            bars[2].image = state.i;
            bars[2].oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
            bars[2].dstAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
            vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 0,
                                 nullptr, 0, nullptr, 3, bars);
            raw_preview::RawPreviewCreateInfo ci{};
            ci.physicalDevice = c.pd;
            ci.device = c.dev;
            ci.shaderPath = shader;
            ci.cfaStateShaderPath = maskShader;
            // Highlight shaders default to the CFA-state shader's directory.
            raw_preview::RawPreview processor(ci);
            raw_preview::RawPreviewRecordInfo ri{};
            ri.commandBuffer = cmd;
            ri.inputRawR16UintView = input.v;
            ri.outputLinearRgba16fView = rgb.v;
            ri.outputCfaStateR16UintView = state.v;
            ri.width = W;
            ri.height = H;
            ri.parameters = p;
            processor.record(ri);
            VkImageMemoryBarrier sb{};
            sb.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
            sb.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
            sb.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
            sb.oldLayout = VK_IMAGE_LAYOUT_GENERAL;
            sb.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
            sb.srcQueueFamilyIndex = sb.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            sb.image = state.i;
            sb.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
            vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0,
                                 nullptr, 0, nullptr, 1, &sb);
            VkBufferImageCopy sc{};
            sc.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
            sc.imageExtent = {(uint32_t)OW, (uint32_t)OH, 1};
            vkCmdCopyImageToBuffer(cmd, state.i, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, download.b, 1, &sc);
            vkEndCommandBuffer(cmd);
            VkSubmitInfo si{};
            si.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
            si.commandBufferCount = 1;
            si.pCommandBuffers = &cmd;
            ck(vkQueueSubmit(c.q, 1, &si, VK_NULL_HANDLE), "submit");
            vkQueueWaitIdle(c.q);
            ck(vkMapMemory(c.dev, download.m, 0, stateBytes, 0, &m), "map state");
            auto* gpuState = (uint16_t*)m;
            size_t mismatch = 0;
            for (size_t i = 0; i < expected.size(); i++)
                if (gpuState[i] != expected[i]) mismatch++;
            std::vector<uint16_t> visual(gpuState, gpuState + expected.size());
            vkUnmapMemory(c.dev, download.m);
            bool pass = mismatch == 0;
            all &= pass;
            std::cout << W << "x" << H << " -> " << OW << "x" << OH << " " << patternName(pattern)
                      << " exact mask mismatches " << mismatch << "/" << expected.size() << "  "
                      << (pass ? "PASS" : "FAIL") << "\n";
            if (pattern == raw_preview::BayerPattern::RGGB)
                writeMaskPpm(outDir + "/cfa_state_rgba_debug.ppm", visual, OW, OH);
            vkDestroyCommandPool(c.dev, pool, nullptr);
            delImg(c.dev, state);
            delImg(c.dev, rgb);
            delImg(c.dev, input);
            delBuf(c.dev, download);
            delBuf(c.dev, upload);
        }
        std::cout << "PRODUCTION_CFA_GEOMETRY_PASS " << W << "x" << H << " -> " << OW << "x" << OH << "\n";
        std::cout << "CFA STATE GATE       : " << (all ? "PASS" : "FAIL") << "\n";
        delCtx(c);
        return all ? 0 : 3;
    } catch (const std::exception& e) {
        std::cerr << "ERROR: " << e.what() << "\n";
        return 2;
    }
}
