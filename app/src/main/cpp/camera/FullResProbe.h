#pragma once

#include <functional>
#include <string>

namespace rawrcam::camera {

// Debug experiment: vivo's own camera configures an 8192x6144 RAW stream on camera 2 although the HAL advertises
// nothing above the binned size. This opens the camera by itself (the caller must have released it), tries RAW
// streams at 2x and 4x the advertised size in several configurations, captures one frame from each that the HAL
// accepts and logs what came back: delivered size, a brightness grid for field of view checks, and the result's pixel
// mode and crop. Everything is reported through diag; nothing is written to disk. Blocking; call from a worker thread
// without holding any camera lock.
void runFullResProbe(const std::string& cameraId, int baseWidth, int baseHeight,
                     const std::function<void(const std::string&)>& diag);

}  // namespace rawrcam::camera
