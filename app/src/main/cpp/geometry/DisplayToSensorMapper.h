#pragma once

#include <cstdint>
#include <optional>

namespace rawrcam::geometry {

struct NormalizedPoint {
    float x = 0.5f;
    float y = 0.5f;
};

// Centered crop window in pre-rotation source pixels (same space as the
// sourceWidth/sourceHeight arguments). Disabled (or empty) means the full
// source. Mirrors the recording source rect (see video_pipeline/VideoCrop.h)
// so tap-to-focus, spot-AE, and face boxes track the recorded crop exactly.
struct SourceCropRect {
    uint32_t x = 0;
    uint32_t y = 0;
    uint32_t w = 0;
    uint32_t h = 0;
    bool enabled = false;
};

// Converts a normalized point in the displayed Surface into normalized sensor/source
// coordinates using the exact same rotation and aspect-fit contract as present.frag.
std::optional<NormalizedPoint> displayToSource(float displayX, float displayY, uint32_t sourceWidth,
                                               uint32_t sourceHeight, uint32_t surfaceWidth, uint32_t surfaceHeight,
                                               int rotationDegrees);
std::optional<NormalizedPoint> displayToSource(float displayX, float displayY, uint32_t sourceWidth,
                                               uint32_t sourceHeight, uint32_t surfaceWidth, uint32_t surfaceHeight,
                                               int rotationDegrees, const SourceCropRect& crop);

// Exact inverse of displayToSource (same rotation/aspect-fit contract): sensor/
// source normalized -> displayed Surface normalized. Used for overlaying
// sensor-space detections (faces) on the viewfinder.
std::optional<NormalizedPoint> sourceToDisplay(float sourceX, float sourceY, uint32_t sourceWidth,
                                               uint32_t sourceHeight, uint32_t surfaceWidth, uint32_t surfaceHeight,
                                               int rotationDegrees);
std::optional<NormalizedPoint> sourceToDisplay(float sourceX, float sourceY, uint32_t sourceWidth,
                                               uint32_t sourceHeight, uint32_t surfaceWidth, uint32_t surfaceHeight,
                                               int rotationDegrees, const SourceCropRect& crop);

// Centered pre-rotation crop rect -> rotated-frame window fractions
// [x0,y0,x1,y1]. Identity when the crop is disabled/empty/out of range.
// Shared with the presentation shader path (PresentRecorder) so Kotlin-side
// focus boxes, face boxes, and displayed pixels always agree.
inline void cropWindowFractions(uint32_t sourceWidth, uint32_t sourceHeight, const SourceCropRect& crop,
                                bool swapped, float& x0, float& y0, float& x1, float& y1) noexcept {
    x0 = 0.0f;
    y0 = 0.0f;
    x1 = 1.0f;
    y1 = 1.0f;
    if (!crop.enabled || crop.w == 0 || crop.h == 0 || sourceWidth == 0 || sourceHeight == 0) return;
    if (crop.x + crop.w > sourceWidth || crop.y + crop.h > sourceHeight) return;
    const float rotatedW = swapped ? float(sourceHeight) : float(sourceWidth);
    const float rotatedH = swapped ? float(sourceWidth) : float(sourceHeight);
    // Centered rects stay centered under quarter turns; dims swap on odd turns.
    const float cropW = swapped ? float(crop.h) : float(crop.w);
    const float cropH = swapped ? float(crop.w) : float(crop.h);
    if (cropW <= 0.0f || cropH <= 0.0f || cropW > rotatedW || cropH > rotatedH) return;
    const float fx = cropW / rotatedW;
    const float fy = cropH / rotatedH;
    x0 = (1.0f - fx) * 0.5f;
    y0 = (1.0f - fy) * 0.5f;
    x1 = 1.0f - x0;
    y1 = 1.0f - y0;
}

}  // namespace rawrcam::geometry
