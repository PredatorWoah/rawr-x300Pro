#pragma once
#include <rawr/raw_gpu_pipeline/ResourceGeometry.h>

#include <cstdint>
#include <string>
#include <vector>

namespace rawr::raw_gpu_pipeline {

enum class PixelStorage : std::uint32_t { R8Uint, R16Uint, R32Float, RG32Float, RGBA16Float, RGBA32Float };

enum class Lifetime : std::uint32_t {
    Burst,        // retained across all companions
    Companion,    // reused for each companion
    PyramidTemp,  // reused while building reference/companion pyramids
    Output        // final reconstruction/accumulators
};

struct ImageSpec {
    std::string name;
    Extent2D extent{};
    PixelStorage storage = PixelStorage::R32Float;
    Lifetime lifetime = Lifetime::Companion;
};

struct BufferSpec {
    std::string name;
    std::uint64_t bytes = 0;
    Lifetime lifetime = Lifetime::Companion;
};

struct ScratchLayout {
    MultiframeGeometry geometry{};
    std::vector<ImageSpec> images;
    std::vector<BufferSpec> buffers;
    std::uint64_t logicalImageBytes = 0;
    std::uint64_t logicalBufferBytes = 0;
};

std::uint32_t bytesPerPixel(PixelStorage) noexcept;
ScratchLayout makeMultiframeScratchLayout(const MultiframeGeometry&);
ScratchLayout makeMultiframeScratchLayout(const MultiframeGeometry&, std::uint32_t reconstructionStripeHeight);

}  // namespace rawr::raw_gpu_pipeline
