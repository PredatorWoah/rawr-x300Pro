#pragma once

#include <string>

#include "metadata/CameraContextMetadata.h"
#include "metadata/FrameMetadataSnapshot.h"

namespace rawrcam::metadata {

bool validate(const CameraContextMetadata& metadata, std::string* error);
bool validate(const FrameMetadataSnapshot& metadata, std::string* error);

}  // namespace rawrcam::metadata
