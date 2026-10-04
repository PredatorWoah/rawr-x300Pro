#pragma once
// EXIF-orientation transpose for renderer preview pixels.
//
// The JNI preview path historically returned unrotated pixels and relied on
// a second Kotlin Bitmap (Matrix postRotate/postScale) for display order.
// That cost one full-frame allocation + copy per preview. These helpers fold
// the orientation into the native return buffer instead.
//
// Mapping is the EXIF standard (mirrors Android ExifInterface rotation
// matrices and the previous Kotlin Matrix block):
//   1 identity, 2 flip-horizontal, 3 rotate-180, 4 flip-vertical,
//   5 transpose, 6 rotate-90-CW, 7 transverse, 8 rotate-270-CW.
// Out-of-range orientations behave as 1 (identity), matching DngSource.
#include <cstdint>
#include <utility>
#include <vector>

namespace rawrcam::renderer {

// Display dimensions after applying orientation (5..8 swap width/height).
inline std::pair<uint32_t, uint32_t> transposedDimensions(uint32_t w, uint32_t h,
                                                          uint32_t orientation) noexcept {
    if (orientation >= 5 && orientation <= 8) return {h, w};
    return {w, h};
}

// Source coordinates for display pixel (dx, dy) in an image of size w x h.
inline std::pair<uint32_t, uint32_t> transposeSource(uint32_t dx, uint32_t dy, uint32_t w, uint32_t h,
                                                     uint32_t orientation) noexcept {
    switch (orientation) {
        case 2:
            return {w - 1 - dx, dy};
        case 3:
            return {w - 1 - dx, h - 1 - dy};
        case 4:
            return {dx, h - 1 - dy};
        case 5:
            return {dy, dx};
        case 6:
            return {dy, h - 1 - dx};
        case 7:
            return {w - 1 - dy, h - 1 - dx};
        case 8:
            return {w - 1 - dy, dx};
        case 1:
        default:
            return {dx, dy};
    }
}

// Returns display-ordered pixels. Orientations 5..8 swap dimensions; the
// caller must size the destination with transposedDimensions().
template <typename T>
inline std::vector<T> transposeExif(const std::vector<T>& src, uint32_t w, uint32_t h,
                                    uint32_t orientation) {
    if (orientation < 2 || orientation > 8 || src.size() < size_t(w) * h) return src;
    const auto dims = transposedDimensions(w, h, orientation);
    std::vector<T> dst(size_t(dims.first) * dims.second);
    for (uint32_t y = 0; y < dims.second; ++y)
        for (uint32_t x = 0; x < dims.first; ++x) {
            const auto s = transposeSource(x, y, w, h, orientation);
            dst[size_t(y) * dims.first + x] = src[size_t(s.second) * w + s.first];
        }
    return dst;
}

}  // namespace rawrcam::renderer
