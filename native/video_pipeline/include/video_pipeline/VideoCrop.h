#pragma once

#include <cstdint>

namespace rawrcam::video {

// Recording source rect shared by the recording RAW stage and the idle
// viewfinder preview crop, so preview framing matches the recorded frame
// exactly. Single owner of the center-crop rule previously inline in
// VideoDemosaic: optional 2x Bayer reduction, then an even-aligned centered
// crop of the full RAW frame.
struct VideoSourceRect {
    uint32_t x = 0;
    uint32_t y = 0;
    uint32_t width = 0;
    uint32_t height = 0;
    bool reduceCfa = false;
};

// Source rect in full-RAW pixels for a recording output size. Returns an
// empty rect (width/height 0) when the output does not fit the sensor.
[[nodiscard]] inline VideoSourceRect videoSourceRect(uint32_t rawWidth, uint32_t rawHeight,
                                                     uint32_t outWidth, uint32_t outHeight) noexcept {
    VideoSourceRect rect{};
    const bool reduce =
        uint64_t(outWidth) * 2u <= rawWidth && uint64_t(outHeight) * 2u <= rawHeight;
    const uint32_t sourceWidth = reduce ? outWidth * 2u : outWidth;
    const uint32_t sourceHeight = reduce ? outHeight * 2u : outHeight;
    if (sourceWidth == 0 || sourceHeight == 0 || sourceWidth > rawWidth || sourceHeight > rawHeight)
        return rect;
    rect.x = ((rawWidth - sourceWidth) / 2u) & ~1u;
    rect.y = ((rawHeight - sourceHeight) / 2u) & ~1u;
    rect.width = sourceWidth;
    rect.height = sourceHeight;
    rect.reduceCfa = reduce;
    return rect;
}

// Scales a full-RAW-space rect into another source space (e.g. the half-res
// preview image). Exact for even halvings; otherwise truncates with even
// alignment and clamps inside the target. Returns empty when degenerate.
[[nodiscard]] inline VideoSourceRect scaleVideoSourceRect(const VideoSourceRect& rect, uint32_t fromWidth,
                                                          uint32_t fromHeight, uint32_t toWidth,
                                                          uint32_t toHeight) noexcept {
    VideoSourceRect out{};
    out.reduceCfa = rect.reduceCfa;
    if (fromWidth == 0 || fromHeight == 0 || toWidth == 0 || toHeight == 0 || rect.width == 0 ||
        rect.height == 0)
        return VideoSourceRect{};
    const uint64_t x = (uint64_t(rect.x) * toWidth / fromWidth) & ~1u;
    const uint64_t y = (uint64_t(rect.y) * toHeight / fromHeight) & ~1u;
    uint64_t w = uint64_t(rect.width) * toWidth / fromWidth;
    uint64_t h = uint64_t(rect.height) * toHeight / fromHeight;
    if (x >= toWidth || y >= toHeight) return VideoSourceRect{};
    if (x + w > toWidth) w = toWidth - x;
    if (y + h > toHeight) h = toHeight - y;
    if (w == 0 || h == 0) return VideoSourceRect{};
    out.x = static_cast<uint32_t>(x);
    out.y = static_cast<uint32_t>(y);
    out.width = static_cast<uint32_t>(w);
    out.height = static_cast<uint32_t>(h);
    return out;
}

}  // namespace rawrcam::video
