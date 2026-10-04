#pragma once
namespace raw_preview {
class RawPreview;
}
namespace rawrcam::vulkan {
class RawAhbImporter;
}
namespace rawrcam::pipeline {
class RawDevelopRecorder;
class RealtimeResources {
   public:
    virtual ~RealtimeResources() = default;
    virtual vulkan::RawAhbImporter* importer() const noexcept = 0;
    virtual RawDevelopRecorder* recorder() const noexcept = 0;
    virtual raw_preview::RawPreview* preview() const noexcept = 0;
};
}  // namespace rawrcam::pipeline
