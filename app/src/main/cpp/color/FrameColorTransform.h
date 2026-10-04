#pragma once

#include <array>
#include <string>

namespace rawrcam::color {

enum class PreviewColorMode {
    Auto,
    Result,
    Forward1,
    Forward2,
    Identity,
    NeutralWb,
};

// Per-frame color product consumed by the preview renderer. The input RGB has
// already been white-balanced by raw_preview; this transform therefore maps
// that exact white-balanced camera-RGB representation to linear sRGB.
struct FrameColorTransform {
    std::array<float, 4> baselineWbRggb{1, 1, 1, 1};
    std::array<float, 9> cameraToLinearSrgbRowMajor{1, 0, 0, 0, 1, 0, 0, 0, 1};
    std::string source;
    bool baselineWbAppliedByRawPreview = true;

    // Diagnostics for the dual-illuminant solution. These values are products
    // of ColorCalibration, not Camera2 metadata themselves.
    bool hasEstimatedWhite = false;
    float estimatedWhiteX = 0.0f;
    float estimatedWhiteY = 0.0f;
    float estimatedCctKelvin = 0.0f;
    float calibrationWeight1 = 1.0f;  // 1 => illuminant/matrix set 1, 0 => set 2.
};

}  // namespace rawrcam::color
