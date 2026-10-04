#include "tonemap/lut/LutGpuPayload.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>

namespace tonemap::lut {
namespace {
color::Vec3 add(color::Vec3 a, color::Vec3 b) { return {a[0] + b[0], a[1] + b[1], a[2] + b[2]}; }
color::Vec3 sub(color::Vec3 a, color::Vec3 b) { return {a[0] - b[0], a[1] - b[1], a[2] - b[2]}; }
color::Vec3 mul(color::Vec3 a, float s) { return {a[0] * s, a[1] * s, a[2] * s}; }
color::Vec3 madd(color::Vec3 a, color::Vec3 b, float s) { return add(a, mul(b, s)); }
}  // namespace

LutGpuPayload packGpuPayload(const LutChain* chain) {
    LutGpuPayload out{};
    // Vulkan descriptors cannot be left unbound on the target contract, so
    // disabled mode still carries one harmless texel in the backing SSBO.
    out.rgbaTexels.push_back({0.0f, 0.0f, 0.0f, 0.0f});
    if (!chain) return out;
    if (chain->stages.empty()) throw std::invalid_argument("LUT chain must contain at least one stage");
    if (chain->stages.size() > kMaxGpuLutStages) throw std::invalid_argument("LUT chain exceeds GPU stage limit");
    if (!std::isfinite(chain->intensity)) throw std::invalid_argument("LUT intensity is non-finite");

    out.enabled = true;
    out.stageCount = static_cast<uint32_t>(chain->stages.size());
    out.inputSpace = chain->inputSpace;
    out.outputSpace = chain->outputSpace;
    out.placement = chain->placement;
    out.afterAction = chain->afterAction;
    out.intensity = std::clamp(chain->intensity, 0.0f, 1.0f);
    out.rgbaTexels.clear();

    uint64_t totalTexels = 0;
    for (uint32_t i = 0; i < out.stageCount; ++i) {
        const Lut3D& src = chain->stages[i];
        if (src.size < 2 || src.size > 65) throw std::invalid_argument("unsupported LUT dimension");
        const uint64_t expected = uint64_t(src.size) * src.size * src.size;
        if (src.values.size() != expected) throw std::invalid_argument("LUT table length mismatch");
        if (totalTexels + expected > std::numeric_limits<uint32_t>::max())
            throw std::invalid_argument("LUT chain texel count exceeds 32-bit offset range");
        LutGpuStage& dst = out.stages[i];
        dst.texelOffset = static_cast<uint32_t>(totalTexels);
        dst.size = src.size;
        dst.domainMin = src.domainMin;
        dst.domainMax = src.domainMax;
        for (int c = 0; c < 3; ++c) {
            if (!std::isfinite(dst.domainMin[c]) || !std::isfinite(dst.domainMax[c]) ||
                !(dst.domainMax[c] > dst.domainMin[c]))
                throw std::invalid_argument("invalid LUT domain");
        }
        out.rgbaTexels.reserve(static_cast<size_t>(totalTexels + expected));
        for (const auto& v : src.values) {
            if (!std::isfinite(v[0]) || !std::isfinite(v[1]) || !std::isfinite(v[2]))
                throw std::invalid_argument("non-finite LUT value");
            out.rgbaTexels.push_back({v[0], v[1], v[2], 0.0f});
        }
        totalTexels += expected;
    }
    return out;
}

color::Vec3 samplePackedStageTetrahedral(const LutGpuPayload& payload, uint32_t stageIndex, color::Vec3 input) {
    if (!payload.enabled || stageIndex >= payload.stageCount) return input;
    const LutGpuStage& s = payload.stages[stageIndex];
    const uint32_t n = s.size;
    float p[3]{};
    uint32_t i0[3]{}, i1[3]{};
    float f[3]{};
    for (int c = 0; c < 3; ++c) {
        const float q = std::clamp((input[c] - s.domainMin[c]) / (s.domainMax[c] - s.domainMin[c]), 0.0f, 1.0f);
        p[c] = q * float(n - 1);
        i0[c] = static_cast<uint32_t>(std::floor(p[c]));
        i1[c] = std::min(i0[c] + 1, n - 1);
        f[c] = p[c] - float(i0[c]);
    }
    auto fetch = [&](uint32_t r, uint32_t g, uint32_t b) {
        const uint32_t index = s.texelOffset + r + n * (g + n * b);
        const auto& v = payload.rgbaTexels.at(index);
        return color::Vec3{v[0], v[1], v[2]};
    };
    const auto c000 = fetch(i0[0], i0[1], i0[2]);
    color::Vec3 o{};
    if (f[0] >= f[1]) {
        if (f[1] >= f[2]) {
            const auto c100 = fetch(i1[0], i0[1], i0[2]), c110 = fetch(i1[0], i1[1], i0[2]),
                       c111 = fetch(i1[0], i1[1], i1[2]);
            o = madd(madd(madd(c000, sub(c100, c000), f[0]), sub(c110, c100), f[1]), sub(c111, c110), f[2]);
        } else if (f[0] >= f[2]) {
            const auto c100 = fetch(i1[0], i0[1], i0[2]), c101 = fetch(i1[0], i0[1], i1[2]),
                       c111 = fetch(i1[0], i1[1], i1[2]);
            o = madd(madd(madd(c000, sub(c100, c000), f[0]), sub(c101, c100), f[2]), sub(c111, c101), f[1]);
        } else {
            const auto c001 = fetch(i0[0], i0[1], i1[2]), c101 = fetch(i1[0], i0[1], i1[2]),
                       c111 = fetch(i1[0], i1[1], i1[2]);
            o = madd(madd(madd(c000, sub(c001, c000), f[2]), sub(c101, c001), f[0]), sub(c111, c101), f[1]);
        }
    } else {
        if (f[0] >= f[2]) {
            const auto c010 = fetch(i0[0], i1[1], i0[2]), c110 = fetch(i1[0], i1[1], i0[2]),
                       c111 = fetch(i1[0], i1[1], i1[2]);
            o = madd(madd(madd(c000, sub(c010, c000), f[1]), sub(c110, c010), f[0]), sub(c111, c110), f[2]);
        } else if (f[1] >= f[2]) {
            const auto c010 = fetch(i0[0], i1[1], i0[2]), c011 = fetch(i0[0], i1[1], i1[2]),
                       c111 = fetch(i1[0], i1[1], i1[2]);
            o = madd(madd(madd(c000, sub(c010, c000), f[1]), sub(c011, c010), f[2]), sub(c111, c011), f[0]);
        } else {
            const auto c001 = fetch(i0[0], i0[1], i1[2]), c011 = fetch(i0[0], i1[1], i1[2]),
                       c111 = fetch(i1[0], i1[1], i1[2]);
            o = madd(madd(madd(c000, sub(c001, c000), f[2]), sub(c011, c001), f[1]), sub(c111, c011), f[0]);
        }
    }
    return o;
}

color::Vec3 evaluatePackedStages(const LutGpuPayload& payload, color::Vec3 input) {
    auto out = input;
    for (uint32_t i = 0; i < payload.stageCount; ++i) out = samplePackedStageTetrahedral(payload, i, out);
    return out;
}

}  // namespace tonemap::lut
