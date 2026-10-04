#pragma once

#include <cmath>

#include "metadata/FrameMetadataSnapshot.h"
#include "raw_denoise/DenoisePipeline.hpp"

namespace rawrcam::develop::rendered {

// Resolves the profiled-denoise noise model from a frame snapshot: green S,O
// from SENSOR_NOISE_PROFILE (CFA-aware channel pick, row-major 2x2 order)
// mapped with the demosaic normalization (physical-RGGB black + effective
// white, the same values Rcd/Vng still processors normalize with). Returns
// false (a/b untouched) when the profile is missing or invalid — callers
// force strength 0 instead of hallucinating a model.
inline bool resolveDenoiseNoise(const metadata::FrameMetadataSnapshot& metadata, float& a, float& b) {
    if (!metadata.cameraContext) return false;
    const float black = (metadata.blackLevelPhysicalRggb[1] + metadata.blackLevelPhysicalRggb[2]) * 0.5f;
    return raw_denoise::GreenNoiseToNormalized(metadata.sensorNoiseProfile.data(), metadata.sensorNoiseProfile.size(),
                                               metadata.cameraContext->rawPreviewCfa, black,
                                               metadata.effectiveWhiteLevel, a, b);
}

// Resolves the R16U <-> [0,1] bridge levels for GALOSH-RAW from a frame
// snapshot: green-black mean + effective white, the same normalization the
// demosaicers and the wavelet path use. Returns false when invalid —
// callers keep GALOSH off instead of running on a wrong domain.
inline bool resolveGaloshRawLevels(const metadata::FrameMetadataSnapshot& metadata, float& black, float& white) {
    white = metadata.effectiveWhiteLevel;
    black = (metadata.blackLevelPhysicalRggb[1] + metadata.blackLevelPhysicalRggb[2]) * 0.5f;
    if (!metadata.cameraContext) return false;
    return white > black && black >= 0.0f && std::isfinite(white) && std::isfinite(black);
}

}  // namespace rawrcam::develop::rendered
