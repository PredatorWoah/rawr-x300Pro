#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

namespace rawrcam::develop {

// Median-conceals listed hot pixels in a tightly-packed RAW16 image.
// raw16 must be width*height*sizeof(uint16_t) bytes, row-major.
// hotPixelMap is flat [x0,y0,x1,y1,...] in stored-image pixels; out-of-bounds
// points are skipped. Only the JPEG/preview copy is concealed; DNG bytes stay
// untouched (a future FixBadPixels opcode can carry the same list).
// Returns the number of pixels actually replaced.
std::size_t concealHotPixelsInPackedRaw16(std::vector<uint8_t>& raw16, uint32_t width, uint32_t height,
                                          const std::vector<int32_t>& hotPixelMap) noexcept;

}  // namespace rawrcam::develop
