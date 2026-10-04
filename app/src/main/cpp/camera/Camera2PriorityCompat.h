#pragma once

#include <camera/NdkCameraMetadataTags.h>

#include <cstdint>


namespace rawrcam::camera::camera2_priority {

// Android 16 / API 36 Camera2 AE-priority metadata ABI.
//
// NDK r29's generated NdkCameraMetadataTags.h predates these declarations,
// even though devices implementing API 36 expose the same stable metadata tag
// IDs on the Camera2 metadata wire. Keep the compatibility names project-local
// instead of redefining ACAMERA_* symbols so this header remains harmless with
// newer NDKs that add the official declarations.
//
// Canonical AOSP definitions:
//   ACAMERA_CONTROL_AE_PRIORITY_MODE            = ACAMERA_CONTROL_START + 61
//   ACAMERA_CONTROL_AE_AVAILABLE_PRIORITY_MODES = ACAMERA_CONTROL_START + 62
//   OFF = 0, SENSOR_SENSITIVITY_PRIORITY = 1,
//   SENSOR_EXPOSURE_TIME_PRIORITY = 2.
inline constexpr uint32_t kAePriorityModeTag = ACAMERA_CONTROL_START + 61;
inline constexpr uint32_t kAeAvailablePriorityModesTag = ACAMERA_CONTROL_START + 62;

}  // namespace rawrcam::camera::camera2_priority
