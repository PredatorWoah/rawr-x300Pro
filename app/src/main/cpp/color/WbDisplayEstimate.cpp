#include "color/WbDisplayEstimate.h"

#include <algorithm>
#include <cmath>

#include "color/ColorMath.h"

namespace rawrcam::color {
namespace {

constexpr int32_t kTempMinK = 2000;
constexpr int32_t kTempMaxK = 10000;
constexpr int32_t kTintMin = -50;
constexpr int32_t kTintMax = 50;
// Duv per tint step: full slider (±50) spans Duv ±0.015, a strong but
// plausible cast; typical AWB residual (±0.003) reads as ±10.
constexpr float kDuvPerTintStep = 0.00030f;

}  // namespace

std::optional<std::array<int32_t, 2>> estimateWbDisplayTempTint(
    const std::array<float, 3>& cameraNeutral,
    const metadata::StaticColorCalibration& calibration) {
    using math::Matrix3;
    using math::Vec3;
    for (float v : cameraNeutral) {
        if (!std::isfinite(v) || v <= 1.0e-8f) return std::nullopt;
    }
    if (!calibration.colorTransform1.valid || !calibration.colorTransform2.valid ||
        !calibration.calibrationTransform1.valid || !calibration.calibrationTransform2.valid ||
        !calibration.forwardMatrix1.valid || !calibration.forwardMatrix2.valid) {
        return std::nullopt;
    }
    const auto t1 = math::referenceIlluminantCctKelvin(calibration.referenceIlluminant1);
    const auto t2 = math::referenceIlluminantCctKelvin(calibration.referenceIlluminant2);
    if (!t1 || !t2) return std::nullopt;

    const Vec3 neutral = {cameraNeutral[0], cameraNeutral[1], cameraNeutral[2]};
    math::Xy white{0.34567f, 0.35850f, true};  // D50 start, matching DNG practice.
    for (int pass = 0; pass < 30; ++pass) {
        const float temperature = math::correlatedColorTemperatureKelvin(white);
        if (!(temperature > 0.0f)) return std::nullopt;
        const float weight1 = math::reciprocalTemperatureWeight1(temperature, *t1, *t2);
        const Matrix3 colorMatrix =
            math::interpolate(calibration.colorTransform1.rowMajor, calibration.colorTransform2.rowMajor, weight1);
        const Matrix3 calibrationMatrix = math::interpolate(
            calibration.calibrationTransform1.rowMajor, calibration.calibrationTransform2.rowMajor, weight1);
        const Matrix3 xyzToIndividualCamera = math::multiply(calibrationMatrix, colorMatrix);
        const auto individualToXyz = math::inverse(xyzToIndividualCamera);
        if (!individualToXyz) return std::nullopt;
        const math::Xy next = math::xyzToXy(math::multiply(*individualToXyz, neutral));
        if (!next.valid) return std::nullopt;
        if (std::abs(next.x - white.x) + std::abs(next.y - white.y) < 1.0e-7f) {
            white = next;
            break;
        }
        white = next;
    }

    const float cct = math::correlatedColorTemperatureKelvin(white);
    if (!(cct > 0.0f) || !std::isfinite(cct)) return std::nullopt;
    const float duv = math::deltaUvFromPlanckian(white, cct);
    const int32_t tempK =
        std::clamp(static_cast<int32_t>(std::lround(cct)), kTempMinK, kTempMaxK);
    int32_t tint = 0;
    if (std::isfinite(duv)) {
        tint = std::clamp(static_cast<int32_t>(std::lround(duv / kDuvPerTintStep)), kTintMin, kTintMax);
    }
    return std::array<int32_t, 2>{tempK, tint};
}

}  // namespace rawrcam::color
