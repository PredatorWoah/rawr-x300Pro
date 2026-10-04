#pragma once

#include <array>
#include <cstdint>
#include <optional>

#include "metadata/CameraContextMetadata.h"

namespace rawrcam::color {

// Device-calibrated white-balance display estimate for the AUTO/preset
// readout (compact strip, drawer rails, monitor). Pure color math over plain
// floats: no Camera2/Vulkan types, so color-domain purity audits keep passing.
//
// The generic Kelvin-to-gains approximation used by the manual sliders is not
// calibrated to any sensor: on this device its cool-end red gain peaks at
// ~1.26 while the HAL reports ~2.5 for an ~8K scene, so the gains inverse
// pins at the 10K grid corner and stops tracking. This instead runs the same
// dual-illuminant white-xy iteration as the preview path
// (solveDualIlluminant) over the HAL neutral + device calibration, yielding
// the CCT other camera apps report, then maps Planckian displacement (Duv)
// onto the in-app tint axis.
//
// Returns nullopt when calibration is incomplete or degenerate; the caller
// falls back to the uncalibrated gains inverse. Temp is clamped to the
// slider range [2000, 10000]; tint to [-50, 50] (positive pushes green,
// matching Duv > 0 above the locus).
std::optional<std::array<int32_t, 2>> estimateWbDisplayTempTint(
    const std::array<float, 3>& cameraNeutral,
    const metadata::StaticColorCalibration& calibration);

}  // namespace rawrcam::color
