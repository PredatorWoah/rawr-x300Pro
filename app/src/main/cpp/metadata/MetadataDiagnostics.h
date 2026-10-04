#pragma once

#include <string>

#include "metadata/CameraContextMetadata.h"
#include "metadata/FrameMetadataSnapshot.h"

namespace rawrcam::metadata {

std::string describe(const CameraContextMetadata& metadata);
std::string describe(const FrameMetadataSnapshot& metadata);

}  // namespace rawrcam::metadata
