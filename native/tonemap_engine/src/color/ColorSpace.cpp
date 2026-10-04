#include "tonemap/color/ColorSpace.h"

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace tonemap::color {
namespace {
constexpr float DI_A = 0.0075f;
constexpr float DI_B = 7.0f;
constexpr float DI_C = 0.07329248f;
constexpr float DI_M = 10.44426855f;
constexpr float DI_LIN_CUT = 0.00262409f;
constexpr float DI_LOG_CUT = 0.02740668f;
constexpr float REC2020_ALPHA = 1.09929682680944f;
constexpr float REC2020_BETA = 0.018053968510807f;
constexpr float REC2020_ENCODED_CUT = 4.5f * REC2020_BETA;
constexpr float FLOG2C_A = 5.555556f;
constexpr float FLOG2C_B = 0.064829f;
constexpr float FLOG2C_C = 0.245281f;
constexpr float FLOG2C_D = 0.384316f;
constexpr float FLOG2C_E = 8.799461f;
constexpr float FLOG2C_F = 0.092864f;
constexpr float FLOG2C_LIN_CUT = 0.000889f;
constexpr float FLOG2C_LOG_CUT = 0.100686685370811f;

constexpr Mat3 IDENTITY{1, 0, 0, 0, 1, 0, 0, 0, 1};
// AP1(D60) -> linear sRGB(D65), including the same D60->D65 adaptation used by Rawr V15.
constexpr Mat3 AP1_TO_SRGB{1.70505154f,  -0.62179068f, -0.08325840f, -0.13025714f, 1.14080269f,
                           -0.01054853f, -0.02400328f, -0.12896877f, 1.15297184f};
constexpr Mat3 SRGB_TO_AP1{0.61313242f, 0.33953802f, 0.04741670f, 0.07012438f, 0.91639401f,
                           0.01345152f, 0.02058766f, 0.10957457f, 0.86978540f};
constexpr Mat3 DWG_TO_SRGB{1.89831314f,  -0.79205028f, -0.10642184f, -0.16895292f, 1.48901017f,
                           -0.32003433f, -0.12156831f, -0.31575156f, 1.43755990f};
// Linear Rec.2020 (D65) <-> linear sRGB/Rec.709 (D65).
constexpr Mat3 REC2020_TO_SRGB{1.66049100f,  -0.58764114f, -0.07284986f, -0.12455047f, 1.13289990f,
                               -0.00834942f, -0.01815076f, -0.10057890f, 1.11872966f};
constexpr Mat3 SRGB_TO_REC2020{0.62740390f, 0.32928304f, 0.04331307f, 0.06909729f, 0.91954040f,
                               0.01136232f, 0.01639144f, 0.08801331f, 0.89559525f};
// D65 camera gamuts derived from the manufacturers' published chromaticities.
constexpr Mat3 ARRI_WG3_TO_SRGB{1.61752344f,  -0.53728662f, -0.08023681f, -0.07057274f, 1.33461306f,
                                -0.26404032f, -0.02110173f, -0.22695388f, 1.24805560f};
constexpr Mat3 SRGB_TO_ARRI_WG3{0.63132102f, 0.27080073f, 0.09787825f, 0.03681993f, 0.79303697f,
                                0.17014310f, 0.01736973f, 0.14878918f, 0.83384108f};
constexpr Mat3 SONY_SGAMUT3CINE_TO_SRGB{1.62694741f,  -0.54013854f, -0.08680887f, -0.17851553f, 1.41794093f,
                                        -0.23942540f, -0.04443612f, -0.19591997f, 1.24035608f};
constexpr Mat3 SRGB_TO_SONY_SGAMUT3CINE{0.64567948f, 0.25911455f, 0.09520598f, 0.08752999f, 0.75969956f,
                                        0.15277045f, 0.03695742f, 0.12928090f, 0.83376168f};
constexpr Mat3 PANASONIC_VGAMUT_TO_SRGB{1.80657588f,  -0.69569727f, -0.11087861f, -0.17009034f, 1.30595522f,
                                        -0.13586487f, -0.02520578f, -0.15446833f, 1.17967411f};
constexpr Mat3 SRGB_TO_PANASONIC_VGAMUT{0.58519615f, 0.32264162f, 0.09216223f, 0.07858857f, 0.81962711f,
                                        0.10178432f, 0.02279424f, 0.11421702f, 0.86298874f};
constexpr Mat3 FUJIFILM_FGAMUT_C_TO_SRGB{2.11985147f,  -1.07570505f, -0.04414642f, -0.23033585f, 1.37244215f,
                                         -0.14210630f, -0.01422743f, -0.15022499f, 1.16445242f};
constexpr Mat3 SRGB_TO_FUJIFILM_FGAMUT_C{0.51706902f, 0.41293468f, 0.06999630f, 0.08861716f, 0.80926315f,
                                         0.10211969f, 0.01775004f, 0.10944762f, 0.87280234f};

Mat3 inverse3(const Mat3& m) {
    const float a = m[0], b = m[1], c = m[2], d = m[3], e = m[4], f = m[5], g = m[6], h = m[7], i = m[8];
    const float det = a * (e * i - f * h) - b * (d * i - f * g) + c * (d * h - e * g);
    if (std::abs(det) < 1e-12f) throw std::runtime_error("singular gamut matrix");
    return Mat3{(e * i - f * h) / det, (c * h - b * i) / det, (b * f - c * e) / det,
                (f * g - d * i) / det, (a * i - c * g) / det, (c * d - a * f) / det,
                (d * h - e * g) / det, (b * g - a * h) / det, (a * e - b * d) / det};
}
}  // namespace

float encodeTransfer(float x, TransferFunction tf) {
    switch (tf) {
        case TransferFunction::Linear:
            return x;
        case TransferFunction::SRgb:
            return x <= 0.0031308f ? 12.92f * x : 1.055f * std::pow(std::max(x, 0.0f), 1.0f / 2.4f) - 0.055f;
        case TransferFunction::DaVinciIntermediate:
            return x <= DI_LIN_CUT ? x * DI_M : (std::log2(x + DI_A) + DI_B) * DI_C;
        case TransferFunction::Rec709:
            return x < 0.018f ? 4.5f * x : 1.099f * std::pow(std::max(x, 0.0f), 0.45f) - 0.099f;
        case TransferFunction::Rec2020:
            return x < REC2020_BETA ? 4.5f * x
                                    : REC2020_ALPHA * std::pow(std::max(x, 0.0f), 0.45f) - (REC2020_ALPHA - 1.0f);
        case TransferFunction::Gamma22:
            return std::copysign(std::pow(std::abs(x), 1.0f / 2.2f), x);
        case TransferFunction::Gamma24:
            return std::copysign(std::pow(std::abs(x), 1.0f / 2.4f), x);
        case TransferFunction::LogC3: {
            constexpr float cut = 0.010591f;
            return x > cut ? 0.247190f * std::log10(5.555556f * x + 0.052272f) + 0.385537f : 5.367655f * x + 0.092809f;
        }
        case TransferFunction::SLog3:
            return x >= 0.01125f ? (420.0f + std::log10((x + 0.01f) / 0.19f) * 261.5f) / 1023.0f
                                 : (x * (171.2102946929f - 95.0f) / 0.01125f + 95.0f) / 1023.0f;
        case TransferFunction::VLog:
            return x < 0.01f ? 5.6f * x + 0.125f : 0.241514f * std::log10(x + 0.00873f) + 0.598206f;
        case TransferFunction::FLog2C:
            return x >= FLOG2C_LIN_CUT ? FLOG2C_C * std::log10(FLOG2C_A * x + FLOG2C_B) + FLOG2C_D
                                       : FLOG2C_E * x + FLOG2C_F;
    }
    throw std::runtime_error("unknown transfer function");
}
float decodeTransfer(float x, TransferFunction tf) {
    switch (tf) {
        case TransferFunction::Linear:
            return x;
        case TransferFunction::SRgb:
            return x <= 0.04045f ? x / 12.92f : std::pow((x + 0.055f) / 1.055f, 2.4f);
        case TransferFunction::DaVinciIntermediate:
            return x <= DI_LOG_CUT ? x / DI_M : std::exp2(x / DI_C - DI_B) - DI_A;
        case TransferFunction::Rec709:
            return x < 0.081f ? x / 4.5f : std::pow((x + 0.099f) / 1.099f, 1.0f / 0.45f);
        case TransferFunction::Rec2020:
            return x < REC2020_ENCODED_CUT ? x / 4.5f
                                           : std::pow((x + (REC2020_ALPHA - 1.0f)) / REC2020_ALPHA, 1.0f / 0.45f);
        case TransferFunction::Gamma22:
            return std::copysign(std::pow(std::abs(x), 2.2f), x);
        case TransferFunction::Gamma24:
            return std::copysign(std::pow(std::abs(x), 2.4f), x);
        case TransferFunction::LogC3: {
            constexpr float cut = 0.1496582f;
            return x > cut ? (std::pow(10.0f, (x - 0.385537f) / 0.247190f) - 0.052272f) / 5.555556f
                           : (x - 0.092809f) / 5.367655f;
        }
        case TransferFunction::SLog3: {
            constexpr float cut = 171.2102946929f / 1023.0f;
            return x >= cut ? std::pow(10.0f, (x * 1023.0f - 420.0f) / 261.5f) * 0.19f - 0.01f
                            : (x * 1023.0f - 95.0f) * 0.01125f / (171.2102946929f - 95.0f);
        }
        case TransferFunction::VLog:
            return x < 0.181f ? (x - 0.125f) / 5.6f : std::pow(10.0f, (x - 0.598206f) / 0.241514f) - 0.00873f;
        case TransferFunction::FLog2C:
            return x >= FLOG2C_LOG_CUT ? (std::pow(10.0f, (x - FLOG2C_D) / FLOG2C_C) - FLOG2C_B) / FLOG2C_A
                                       : (x - FLOG2C_F) / FLOG2C_E;
    }
    throw std::runtime_error("unknown transfer function");
}
Vec3 encodeTransfer(Vec3 v, TransferFunction tf) {
    for (float& x : v) x = encodeTransfer(x, tf);
    return v;
}
Vec3 decodeTransfer(Vec3 v, TransferFunction tf) {
    for (float& x : v) x = decodeTransfer(x, tf);
    return v;
}
Vec3 applyMatrix(const Mat3& m, Vec3 v) {
    return {m[0] * v[0] + m[1] * v[1] + m[2] * v[2], m[3] * v[0] + m[4] * v[1] + m[5] * v[2],
            m[6] * v[0] + m[7] * v[1] + m[8] * v[2]};
}
Mat3 linearGamutMatrix(Gamut s, Gamut d) {
    if (s == d) return IDENTITY;
    auto toSrgb = [&](Gamut g) -> Mat3 {
        switch (g) {
            case Gamut::SRgbRec709:
                return IDENTITY;
            case Gamut::AcesCgAp1:
                return AP1_TO_SRGB;
            case Gamut::DaVinciWideGamut:
                return DWG_TO_SRGB;
            case Gamut::Rec2020:
                return REC2020_TO_SRGB;
            case Gamut::ArriWideGamut3:
                return ARRI_WG3_TO_SRGB;
            case Gamut::SonySGamut3Cine:
                return SONY_SGAMUT3CINE_TO_SRGB;
            case Gamut::PanasonicVGamut:
                return PANASONIC_VGAMUT_TO_SRGB;
            case Gamut::FujifilmFGamutC:
                return FUJIFILM_FGAMUT_C_TO_SRGB;
        }
        throw std::runtime_error("unsupported source gamut");
    };
    auto fromSrgb = [&](Gamut g) -> Mat3 {
        switch (g) {
            case Gamut::SRgbRec709:
                return IDENTITY;
            case Gamut::AcesCgAp1:
                return SRGB_TO_AP1;
            case Gamut::DaVinciWideGamut:
                return inverse3(DWG_TO_SRGB);
            case Gamut::Rec2020:
                return SRGB_TO_REC2020;
            case Gamut::ArriWideGamut3:
                return SRGB_TO_ARRI_WG3;
            case Gamut::SonySGamut3Cine:
                return SRGB_TO_SONY_SGAMUT3CINE;
            case Gamut::PanasonicVGamut:
                return SRGB_TO_PANASONIC_VGAMUT;
            case Gamut::FujifilmFGamutC:
                return SRGB_TO_FUJIFILM_FGAMUT_C;
        }
        throw std::runtime_error("unsupported destination gamut");
    };
    const Mat3 a = toSrgb(s), b = fromSrgb(d);
    Mat3 r{};
    for (int row = 0; row < 3; ++row)
        for (int col = 0; col < 3; ++col)
            for (int k = 0; k < 3; ++k) r[row * 3 + col] += b[row * 3 + k] * a[k * 3 + col];
    return r;
}
Vec3 convert(Vec3 rgb, ColorSpace s, ColorSpace d) {
    if (s == d) return rgb;
    auto lin = decodeTransfer(rgb, s.transfer);
    lin = applyMatrix(linearGamutMatrix(s.gamut, d.gamut), lin);
    return encodeTransfer(lin, d.transfer);
}
}  // namespace tonemap::color
