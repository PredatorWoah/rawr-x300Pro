#pragma once

#include <array>

namespace rawrcam::color {

// Collapses an RGGB white-balance quad to RGB by averaging the two green
// sites. Single owner of the expression previously triplicated in
// SingleFrameCoordinator, MfsrCaptureJob and MultiframeFrameRing.
[[nodiscard]] inline std::array<float, 3> collapseRggbToRgb(const std::array<float, 4>& wbRggb) noexcept {
    return {wbRggb[0], 0.5f * (wbRggb[1] + wbRggb[2]), wbRggb[3]};
}

}  // namespace rawrcam::color
