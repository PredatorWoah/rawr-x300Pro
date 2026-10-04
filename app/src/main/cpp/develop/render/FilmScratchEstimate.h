#pragma once

#include <cstdint>

namespace rawrcam::develop::rendered {

// Effect gates mirroring SpektraFilm::createSlots() allocation: buffers are
// reserved per enabled effect, so the worst case is a pure function of
// geometry + these flags. Slightly over-estimates (counts full buffers for
// quarter-size down-chain, ignores tiny uniform buffers); the caller adds
// headroom on top. Unit-tested; keep in sync with createSlots().
//
// Memory model (P1 aliasing): halation scatter/bounce, DIR correction, and
// scanner post share one 3-buffer spatial arena (sequential lifetimes).
// Diffusion keeps its own Temp(2x)/Accum/Down chain; camera and print
// diffusion share one raw buffer. DIR density is gated on amount>0.
struct FilmScratchGates {
    bool grainPreview = false;     // grain on, any model
    bool grainProduction = false;  // grain on && model != 0
    bool halation = false;
    bool cameraDiffusion = false;
    bool printDiffusion = false;
    bool scanner = false;
    bool dir = false;  // dirCouplersAmount > 0
};

// Worst-case device-local bytes for one film slot at W×H RGBA32F (16 B/px).
[[nodiscard]] inline std::uint64_t estimateFilmScratchBytes(std::uint32_t w, std::uint32_t h,
                                                            const FilmScratchGates& gates) noexcept {
    const std::uint64_t px = std::uint64_t(w) * h * 16u;
    // Always resident: source, destination, filmRaw, filmDensity.
    std::uint64_t buffers = 2u + 2u;
    if (gates.grainPreview) buffers += 1u;
    // Sliced layer blur: densityB/microA/microB + one 9-float/px layer buffer;
    // the second 9x buffer is replaced by shared-arena scratch.
    if (gates.grainProduction) buffers += 3u;
    if (gates.halation) buffers += 2u;  // boostedRaw + logRaw (scratch in shared arena)
    // Temp 2x + accum + down-chain + raw (rounded up). Raw shared with print.
    if (gates.cameraDiffusion) buffers += 6u;
    if (gates.printDiffusion && !gates.cameraDiffusion) buffers += 1u;
    if (gates.dir) buffers += 1u;  // dirDensity (correction scratch in shared arena)
    // Shared spatial arena for halation/DIR/scanner (sequential, 3 bufs).
    if (gates.halation || gates.dir || gates.scanner || gates.grainProduction) buffers += 3u;
    return buffers * px + (gates.grainProduction ? px * 9u / 4u : 0u);
}

// Tile-memory engines retain two full-resolution buffers, while all effect
// scratch lives in one working tile. This mirrors the sliced production
// grain and the shared spatial arena in SpektraFilm::createSlots().
[[nodiscard]] inline std::uint64_t estimateFilmTileScratchBytes(std::uint32_t fullW, std::uint32_t fullH,
                                                                std::uint32_t tileW, std::uint32_t tileH,
                                                                const FilmScratchGates& gates) noexcept {
    const std::uint64_t fullPixels = std::uint64_t(fullW) * fullH;
    const std::uint64_t tilePixels = std::uint64_t(tileW) * tileH;
    std::uint64_t tileBuffers = 3u;  // filmRaw, filmDensity, dest
    if (gates.grainPreview) tileBuffers += 1u;
    if (gates.grainProduction) tileBuffers += 3u;                    // densityB, micro A/B
    if (gates.dir) tileBuffers += 1u;                                // corrected density
    if (gates.halation) tileBuffers += 1u;                           // boosted raw
    if (gates.halation || gates.cameraDiffusion) tileBuffers += 1u;  // log raw
    if (gates.cameraDiffusion) tileBuffers += 4u;                    // temp 2x, accum, camera raw
    if (gates.printDiffusion) tileBuffers += 1u;
    tileBuffers += gates.scanner ? 5u : gates.halation ? 4u : gates.dir ? 3u : gates.grainProduction ? 1u : 0u;
    std::uint64_t bytes = (2u * fullPixels + tileBuffers * tilePixels) * 16u;
    if (gates.grainProduction) bytes += tilePixels * 9u * sizeof(float);
    if (gates.cameraDiffusion) {
        const std::uint64_t downPixels = std::uint64_t(tileW / 2u + 2u) * (tileH / 2u + 2u);
        bytes += downPixels * 5u * 16u;
    }
    if (gates.halation) {
        bytes += ((fullPixels + 255u) / 256u) * 16u;  // boost maxima
    }
    return bytes;
}

}  // namespace rawrcam::develop::rendered
