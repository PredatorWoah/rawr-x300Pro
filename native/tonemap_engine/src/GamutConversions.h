#pragma once

#include <array>
#include "tonemap/lut/LutGpuPayload.h"

namespace tonemap::detail {
using ColumnMatrix3 = std::array<float, 9>;
using ColumnMatrix4 = std::array<float, 16>;
#define RAWR_GAMUT_MATRIX(name, ...) inline constexpr ColumnMatrix3 name{{__VA_ARGS__}};
#include "../shaders/color/gamut_matrices.inc"
#undef RAWR_GAMUT_MATRIX
inline constexpr ColumnMatrix3 identity3{{1, 0, 0, 0, 1, 0, 0, 0, 1}};

inline const ColumnMatrix3& toSrgb(color::Gamut gamut) {
    switch (gamut) {
        case color::Gamut::SRgbRec709: return identity3;
        case color::Gamut::AcesCgAp1: return RAWR_AP1_TO_SRGB;
        case color::Gamut::DaVinciWideGamut: return RAWR_DWG_TO_SRGB;
        case color::Gamut::Rec2020: return RAWR_REC2020_TO_SRGB;
        case color::Gamut::ArriWideGamut3: return RAWR_ARRI_WG3_TO_SRGB;
        case color::Gamut::SonySGamut3Cine: return RAWR_SONY_SGAMUT3CINE_TO_SRGB;
        case color::Gamut::FujifilmFGamutC: return RAWR_FUJIFILM_FGAMUT_C_TO_SRGB;
        default: return RAWR_PANASONIC_VGAMUT_TO_SRGB;
    }
}
inline const ColumnMatrix3& fromSrgb(color::Gamut gamut) {
    switch (gamut) {
        case color::Gamut::SRgbRec709: return identity3;
        case color::Gamut::AcesCgAp1: return RAWR_SRGB_TO_AP1;
        case color::Gamut::DaVinciWideGamut: return RAWR_SRGB_TO_DWG;
        case color::Gamut::Rec2020: return RAWR_SRGB_TO_REC2020;
        case color::Gamut::ArriWideGamut3: return RAWR_SRGB_TO_ARRI_WG3;
        case color::Gamut::SonySGamut3Cine: return RAWR_SRGB_TO_SONY_SGAMUT3CINE;
        case color::Gamut::FujifilmFGamutC: return RAWR_SRGB_TO_FUJIFILM_FGAMUT_C;
        default: return RAWR_SRGB_TO_PANASONIC_VGAMUT;
    }
}
inline ColumnMatrix4 directGamutMatrix(color::Gamut source, color::Gamut destination) {
    ColumnMatrix4 result{};
    result[15] = 1;
    const auto& a = fromSrgb(destination);
    const auto& b = toSrgb(source);
    for (int col = 0; col < 3; ++col) {
        for (int row = 0; row < 3; ++row) {
            if (source == destination) {
                result[col * 4 + row] = col == row ? 1.f : 0.f;
            } else if (source == color::Gamut::SRgbRec709) {
                result[col * 4 + row] = a[col * 3 + row];
            } else if (destination == color::Gamut::SRgbRec709) {
                result[col * 4 + row] = b[col * 3 + row];
            } else {
                // Compose once in double precision, then round uploaded coefficients to float.
                double value = 0;
                for (int k = 0; k < 3; ++k) value += double(a[k * 3 + row]) * b[col * 3 + k];
                result[col * 4 + row] = static_cast<float>(value);
            }
        }
    }
    return result;
}
struct GamutConversions {
    ColumnMatrix4 neutral, userInput, userOutput;
    bool combineUser = true;
    bool combineNeutral = true;
    explicit GamutConversions(const lut::LutGpuPayload& payload)
        : neutral(directGamutMatrix(color::Gamut::AcesCgAp1, color::Gamut::DaVinciWideGamut)),
          userInput(directGamutMatrix(payload.placement == lut::LutPlacement::RenderTransform
                                         ? color::Gamut::AcesCgAp1 : color::Gamut::SRgbRec709,
                                     payload.inputSpace.gamut)),
          userOutput(directGamutMatrix(payload.outputSpace.gamut, color::Gamut::SRgbRec709)) {
        // Tiny input rounding can be amplified by custom domains or extended
        // LUT values followed by log decoding. Preserve the original arithmetic
        // for those chains; their spaces, domains and range remain supported.
        // Decoding a declared log output can greatly amplify input rounding,
        // even for a unit-domain LUT. Keep its original boundary arithmetic.
        const auto tf = payload.outputSpace.transfer;
        if (payload.afterAction == lut::LutAfterAction::ConvertToSrgb &&
            (tf == color::TransferFunction::DaVinciIntermediate || tf == color::TransferFunction::LogC3 ||
             tf == color::TransferFunction::SLog3 || tf == color::TransferFunction::VLog || tf == color::TransferFunction::FLog2C))
            combineUser = false;
        for (uint32_t i = 0; i < payload.stageCount; ++i)
            for (int channel = 0; channel < 3; ++channel)
                if (payload.stages[i].domainMin[channel] != 0.f || payload.stages[i].domainMax[channel] != 1.f)
                    combineUser = false;
        for (const auto& texel : payload.rgbaTexels)
            for (int channel = 0; channel < 3; ++channel)
                if (texel[channel] < 0.f || texel[channel] > 1.f) combineUser = false;
        // Post-render LUTs consume Neutral's result. Preserve that arithmetic
        // too when the downstream chain can amplify its rounding differences.
        combineNeutral = !(payload.enabled && payload.intensity > 0.f && payload.placement == lut::LutPlacement::PostRender && !combineUser);
    }
};
}  // namespace tonemap::detail
