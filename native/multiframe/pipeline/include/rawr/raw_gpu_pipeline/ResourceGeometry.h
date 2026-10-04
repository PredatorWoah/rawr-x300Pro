#pragma once
#include <array>
#include <cstdint>
#include <stdexcept>

namespace rawr::raw_gpu_pipeline {

struct Extent2D {
    std::uint32_t width = 0;
    std::uint32_t height = 0;
};

struct AlignmentLevelGeometry {
    Extent2D image{};
    std::uint32_t tileSize = 0;
    std::uint32_t radius = 0;
    std::uint32_t factorFromPreviousFine = 1;
    std::uint32_t tilesX = 0;
    std::uint32_t tilesY = 0;
};

struct MultiframeGeometry {
    Extent2D raw{};
    Extent2D guide{};   // exact CPU robustness guide domain: floor(raw/2)
    Extent2D output{};  // reconstruction output domain
    std::array<AlignmentLevelGeometry, 4> coarseToFine{};
    std::uint32_t outputScaleNumerator = 1;
    std::uint32_t outputScaleDenominator = 1;
};

// Literal ReferenceParity geometry from CPU_ORACLE_RawAlignmentReference.cpp.
// Downsample(f): scipy-style separable VALID Gaussian, then sample [0::f].
MultiframeGeometry makeMultiframeGeometry(std::uint32_t rawWidth, std::uint32_t rawHeight, float outputScale);

}  // namespace rawr::raw_gpu_pipeline
