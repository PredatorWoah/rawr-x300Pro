#include "color/ColorCalibration.h"
#include "metadata/MetadataValidation.h"

#include <cassert>
#include <memory>
#include <string>

int main() {
    auto context = std::make_shared<rawrcam::metadata::CameraContextMetadata>();
    context->cameraContextGeneration = 7;
    context->cameraId = "3";
    context->lensId = "main";
    context->geometry.rawBufferWidth = 4080;
    context->geometry.rawBufferHeight = 3064;
    context->rawPreviewCfa = 0;
    context->baselineBlackLevelPhysicalRggb = {64,64,64,64};
    context->baselineWhiteLevel = 1023;
    std::string error;
    assert(rawrcam::metadata::validate(*context, &error));

    rawrcam::metadata::FrameMetadataSnapshot frame{};
    frame.cameraContext = context;
    frame.frameOrdinal = 1;
    frame.timestampNs = 123456789;
    frame.blackLevelPhysicalRggb = {63.8f,63.8f,63.9f,63.8f};
    frame.staticWhiteLevel = 1023;
    frame.effectiveWhiteLevel = 1023;
    frame.colorCorrectionGainsRggb = {2.0f,1.0f,1.0f,1.5f};
    frame.exposureTimeNs = 8'000'000;
    frame.sensitivity = 100;
    assert(rawrcam::metadata::validate(frame, &error));

    // The frame pins the exact context even if the camera controller drops its
    // own reference after a lens switch or camera close.
    const rawrcam::metadata::CameraContextMetadata* pinned = frame.cameraContext.get();
    context.reset();
    assert(frame.cameraContext.get() == pinned);
    assert(frame.cameraContext->cameraContextGeneration == 7);
    assert(frame.cameraContext->cameraId == "3");
    assert(frame.cameraContext->geometry.rawBufferHeight == 3064);

    const auto neutral = rawrcam::color::deriveFrameColorTransform(
        frame, rawrcam::color::PreviewColorMode::NeutralWb);
    assert(neutral.baselineWbRggb[0] == 1.0f && neutral.baselineWbRggb[3] == 1.0f);

    const auto identity = rawrcam::color::deriveFrameColorTransform(
        frame, rawrcam::color::PreviewColorMode::Identity);
    assert(identity.cameraToLinearSrgbRowMajor[0] == 1.0f);
    assert(identity.cameraToLinearSrgbRowMajor[4] == 1.0f);
    assert(identity.cameraToLinearSrgbRowMajor[8] == 1.0f);
    return 0;
}
