#pragma once

#include <cstdint>
#include <vector>

#include "color/FrameColorTransform.h"
#include "metadata/FrameMetadataSnapshot.h"

namespace rawrcam::imaging {

// Independent still-capture snapshot. It contains no Camera2/AImage/Vulkan
// handles, so its lifetime is completely decoupled from realtime preview.
struct RawSnapshot {
    uint64_t requestId = 0;
    uint64_t timestampNs = 0;
    uint32_t width = 0;
    uint32_t height = 0;
    uint32_t packedRowStrideBytes = 0;
    int32_t sourceRowStrideBytes = 0;
    int32_t sourcePixelStrideBytes = 0;
    metadata::FrameMetadataSnapshot metadata{};
    color::FrameColorTransform colorState{};
    std::vector<uint8_t> raw16;
};

}  // namespace rawrcam::imaging
