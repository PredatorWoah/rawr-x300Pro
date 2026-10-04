#pragma once

#include <cstdint>

namespace rawrcam::geometry {

// Video orientation follows sensor mount and physical posture. Front-facing
// cameras use the opposite posture sign; this does not rotate pixels.
// Non-quarter-turn inputs return -1 because video containers require quarter turns.
inline constexpr int videoRotationDegrees(int sensorOrientationDegrees, int deviceRotationDegrees,
                                          bool frontFacing) noexcept {
    const int sensor = (sensorOrientationDegrees % 360 + 360) % 360;
    const int device = (deviceRotationDegrees % 360 + 360) % 360;
    const int rotation = (sensor + (frontFacing ? device : -device) + 360) % 360;
    return rotation % 90 == 0 ? rotation : -1;
}

// Authoritative clockwise sensor/native-buffer -> displayed-image rotation.
// Device posture never changes native buffer dimensions. This convention is shared
// by live presentation, scope measurement orientation, still rendering, and DNG.
inline constexpr std::uint32_t presentationQuarterTurns(int sensorOrientationDegrees,
                                                        int displayRotationDegrees) noexcept {
    const int rotation = (sensorOrientationDegrees - displayRotationDegrees + 360) % 360;
    return static_cast<std::uint32_t>((rotation / 90) & 3);
}

}  // namespace rawrcam::geometry
