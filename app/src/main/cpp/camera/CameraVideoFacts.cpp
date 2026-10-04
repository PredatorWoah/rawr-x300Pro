#include "camera/CameraVideoFacts.h"

#include <algorithm>
#include <iomanip>
#include <locale>
#include <sstream>

namespace rawrcam::camera {
namespace {
void jsonString(std::ostream& out, const std::string& value) {
    out << '"';
    for (const unsigned char c : value) {
        if (c == '"' || c == '\\') out << '\\' << static_cast<char>(c);
        else if (c < 0x20) out << "\\u00" << std::hex << std::setw(2) << std::setfill('0') << int(c) << std::dec;
        else out << static_cast<char>(c);
    }
    out << '"';
}
template <typename T> void optionalNumber(std::ostream& out, const std::optional<T>& value) {
    if (value) out << *value;
    else out << "null";
}
}  // namespace

std::vector<RawVideoMode> readRawVideoModes(const int32_t* configurations, size_t configurationCount,
                                         const int64_t* durations, size_t durationCount,
                                         int32_t rawFormat, int32_t outputKind) {
    std::vector<RawVideoMode> modes;
    if (!configurations) return modes;
    for (size_t i = 0; i + 3 < configurationCount; i += 4) {
        const int32_t width = configurations[i + 1], height = configurations[i + 2];
        if (configurations[i] != rawFormat || configurations[i + 3] != outputKind || width <= 0 || height <= 0) continue;
        if (std::any_of(modes.begin(), modes.end(), [&](const auto& mode) {
                return mode.width == width && mode.height == height;
            })) continue;
        RawVideoMode mode{width, height, std::nullopt};
        if (durations) {
            for (size_t j = 0; j + 3 < durationCount; j += 4) {
                if (durations[j] == rawFormat && durations[j + 1] == width && durations[j + 2] == height &&
                    durations[j + 3] >= 0) {
                    mode.minFrameDurationNs = durations[j + 3];
                    break;
                }
            }
        }
        modes.push_back(mode);
    }
    return modes;
}

std::vector<std::pair<int32_t, int32_t>> encoderProbeDimensions(const std::vector<CameraVideoFacts>& cameras) {
    std::vector<std::pair<int32_t, int32_t>> sizes{{1920, 1080}, {3840, 2160}};
    for (const auto& camera : cameras) {
        const RawVideoMode* largest = nullptr;
        for (const auto& mode : camera.raw) {
            if (mode.width <= 0 || mode.height <= 0) continue;
            if (!largest || int64_t(mode.width) * mode.height > int64_t(largest->width) * largest->height) largest = &mode;
        }
        if (!largest) continue;
        const std::pair<int32_t, int32_t> size{largest->width & ~1, largest->height & ~1};
        if (size.first > 0 && size.second > 0 && std::find(sizes.begin(), sizes.end(), size) == sizes.end()) sizes.push_back(size);
    }
    return sizes;
}

std::string serializeCameraVideoFacts(const std::vector<CameraVideoFacts>& cameras, const std::string& discoveryError) {
    std::ostringstream out;
    out.imbue(std::locale::classic());
    out << "{\"cameras\":[";
    bool first = true;
    for (const auto& camera : cameras) {
        if (!first) out << ',';
        first = false;
        out << "{\"id\":";
        jsonString(out, camera.id);
        out << ",\"sensorOrientationDegrees\":";
        optionalNumber(out, camera.sensorOrientationDegrees);
        out << ",\"lensFacing\":";
        optionalNumber(out, camera.lensFacing);
        out << ",\"timestampSource\":";
        optionalNumber(out, camera.timestampSource);
        out << ",\"raw\":[";
        bool firstMode = true;
        for (const auto& mode : camera.raw) {
            if (!firstMode) out << ',';
            firstMode = false;
            out << "{\"width\":" << mode.width << ",\"height\":" << mode.height << ",\"minFrameDurationNs\":";
            optionalNumber(out, mode.minFrameDurationNs);
            out << '}';
        }
        out << ']';
        if (!camera.error.empty()) { out << ",\"error\":"; jsonString(out, camera.error); }
        out << '}';
    }
    out << "],\"encoderProbeModes\":[";
    first = true;
    for (const auto& [width, height] : encoderProbeDimensions(cameras)) {
        if (!first) out << ',';
        first = false;
        out << "{\"width\":" << width << ",\"height\":" << height << '}';
    }
    out << ']';
    if (!discoveryError.empty()) { out << ",\"discoveryError\":"; jsonString(out, discoveryError); }
    out << '}';
    return out.str();
}
}  // namespace rawrcam::camera
