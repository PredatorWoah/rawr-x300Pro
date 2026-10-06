#pragma once

#include <algorithm>
#include <cmath>

namespace rawrcam::camera {

// The manual focus rail position (0 = closest, 1 = infinity) to lens position in diopters (1 / metres).
//
// A rail that is linear in diopters wastes almost all of its travel on near distances: with a 10 D minimum, 3 m is
// already at 0.967 and everything out to infinity lives in the last 3% of the slider, so any touch beyond a few
// metres jumps to the far end. Raising (1 - position) to a power spreads the rail roughly evenly over the logarithm
// of distance. The Kotlin side mirrors this exponent in ExposureModel.kt (MF_CURVE_EXPONENT); keep them equal.
inline constexpr float kManualFocusCurveExponent = 3.0f;

[[nodiscard]] inline float manualFocusDiopters(float normalized, float minimumFocusDistance) noexcept {
    const float far = 1.0f - std::clamp(normalized, 0.0f, 1.0f);
    return std::pow(far, kManualFocusCurveExponent) * std::max(minimumFocusDistance, 0.0f);
}

// Inverse of manualFocusDiopters, clamped to the rail.
[[nodiscard]] inline float manualFocusNormalized(float diopters, float minimumFocusDistance) noexcept {
    if (!(minimumFocusDistance > 0.0f)) return 1.0f;
    const float ratio = std::clamp(diopters / minimumFocusDistance, 0.0f, 1.0f);
    return 1.0f - std::pow(ratio, 1.0f / kManualFocusCurveExponent);
}

}  // namespace rawrcam::camera
