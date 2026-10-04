#include "camera/CameraControlResult.h"

#include <algorithm>

namespace rawrcam::camera {

std::vector<FaceDetection> normalizeCameraFaces(const metadata::RectI& active, const int32_t* rectangles,
                                                size_t rectangleValueCount, const uint8_t* scores, size_t scoreCount) {
    std::vector<FaceDetection> out;
    if (!active.valid || active.right <= active.left || active.bottom <= active.top || !rectangles) return out;
    const size_t faces = std::min(rectangleValueCount / 4, kMaxFaceDetections);
    const float width = static_cast<float>(active.right - active.left);
    const float height = static_cast<float>(active.bottom - active.top);
    for (size_t i = 0; i < faces; ++i) {
        const int32_t left = std::clamp(rectangles[i * 4], active.left, active.right);
        const int32_t top = std::clamp(rectangles[i * 4 + 1], active.top, active.bottom);
        const int32_t right = std::clamp(rectangles[i * 4 + 2], active.left, active.right);
        const int32_t bottom = std::clamp(rectangles[i * 4 + 3], active.top, active.bottom);
        if (right <= left || bottom <= top) continue;
        FaceDetection face;
        face.x = static_cast<float>(left - active.left) / width;
        face.y = static_cast<float>(top - active.top) / height;
        face.w = static_cast<float>(right - left) / width;
        face.h = static_cast<float>(bottom - top) / height;
        face.score = scores && i < scoreCount && scores[i] > 0 ? scores[i] : 50;
        out.push_back(face);
    }
    std::sort(out.begin(), out.end(), [](const FaceDetection& a, const FaceDetection& b) { return a.score > b.score; });
    return out;
}

}  // namespace rawrcam::camera
