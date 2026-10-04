#pragma once
#include <functional>
#include <optional>

#include "develop/render/RenderDeviceContext.h"
#include "develop/render/RenderedStillTypes.h"
#include "imaging/Raw16CpuSnapshot.h"
#include "imaging/RawSnapshot.h"
namespace rawrcam::capture {
using CaptureDiagnostic = std::function<void(const std::string&)>;
struct SingleFrameSnapshot {
    std::shared_ptr<imaging::RawSnapshot> frame;
    tonemap::TonemapParams tonemapParams{};
    float aePostGain = 1.0f;
    bool filmEnabled = false;
    spektrafilm_native::FilmLook filmLook{};
    imaging::Raw16CpuSnapshotResult copyResult{};
};
struct SingleFrameDevelopResult {
    develop::rendered::RenderedStillCompletion completion;
    std::optional<develop::rendered::RenderedStillContext> context;
    develop::rendered::RenderOutputView pixels;
    double demosaicMs = 0.0;
    double demosaicSetupMs = 0.0;
};
}  // namespace rawrcam::capture
