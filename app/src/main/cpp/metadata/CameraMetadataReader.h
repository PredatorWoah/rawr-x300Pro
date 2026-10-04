#pragma once

#include <camera/NdkCameraMetadata.h>

#include <cstdint>
#include <optional>
#include <string>

#include "metadata/CameraContextMetadata.h"
#include "metadata/FrameMetadataSnapshot.h"

namespace rawrcam::metadata {

std::optional<CameraContextMetadata> readCameraContextMetadata(const ACameraMetadata* characteristics,
                                                               const std::string& cameraId, const std::string& lensId,
                                                               uint64_t cameraContextGeneration, uint32_t rawWidth,
                                                               uint32_t rawHeight, std::string* error);

std::optional<FrameMetadataSnapshot> readFrameMetadataSnapshot(const ACameraMetadata* result,
                                                               CameraContextMetadataPtr cameraContext,
                                                               uint64_t frameOrdinal, std::string* error);

}  // namespace rawrcam::metadata
