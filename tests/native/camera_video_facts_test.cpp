#include "camera/CameraVideoFacts.h"
#include "geometry/OrientationTransform.h"

#include <cassert>
#include <iostream>

int main(int argc, char**) {
    using namespace rawrcam::camera;
    using rawrcam::geometry::videoRotationDegrees;
    const int back90[] = {90, 0, 270, 180};
    const int front270[] = {270, 0, 90, 180};
    for (int index = 0; index < 4; ++index) {
        assert(videoRotationDegrees(90, index * 90, false) == back90[index]);
        assert(videoRotationDegrees(270, index * 90, true) == front270[index]);
    }
    assert(videoRotationDegrees(450, -90, false) == 180);
    assert(videoRotationDegrees(450, -90, true) == 0);
    assert(videoRotationDegrees(90, 45, false) == -1);

    constexpr int32_t raw = 32, output = 0;
    const int32_t configurations[] = {
        raw, 1920, 1080, output,
        35, 3840, 2160, output,      // Different pixel format.
        raw, 4081, 3065, output,
        raw, 4081, 3065, output,    // Duplicate size.
        raw, -1, 100, output,
        raw, 2000, 1500, 1,         // Input stream.
        raw, 2001, 1501, output,
        raw, 99, 99                 // Incomplete trailing entry.
    };
    const int64_t durations[] = {
        35, 4081, 3065, 12,
        raw, 4081, 3065, 33333333,
        raw, 1920, 1080, 0,
        raw, 2001, 1501, -1,
        raw, 99, 99
    };
    const auto modes = readRawVideoModes(configurations, sizeof(configurations) / sizeof(configurations[0]),
                                        durations, sizeof(durations) / sizeof(durations[0]), raw, output);
    assert(modes.size() == 3);
    assert(modes[0].minFrameDurationNs && *modes[0].minFrameDurationNs == 0);
    assert(modes[1].width == 4081 && modes[1].height == 3065);
    assert(modes[1].minFrameDurationNs && *modes[1].minFrameDurationNs == 33333333);
    assert(!modes[2].minFrameDurationNs);
    assert(readRawVideoModes(nullptr, 8, durations, 4, raw, output).empty());
    const auto unknown = readRawVideoModes(configurations, 4, nullptr, 100, raw, output);
    assert(unknown.size() == 1 && !unknown[0].minFrameDurationNs);

    CameraVideoFacts camera;
    camera.id = "private\"camera\\\n";
    camera.sensorOrientationDegrees = 90;
    camera.lensFacing = 1;
    camera.timestampSource = 1;
    camera.raw = modes;
    CameraVideoFacts unavailable;
    unavailable.id = "missing";
    unavailable.error = "Unavailable\nmetadata";
    const auto probes = encoderProbeDimensions({camera, camera, unavailable});
    assert(probes.size() == 3);
    assert(probes[0] == std::make_pair(1920, 1080));
    assert(probes[1] == std::make_pair(3840, 2160));
    assert(probes[2] == std::make_pair(4080, 3064));
    CameraVideoFacts large;
    large.raw = {{100000, 100000, std::nullopt}, {200000, 200000, std::nullopt}};
    assert(encoderProbeDimensions({large}).back() == std::make_pair(200000, 200000));
    assert(encoderProbeDimensions({}).size() == 2);

    if (argc > 1) std::cout << serializeCameraVideoFacts({camera, unavailable}, "Discovery\tpartial") << '\n';
    else std::cout << "CAMERA_VIDEO_FACTS_TEST_PASS\n";
}
