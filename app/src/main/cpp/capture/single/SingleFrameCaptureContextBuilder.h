#pragma once
#include "capture/CaptureRequest.h"
#include "capture/single/SingleFrameCaptureTypes.h"
namespace rawrcam::capture {
const char* singleFrameDemosaicName(develop::DemosaicAlgorithm algorithm) noexcept;
develop::rendered::RenderedStillContext makeSingleFrameRenderContext(
    const imaging::RawSnapshot& captured, const tonemap::TonemapParams& tone, float gain, uint32_t turns, bool film,
    const spektrafilm_native::FilmLook& look, const JpegCaptureRequest* jpeg, const CaptureDiagnostic& diagnostic);
void applySingleFrameCaptureMetadata(encoding::jpeg::JpegCaptureContext& output, const imaging::RawSnapshot& captured);
}  // namespace rawrcam::capture
