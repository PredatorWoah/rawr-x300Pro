#pragma once
#include "develop/DevelopSettings.h"
#include "encoding/jpeg/JpegCaptureContext.h"
namespace rawrcam::capture {
// Processing intent and encoded artifact metadata have independent owners.
struct JpegCaptureRequest {
    develop::DevelopSettings develop;
    encoding::jpeg::JpegCaptureContext output;
};
}  // namespace rawrcam::capture
