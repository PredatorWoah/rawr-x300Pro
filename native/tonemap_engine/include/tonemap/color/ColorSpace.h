#pragma once
#include <array>
#include <cstdint>

namespace tonemap::color {

enum class Gamut : uint32_t {
    SRgbRec709 = 0,
    AcesCgAp1 = 1,
    DaVinciWideGamut = 2,
    Rec2020 = 3,
    ArriWideGamut3 = 4,
    SonySGamut3Cine = 5,
    PanasonicVGamut = 6,
    FujifilmFGamutC = 7,
};

enum class TransferFunction : uint32_t {
    Linear = 0,
    SRgb = 1,
    DaVinciIntermediate = 2,
    Rec2020 = 3,
    Gamma22 = 4,
    Gamma24 = 5,
    LogC3 = 6,
    SLog3 = 7,
    VLog = 8,
    FLog2C = 9,
    Rec709 = 10,
};

struct ColorSpace {
    Gamut gamut = Gamut::SRgbRec709;
    TransferFunction transfer = TransferFunction::SRgb;
    bool operator==(const ColorSpace& other) const { return gamut == other.gamut && transfer == other.transfer; }
};

using Mat3 = std::array<float, 9>;  // row-major
using Vec3 = std::array<float, 3>;

float encodeTransfer(float linear, TransferFunction tf);
float decodeTransfer(float encoded, TransferFunction tf);
Vec3 encodeTransfer(Vec3 linear, TransferFunction tf);
Vec3 decodeTransfer(Vec3 encoded, TransferFunction tf);
Mat3 linearGamutMatrix(Gamut source, Gamut destination);
Vec3 applyMatrix(const Mat3& m, Vec3 v);
Vec3 convert(Vec3 rgb, ColorSpace source, ColorSpace destination);

}  // namespace tonemap::color
