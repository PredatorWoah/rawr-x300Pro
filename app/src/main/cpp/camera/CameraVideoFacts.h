#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace rawrcam::camera {
struct RawVideoMode {
    int32_t width = 0;
    int32_t height = 0;
    std::optional<int64_t> minFrameDurationNs;
};
struct CameraVideoFacts {
    std::string id;
    std::optional<int32_t> sensorOrientationDegrees;
    std::optional<int32_t> lensFacing;
    std::optional<int32_t> timestampSource;
    std::vector<RawVideoMode> raw;
    std::string error;
};
// Four scalars per NDK table entry. Unknown durations stay unknown; static
// stream facts do not promise that a session can sustain a particular FPS.
std::vector<RawVideoMode> readRawVideoModes(const int32_t* configurations, size_t configurationCount,
                                         const int64_t* durations, size_t durationCount,
                                         int32_t rawFormat, int32_t outputKind);
std::vector<std::pair<int32_t, int32_t>> encoderProbeDimensions(const std::vector<CameraVideoFacts>& cameras);
std::string serializeCameraVideoFacts(const std::vector<CameraVideoFacts>& cameras,
                                     const std::string& discoveryError = {});
}  // namespace rawrcam::camera
