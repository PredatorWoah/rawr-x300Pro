#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

#include "geometry/RawGeometry.h"

namespace rawrcam::imaging {

// RAW10 groups are four MSB bytes followed by one byte of the four 2-bit LSBs.
using geometry::RawPixelFormat;
using geometry::rawPixelFormatName;

// A CPU-visible view of one RAW plane as delivered by the camera.
struct RawPixelSource {
    const uint8_t* base = nullptr;
    RawPixelFormat format = RawPixelFormat::Raw16;
    uint32_t width = 0;
    uint32_t height = 0;
    size_t rowStrideBytes = 0;
};

// Minimum row bytes a valid source must provide for its width.
[[nodiscard]] size_t minimumRowBytes(RawPixelFormat format, uint32_t width) noexcept;

[[nodiscard]] uint16_t readRawPixel(const RawPixelSource& source, uint32_t x, uint32_t y) noexcept;

// Unpacks one RAW10 row (width must be a multiple of 4) into uint16 values.
void unpackRaw10Row(const uint8_t* packed, uint32_t width, uint16_t* destination) noexcept;

// Copies the source into tightly packed RAW16 rows (stride = width). Returns
// false when the source layout is invalid for its declared width.
bool copyRawToPackedRaw16(const RawPixelSource& source, uint16_t* destination) noexcept;

struct RawContentStatsInput {
    // CFA as Camera2 SENSOR_INFO_COLOR_FILTER_ARRANGEMENT (0=RGGB 1=GRBG 2=GBRG 3=BGGR).
    int cfa = 0;
    float blackLevel = 0.0f;
    float whiteLevel = 1023.0f;
    // Sample every Nth Bayer quad in each direction; 1 samples every quad.
    uint32_t quadStep = 8;
    // When both are non-zero the line also carries a coarse green-brightness map, gridColumns x gridRows cells in
    // row-major order, so the framing of two frames (and hence the zoom between sensor modes) can be compared from a
    // log alone. Off by default: it is only worth the extra log bytes while scanning sensor modes.
    uint32_t gridColumns = 0;
    uint32_t gridRows = 0;
};

// One-line summary of the RAW plane content, to tell a black or garbage
// sensor buffer apart from a development problem in device logs.
[[nodiscard]] std::string rawContentStatsLine(const RawPixelSource& source, const RawContentStatsInput& input);

}  // namespace rawrcam::imaging
