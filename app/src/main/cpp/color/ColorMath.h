#pragma once

#include <array>
#include <cstdint>
#include <optional>

namespace rawrcam::color::math {

using Matrix3 = std::array<float, 9>;
using Vec3 = std::array<float, 3>;

struct Xy {
    float x = 0.0f;
    float y = 0.0f;
    bool valid = false;
};

Matrix3 identity3();
Matrix3 multiply(const Matrix3& a, const Matrix3& b);
Vec3 multiply(const Matrix3& a, const Vec3& v);
std::optional<Matrix3> inverse(const Matrix3& m);
Matrix3 interpolate(const Matrix3& a, const Matrix3& b, float weightA);
Matrix3 diagonal(const Vec3& v);
std::optional<Matrix3> inverseDiagonal(const Vec3& v);

Xy xyzToXy(const Vec3& xyz);
float correlatedColorTemperatureKelvin(const Xy& xy);

// Signed green-magenta displacement of [xy] from the Planckian locus at
// [cctKelvin], measured as (v - vPlanck) in CIE 1960 uv space. Positive is
// above the locus (greenish), mirroring the in-app tint convention where
// positive tint pushes green. Returns NaN when either input is degenerate.
float deltaUvFromPlanckian(const Xy& xy, float cctKelvin);

// Android/DNG reference-illuminant values follow TIFF/EXIF LightSource IDs.
// Returns a nominal CCT only for illuminants with a well-defined useful CCT.
std::optional<float> referenceIlluminantCctKelvin(int32_t illuminant);

// DNG-style reciprocal-temperature interpolation. Returns the weight for
// endpoint 1 and is valid regardless of endpoint ordering.
float reciprocalTemperatureWeight1(float temperatureKelvin, float temperature1Kelvin, float temperature2Kelvin);

constexpr Matrix3 kLinearSrgbToAcesAp1 = {0.613097f, 0.339523f, 0.047380f, 0.070194f, 0.916354f,
                                          0.013452f, 0.020616f, 0.109570f, 0.869815f};

std::array<float, 9> toColumnMajor(const Matrix3& matrix);

// Camera-to-working (ACEScg) transform in column-major order. Pure
// composition of the calibrated camera-to-linear-sRGB matrix with the fixed
// linear-sRGB to ACEScg matrix. Shared by the single-frame and MFSR still
// stages, which previously duplicated this expression.
inline std::array<float, 9> cameraToWorkingColumnMajor(const Matrix3& cameraToLinearSrgbRowMajor) {
    return toColumnMajor(multiply(kLinearSrgbToAcesAp1, cameraToLinearSrgbRowMajor));
}

constexpr Matrix3 kD50ToD65Bradford = {0.9555766f, -0.0230393f, 0.0631636f,  -0.0282895f, 1.0099416f,
                                       0.0210077f, 0.0122982f,  -0.0204830f, 1.3299098f};

constexpr Matrix3 kXyzD65ToLinearSrgb = {3.2404542f, -1.5371385f, -0.4985314f, -0.9692660f, 1.8760108f,
                                         0.0415560f, 0.0556434f,  -0.2040259f, 1.0572252f};

}  // namespace rawrcam::color::math
