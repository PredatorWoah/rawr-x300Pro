#pragma once
#include <array>
#include <cstdint>
#include <vector>

#include "tonemap/lut/LutChain.h"

namespace tonemap::lut {

struct LutGpuStage {
    uint32_t texelOffset = 0;
    uint32_t size = 0;
    std::array<float, 3> domainMin{0.0f, 0.0f, 0.0f};
    std::array<float, 3> domainMax{1.0f, 1.0f, 1.0f};
};

struct LutGpuPayload {
    bool enabled = false;
    uint32_t stageCount = 0;
    color::ColorSpace inputSpace{};
    color::ColorSpace outputSpace{};
    LutPlacement placement = LutPlacement::PostRender;
    LutAfterAction afterAction = LutAfterAction::ConvertToSrgb;
    float intensity = 0.0f;
    std::array<LutGpuStage, kMaxGpuLutStages> stages{};
    std::vector<std::array<float, 4>> rgbaTexels;
};

LutGpuPayload packGpuPayload(const LutChain* chain);
color::Vec3 samplePackedStageTetrahedral(const LutGpuPayload& payload, uint32_t stageIndex, color::Vec3 input);
color::Vec3 evaluatePackedStages(const LutGpuPayload& payload, color::Vec3 input);

}  // namespace tonemap::lut
