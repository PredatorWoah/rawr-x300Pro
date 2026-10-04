#pragma once
#include <cstdint>
#include <tonemap/TonemapMath.h>

namespace rawrcam::tonemap_integration {

enum class ColorRenderProfile : std::uint8_t {
    RawrBase = 0,
    UserLut = 1,
    SRgb = 2,
    Rec709 = 3,
    LogC3 = 4,
    SLog3 = 5,
    VLog = 6,
    FLog2C = 7,
    DaVinciIntermediate = 8,
};

void applyRenderProfile(ColorRenderProfile profile, tonemap::TonemapParams& params) noexcept;

[[nodiscard]] const char* colorRenderProfileName(ColorRenderProfile profile) noexcept;
[[nodiscard]] bool colorRenderProfileFromId(std::uint32_t id, ColorRenderProfile* out) noexcept;

}  // namespace rawrcam::tonemap_integration
