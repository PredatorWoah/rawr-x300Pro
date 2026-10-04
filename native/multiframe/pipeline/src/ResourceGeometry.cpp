#include <rawr/raw_gpu_pipeline/ResourceGeometry.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>

namespace rawr::raw_gpu_pipeline {
namespace {
Extent2D downsampleValid(Extent2D src, std::uint32_t factor) {
    if (factor == 1u) return src;
    const float sigma = static_cast<float>(factor) * 0.5f;
    const int radius = static_cast<int>(4.0f * sigma + 0.5f);
    const std::uint32_t kernel = static_cast<std::uint32_t>(2 * radius + 1);
    if (src.width < kernel || src.height < kernel) {
        throw std::invalid_argument("multiframe geometry: pyramid level too small for valid Gaussian");
    }
    const std::uint32_t filteredW = src.width - kernel + 1u;
    const std::uint32_t filteredH = src.height - kernel + 1u;
    const Extent2D out{filteredW / factor, filteredH / factor};
    if (out.width == 0u || out.height == 0u) {
        throw std::invalid_argument("multiframe geometry: empty pyramid level");
    }
    return out;
}

std::pair<std::uint32_t, std::uint32_t> rationalScale(float s) {
    if (!(std::isfinite(s) && s >= 1.0f && s <= 2.0f)) {
        throw std::invalid_argument("multiframe geometry: output scale outside [1,2]");
    }
    // Keep output geometry deterministic for the currently supported public API.
    // 1/1024 resolution is far finer than UI parameter granularity and avoids
    // platform-dependent float->integer drift in output dimensions.
    constexpr std::uint32_t den = 1024u;
    const auto num = static_cast<std::uint32_t>(std::llround(static_cast<double>(s) * den));
    return {num, den};
}
}  // namespace

MultiframeGeometry makeMultiframeGeometry(std::uint32_t w, std::uint32_t h, float outputScale) {
    if (w == 0u || h == 0u || (w & 1u) != 0u || (h & 1u) != 0u) {
        throw std::invalid_argument("multiframe geometry: RAW dimensions must be nonzero/even");
    }
    static constexpr std::array<std::uint32_t, 4> factors{1u, 2u, 4u, 4u};
    static constexpr std::array<std::uint32_t, 4> tileSizes{16u, 16u, 16u, 8u};
    static constexpr std::array<std::uint32_t, 4> radii{1u, 4u, 4u, 4u};

    std::array<AlignmentLevelGeometry, 4> fineToCoarse{};
    Extent2D cur{w, h};
    for (std::size_t i = 0; i < 4u; ++i) {
        cur = downsampleValid(cur, factors[i]);
        auto& g = fineToCoarse[i];
        g.image = cur;
        g.tileSize = tileSizes[i];
        g.radius = radii[i];
        g.factorFromPreviousFine = factors[i];
        g.tilesX = cur.width / g.tileSize;
        g.tilesY = cur.height / g.tileSize;
        if (g.tilesX == 0u || g.tilesY == 0u) {
            throw std::invalid_argument("multiframe geometry: pyramid level has no complete alignment tile");
        }
    }

    const auto [num, den] = rationalScale(outputScale);
    MultiframeGeometry out{};
    out.raw = {w, h};
    out.guide = {w / 2u, h / 2u};
    out.outputScaleNumerator = num;
    out.outputScaleDenominator = den;
    // The reconstructed output is projected back to a 2x2 CFA mosaic. Preserve
    // an even extent for every public scale so Bayer phase and downstream
    // demosaic geometry remain stable.
    out.output = {static_cast<std::uint32_t>((static_cast<std::uint64_t>(w) * num) / den) & ~1u,
                  static_cast<std::uint32_t>((static_cast<std::uint64_t>(h) * num) / den) & ~1u};
    for (std::size_t i = 0; i < 4u; ++i) out.coarseToFine[i] = fineToCoarse[3u - i];
    return out;
}

}  // namespace rawr::raw_gpu_pipeline
