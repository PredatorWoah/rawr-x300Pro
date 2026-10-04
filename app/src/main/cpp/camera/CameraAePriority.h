#pragma once

#include <cstdint>

// Stable Camera2 AE-priority values. Kept independent of NDK tag declarations
// so provenance and result policy can execute in host tests.
namespace rawrcam::camera::camera2_priority {
inline constexpr uint8_t kOff = 0;
inline constexpr uint8_t kSensorSensitivityPriority = 1;
inline constexpr uint8_t kSensorExposureTimePriority = 2;
}  // namespace rawrcam::camera::camera2_priority
