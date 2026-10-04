#include <algorithm>
#include <array>
#include <cmath>
#include <cstdlib>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <numeric>
#include <string>
#include <vector>

#include "../../vk_common/testing/vk_test_common.hpp"
#include "raw_preview/RawPreview.hpp"
using namespace vktest;
struct ReferenceCell {
    float r, g, b;
    unsigned mask, clipState;
};
static float cl(float x) { return std::max(0.f, std::min(1.f, x)); }
static int cf(raw_preview::BayerPattern p, int x, int y) {
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
static const char* pn(raw_preview::BayerPattern p) {
    return p == raw_preview::BayerPattern::RGGB   ? "RGGB"
           : p == raw_preview::BayerPattern::GRBG ? "GRBG"
           : p == raw_preview::BayerPattern::GBRG ? "GBRG"
                                                  : "BGGR";
}
struct Scene {
    std::vector<float> rgb;
    std::vector<uint16_t> raw;
    std::vector<unsigned char> recoverable;
};
static Scene makeScene(int W, int H, const raw_preview::RawFrameParameters& p, int variant) {
    int OW = W / 2, OH = H / 2;
    Scene s;
    s.rgb.resize(size_t(OW) * OH * 3);
    s.raw.resize(size_t(W) * H);
    s.recoverable.resize(size_t(OW) * OH);
    for (int oy = 0; oy < OH; oy++)
        for (int ox = 0; ox < OW; ox++) {
            float x = (ox + .5f) / OW, y = (oy + .5f) / OH;
            float lum = 0.15f + 1.85f * std::pow(x, 1.35f);
            float edge = x > (0.46f + 0.06f * std::sin(y * 18.f)) ? 1.f : 0.f;
            float rr, gg, bb;
            if (variant == 0) {
                rr = lum * (0.92f + 0.08f * y);
                gg = lum;
                bb = lum * (0.88f + 0.12f * x);
            } else if (variant == 1) {
                rr = lum * (1.55f - 0.25f * y);
                gg = lum * .82f;
                bb = lum * .48f;
            } else {
                rr = lum * (edge ? 0.55f : 1.35f);
                gg = lum * (edge ? 1.15f : .72f);
                bb = lum * (edge ? 1.45f : .62f);
            }
            size_t oi = (size_t(oy) * OW + ox) * 3;
            s.rgb[oi] = rr;
            s.rgb[oi + 1] = gg;
            s.rgb[oi + 2] = bb;
            bool anyValid = false, anyClip = false;
            for (int dy = 0; dy < 2; dy++)
                for (int dx = 0; dx < 2; dx++) {
                    int X = ox * 2 + dx, Y = oy * 2 + dy, c = cf(p.pattern, X, Y);
                    float v = c == 0 ? rr : (c == 3 ? bb : gg);
                    anyValid |= v < p.clipThreshold;
                    anyClip |= v >= p.clipThreshold;
                    float q = cl(v);
                    float raw = p.blackLevel[c] + q * (p.whiteLevel - p.blackLevel[c]);
                    s.raw[size_t(Y) * W + X] = (uint16_t)std::lround(raw);
                }
            s.recoverable[size_t(oy) * OW + ox] = (anyClip && anyValid) ? 1 : 0;
        }
    return s;
}
static ReferenceCell loadReferenceCell(const std::vector<uint16_t>& raw, int W, int H, int cx, int cy,
                                       const raw_preview::RawFrameParameters& p) {
    int OW = W / 2, OH = H / 2;
    cx = std::max(0, std::min(OW - 1, cx));
    cy = std::max(0, std::min(OH - 1, cy));
    int bx = cx * 2, by = cy * 2;
    auto n = [&](int x, int y, int c) {
        return cl((float(raw[size_t(y) * W + x]) - p.blackLevel[c]) / std::max(1.f, p.whiteLevel - p.blackLevel[c]));
    };
    float r, g1, g2, b;
    if (p.pattern == raw_preview::BayerPattern::RGGB) {
        r = n(bx, by, 0);
        g1 = n(bx + 1, by, 1);
        g2 = n(bx, by + 1, 2);
        b = n(bx + 1, by + 1, 3);
    } else if (p.pattern == raw_preview::BayerPattern::GRBG) {
        g1 = n(bx, by, 1);
        r = n(bx + 1, by, 0);
        b = n(bx, by + 1, 3);
        g2 = n(bx + 1, by + 1, 2);
    } else if (p.pattern == raw_preview::BayerPattern::GBRG) {
        g2 = n(bx, by, 2);
        b = n(bx + 1, by, 3);
        r = n(bx, by + 1, 0);
        g1 = n(bx + 1, by + 1, 1);
    } else {
        b = n(bx, by, 3);
        g2 = n(bx + 1, by, 2);
        g1 = n(bx, by + 1, 1);
        r = n(bx + 1, by + 1, 0);
    }
    bool vr = r < p.clipThreshold, v1 = g1 < p.clipThreshold, v2 = g2 < p.clipThreshold, vb = b < p.clipThreshold;
    float g = .5f * (g1 + g2);
    unsigned clip = (!vr ? 1u : 0u) | (!v1 ? 2u : 0u) | (!v2 ? 4u : 0u) | (!vb ? 8u : 0u);
    return {r, g, b, (vr ? 1u : 0u) | ((v1 || v2) ? 2u : 0u) | (vb ? 4u : 0u), clip};
}
static bool isValid(ReferenceCell c, int ch) { return (c.mask & (1u << ch)) != 0; }
static float ew(float a, float b, float k) { return 1.f / (1.f + k * std::abs(a - b)); }

static std::array<float, 3> clipRgb(unsigned state) {
    return {(state & 1u) ? 1.f : 0.f, .5f * float(((state & 2u) ? 1 : 0) + ((state & 4u) ? 1 : 0)),
            (state & 8u) ? 1.f : 0.f};
}
static std::array<float, 3> localClipInfluence(const Scene& s, int W, int H, int x, int y,
                                               const raw_preview::RawFrameParameters& p) {
    static constexpr float k[3] = {1, 2, 1};
    std::array<float, 3> sum{};
    for (int dy = -1; dy <= 1; dy++)
        for (int dx = -1; dx <= 1; dx++) {
            auto q = clipRgb(loadReferenceCell(s.raw, W, H, x + dx, y + dy, p).clipState);
            float w = k[dx + 1] * k[dy + 1];
            for (int c = 0; c < 3; c++) sum[c] += w * q[c];
        }
    auto center = clipRgb(loadReferenceCell(s.raw, W, H, x, y, p).clipState);
    for (int c = 0; c < 3; c++) {
        float density = std::clamp(sum[c] / 16.f, 0.f, 1.f);
        float inside = std::sqrt(center[c]) * (.78f + .22f * std::sqrt(density));
        sum[c] = center[c] > 1e-6f ? inside : .30f * density;
    }
    return sum;
}
static std::array<float, 3> recoverPostWb(const raw_preview::RawFrameParameters& p, const std::array<float, 3>& center,
                                          unsigned validMask, const std::array<float, 3>&) {
    if (!p.highlightReconstructionEnabled) {
        float gw = .5f * (p.whiteBalance[1] + p.whiteBalance[2]);
        float ceiling = std::min(p.whiteBalance[0], std::min(gw, p.whiteBalance[3]));
        return {std::min(center[0], ceiling), std::min(center[1], ceiling), std::min(center[2], ceiling)};
    }
    if (validMask == 7u) return center;
    if (validMask == 0u) {
        float target = std::max(center[0], std::max(center[1], center[2]));
        return {target, target, target};
    }
    std::array<float, 3> root{std::cbrt(std::max(center[0], 0.f)), std::cbrt(std::max(center[1], 0.f)),
                              std::cbrt(std::max(center[2], 0.f))};
    std::array<float, 3> opposedRoot{.5f * (root[1] + root[2]), .5f * (root[0] + root[2]), .5f * (root[0] + root[1])};
    std::array<float, 3> result = center;
    for (int ch = 0; ch < 3; ch++)
        if ((validMask & (1u << ch)) == 0u)
            result[ch] = std::max(center[ch], opposedRoot[ch] * opposedRoot[ch] * opposedRoot[ch]);
    for (float& v : result) v = std::clamp(std::isfinite(v) ? v : 0.f, 0.f, 60000.f);
    return result;
}
static std::vector<float> cpu(const Scene& s, int W, int H, const raw_preview::RawFrameParameters& p) {
    int OW = W / 2, OH = H / 2;
    std::vector<float> o(size_t(OW) * OH * 4);
    const int d[4][2] = {{0, -1}, {-1, 0}, {1, 0}, {0, 1}};
    for (int y = 0; y < OH; y++)
        for (int x = 0; x < OW; x++) {
            ReferenceCell rawc = loadReferenceCell(s.raw, W, H, x, y, p);
            unsigned om = rawc.mask;
            ReferenceCell c = rawc;
            float r0 = c.r - c.g, b0 = c.b - c.g, rd = r0, bd = b0;
            if (om == 7 && p.chromaBlend > 0) {
                float rs = 0, rw = 0, bs = 0, bw = 0;
                for (auto& q : d) {
                    ReferenceCell n = loadReferenceCell(s.raw, W, H, x + q[0], y + q[1], p);
                    float w = ew(c.g, n.g, p.edgeStrength);
                    if (isValid(n, 0) && isValid(n, 1)) {
                        rs += w * (n.r - n.g);
                        rw += w;
                    }
                    if (isValid(n, 2) && isValid(n, 1)) {
                        bs += w * (n.b - n.g);
                        bw += w;
                    }
                }
                if (rw > 1e-6f) rd = r0 * (1 - p.chromaBlend) + (rs / rw) * p.chromaBlend;
                if (bw > 1e-6f) bd = b0 * (1 - p.chromaBlend) + (bs / bw) * p.chromaBlend;
            }
            float gw = .5f * (p.whiteBalance[1] + p.whiteBalance[2]);
            std::array<float, 3> rgb{std::max(0.f, c.g + rd) * p.whiteBalance[0], c.g * gw,
                                     std::max(0.f, c.g + bd) * p.whiteBalance[3]};
            rgb = recoverPostWb(
                p, rgb, om,
                p.highlightReconstructionEnabled ? localClipInfluence(s, W, H, x, y, p) : std::array<float, 3>{});
            size_t i = (size_t(y) * OW + x) * 4;
            o[i] = rgb[0];
            o[i + 1] = rgb[1];
            o[i + 2] = rgb[2];
            o[i + 3] = 1;
        }
    return o;
}

static std::vector<float> gpu(Ctx& c, const std::string& shader, const Scene& s, int W, int H,
                              const raw_preview::RawFrameParameters& p) {
    int OW = W / 2, OH = H / 2;
    VkDeviceSize rb = s.raw.size() * 2, ob = VkDeviceSize(size_t(OW) * OH * 8);
    Buf up = mkBuf(c.pd, c.dev, rb, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                   VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT),
        down = mkBuf(c.pd, c.dev, ob, VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                     VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    void* m;
    ck(vkMapMemory(c.dev, up.m, 0, rb, 0, &m), "map");
    std::memcpy(m, s.raw.data(), rb);
    vkUnmapMemory(c.dev, up.m);
    Img in = mkImg(c.pd, c.dev, W, H, VK_FORMAT_R16_UINT, VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_STORAGE_BIT),
        out = mkImg(c.pd, c.dev, OW, OH, VK_FORMAT_R16G16B16A16_SFLOAT,
                    VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT);
    VkCommandPoolCreateInfo pi{};
    pi.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    pi.queueFamilyIndex = c.qf;
    VkCommandPool pool;
    ck(vkCreateCommandPool(c.dev, &pi, nullptr, &pool), "pool");
    VkCommandBufferAllocateInfo ai{};
    ai.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    ai.commandPool = pool;
    ai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    ai.commandBufferCount = 1;
    VkCommandBuffer cmd;
    ck(vkAllocateCommandBuffers(c.dev, &ai, &cmd), "cmd");
    VkCommandBufferBeginInfo bi{};
    bi.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    vkBeginCommandBuffer(cmd, &bi);
    VkImageMemoryBarrier a{};
    a.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    a.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    a.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    a.srcQueueFamilyIndex = a.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    a.image = in.i;
    a.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    a.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0,
                         nullptr, 1, &a);
    VkBufferImageCopy bc{};
    bc.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
    bc.imageExtent = {(uint32_t)W, (uint32_t)H, 1};
    vkCmdCopyBufferToImage(cmd, up.b, in.i, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &bc);
    VkImageMemoryBarrier bars[2]{};
    bars[0].sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    bars[0].srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    bars[0].dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    bars[0].oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    bars[0].newLayout = VK_IMAGE_LAYOUT_GENERAL;
    bars[0].srcQueueFamilyIndex = bars[0].dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    bars[0].image = in.i;
    bars[0].subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    bars[1].sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    bars[1].oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    bars[1].newLayout = VK_IMAGE_LAYOUT_GENERAL;
    bars[1].srcQueueFamilyIndex = bars[1].dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    bars[1].image = out.i;
    bars[1].subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    bars[1].dstAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 0, nullptr, 0,
                         nullptr, 2, bars);
    raw_preview::RawPreviewCreateInfo ci{};
    ci.physicalDevice = c.pd;
    ci.device = c.dev;
    ci.shaderPath = shader;
    raw_preview::RawPreview pr(ci);
    raw_preview::RawPreviewRecordInfo ri{};
    ri.commandBuffer = cmd;
    ri.inputRawR16UintView = in.v;
    ri.outputLinearRgba16fView = out.v;
    ri.width = W;
    ri.height = H;
    ri.parameters = p;
    pr.record(ri);
    VkImageMemoryBarrier co{};
    co.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    co.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
    co.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
    co.oldLayout = VK_IMAGE_LAYOUT_GENERAL;
    co.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    co.srcQueueFamilyIndex = co.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    co.image = out.i;
    co.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0,
                         nullptr, 1, &co);
    VkBufferImageCopy bo{};
    bo.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
    bo.imageExtent = {(uint32_t)OW, (uint32_t)OH, 1};
    vkCmdCopyImageToBuffer(cmd, out.i, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, down.b, 1, &bo);
    vkEndCommandBuffer(cmd);
    VkSubmitInfo si{};
    si.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    si.commandBufferCount = 1;
    si.pCommandBuffers = &cmd;
    ck(vkQueueSubmit(c.q, 1, &si, VK_NULL_HANDLE), "submit");
    vkQueueWaitIdle(c.q);
    ck(vkMapMemory(c.dev, down.m, 0, ob, 0, &m), "map down");
    auto* h = (uint16_t*)m;
    std::vector<float> v(size_t(OW) * OH * 4);
    for (size_t i = 0; i < v.size(); i++) v[i] = halfToFloat(h[i]);
    vkUnmapMemory(c.dev, down.m);
    vkDestroyCommandPool(c.dev, pool, nullptr);
    delImg(c.dev, out);
    delImg(c.dev, in);
    delBuf(c.dev, down);
    delBuf(c.dev, up);
    return v;
}
int main(int argc, char** argv) {
    std::string shader = "./raw_preview.comp.spv", od = "validation_out";
    for (int i = 1; i < argc; i++) {
        std::string a = argv[i];
        if (a == "--shader" && i + 1 < argc)
            shader = argv[++i];
        else if (a == "--out" && i + 1 < argc)
            od = argv[++i];
    }
    std::system(("mkdir -p \"" + od + "\"").c_str());
    try {
        Ctx c = ctx();
        std::cout
            << "============================================================\nRAW_PREVIEW 1.2.0 PRODUCTION GEOMETRY "
               "VALIDATION\n============================================================\nGPU                  : "
            << c.prop.deviceName << "\n";
        bool all = true;
        std::array<raw_preview::BayerPattern, 4> pats = {
            raw_preview::BayerPattern::RGGB, raw_preview::BayerPattern::GRBG, raw_preview::BayerPattern::GBRG,
            raw_preview::BayerPattern::BGGR};
        std::array<std::pair<int, int>, 4> dims = {{{4096, 3072}, {4080, 3064}, {4080, 3072}, {640, 480}}};
        for (auto d : dims) {
            int W = d.first, H = d.second, OW = W / 2, OH = H / 2;
            int variants = (W == 640 ? 3 : 1);
            for (auto pat : pats)
                for (int variant = 0; variant < variants; variant++) {
                    raw_preview::RawFrameParameters p{};
                    p.pattern = pat;
                    p.blackLevel[0] = 64;
                    p.blackLevel[1] = 72;
                    p.blackLevel[2] = 80;
                    p.blackLevel[3] = 96;
                    p.whiteLevel = 4095;
                    p.whiteBalance[0] = 1.31f;
                    p.whiteBalance[1] = 1.0f;
                    p.whiteBalance[2] = 1.02f;
                    p.whiteBalance[3] = 1.47f;
                    p.clipThreshold = .995f;
                    p.edgeStrength = 8;
                    p.chromaBlend = .2f;
                    p.highlightReconstructionEnabled = false;
                    Scene scn = makeScene(W, H, p, variant);
                    auto cr = cpu(scn, W, H, p), gr = gpu(c, shader, scn, W, H, p);
                    double ss = 0, ma = 0;
                    float maxGpu = 0;
                    for (size_t i = 0; i < gr.size(); i++) {
                        double e = std::abs(gr[i] - cr[i]);
                        ss += e * e;
                        ma = std::max(ma, e);
                        maxGpu = std::max(maxGpu, gr[i]);
                    }
                    double rm = std::sqrt(ss / gr.size());
                    bool aboveWhite = (W == 640) || (maxGpu > 1.0f);
                    bool parity = ma <= .004 && rm <= .0008 && aboveWhite;
                    all &= parity;
                    std::cout << W << "x" << H << " -> " << OW << "x" << OH << " " << pn(pat) << " scene" << variant
                              << " GPU/CPU max " << std::scientific << ma << " rmse " << rm << " maxGPU " << maxGpu
                              << " above1 " << (aboveWhite ? "YES" : "NO") << "  " << (parity ? "PASS" : "FAIL")
                              << "\n";
                }
            if ((W == 4096 && H == 3072) || (W == 4080 && (H == 3064 || H == 3072)))
                std::cout << "PRODUCTION_RGB_GEOMETRY_PASS " << W << "x" << H << " -> " << OW << "x" << OH << "\n";
        }
        std::cout << "VALIDATION GATE     : " << (all ? "PASS" : "FAIL") << "\n";
        delCtx(c);
        return all ? 0 : 3;
    } catch (const std::exception& e) {
        std::cerr << "ERROR: " << e.what() << "\n";
        return 2;
    }
}
