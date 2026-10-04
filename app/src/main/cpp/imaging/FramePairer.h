#pragma once
#include <android/hardware_buffer.h>
#include <media/NdkImage.h>

#include <cstdint>
#include <map>

#include "color/FrameColorTransform.h"
#include "metadata/FrameMetadataSnapshot.h"

namespace rawrcam::imaging {
struct FrameRenderMetadata {
    metadata::FrameMetadataSnapshot snapshot{};
    color::FrameColorTransform colorState{};
};
struct PendingRawImage {
    AImage* image = nullptr;
    AHardwareBuffer* ahb = nullptr;
    int acquireFenceFd = -1;
};
struct MatchedFrame {
    uint64_t timestampNs = 0;
    PendingRawImage image{};
    FrameRenderMetadata metadata{};
};
class FramePairer {
   public:
    ~FramePairer();
    void putImage(uint64_t timestampNs, PendingRawImage image);
    void putMetadata(uint64_t timestampNs, FrameRenderMetadata metadata);
    bool takeMatched(uint64_t timestampNs, MatchedFrame* out);
    uint64_t trim(size_t maxPendingImages, size_t maxPendingMetadata);
    void clear();
    size_t imageCount() const noexcept { return images_.size(); }
    size_t metadataCount() const noexcept { return metadata_.size(); }

   private:
    static void release(PendingRawImage& image) noexcept;
    std::map<uint64_t, PendingRawImage> images_;
    std::map<uint64_t, FrameRenderMetadata> metadata_;
};
}  // namespace rawrcam::imaging
