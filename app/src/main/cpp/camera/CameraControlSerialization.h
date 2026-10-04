#pragma once
#include <string>

#include "camera/CameraControlTypes.h"
namespace rawrcam::camera {
std::string serializeCameraControlState(const CameraControlState& state);
}
