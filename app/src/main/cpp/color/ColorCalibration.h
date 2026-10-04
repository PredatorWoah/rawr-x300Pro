#pragma once

#include <string>

#include "color/FrameColorTransform.h"
#include "metadata/FrameMetadataSnapshot.h"

namespace rawrcam::color {

PreviewColorMode parsePreviewColorMode(const std::string& mode);

// Pure interpretation step. It consumes one immutable per-frame metadata
// snapshot and has no access to Camera2, Vulkan, UI, or mutable camera state.
FrameColorTransform deriveFrameColorTransform(const metadata::FrameMetadataSnapshot& frameMetadata,
                                              PreviewColorMode mode);

std::string describe(const FrameColorTransform& state, const metadata::FrameMetadataSnapshot& frameMetadata);

}  // namespace rawrcam::color
