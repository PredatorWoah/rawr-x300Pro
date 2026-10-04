#pragma once

#include <string>

namespace rawrcam::camera {
// Read-only NDK query. Does not open a camera or create an engine.
std::string cameraVideoCapabilitiesSnapshot();
}  // namespace rawrcam::camera
