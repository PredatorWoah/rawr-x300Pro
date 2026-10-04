#pragma once

#include <camera/NdkCameraMetadata.h>

#include <cstdint>
#include <string>

#include "camera/CameraControlTypes.h"
#include "camera/CameraRouting.h"

namespace rawrcam::camera {

// Interprets immutable Camera2 characteristics into RawrCam control state.
// It does not own requests/sessions and performs no Camera2 writes.
CameraControlState readInitialCameraControlState(const ACameraMetadata* characteristics, const LensRoute& route,
                                                 uint64_t generation);

std::string describeCameraControlCapabilities(const CameraControlState& state);

}  // namespace rawrcam::camera
