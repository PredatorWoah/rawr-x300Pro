// Unit test for the film-engine memory gate estimator. The gate decides
// between a multi-GB film build and a graceful tonemap fallback, so its
// arithmetic must match createSlots() gating exactly.
#include <cassert>
#include <cstdint>
#include <iostream>

#include "develop/render/FilmScratchEstimate.h"

using namespace rawrcam::develop::rendered;

namespace {
std::uint64_t px(std::uint32_t w, std::uint32_t h) { return std::uint64_t(w) * h * 16u; }
}  // namespace

int main() {
    FilmScratchGates none{};
    // Minimal still engine: source, destination, filmRaw, filmDensity.
    // DIR is gated on amount>0 (P0), shared arena only when needed.
    assert(estimateFilmScratchBytes(4080, 3064, none) == 4u * px(4080, 3064));
    assert(estimateFilmScratchBytes(4080, 3064, none) == 800071680u);
    // Preview grain adds exactly one buffer.
    FilmScratchGates previewGrain{};
    previewGrain.grainPreview = true;
    assert(estimateFilmScratchBytes(4080, 3064, previewGrain) == 5u * px(4080, 3064));
    // DIR adds density + shared arena.
    FilmScratchGates dirOnly{};
    dirOnly.dir = true;
    assert(estimateFilmScratchBytes(4080, 3064, dirOnly) == 8u * px(4080, 3064));
    // Production grain: 11 vec4 buffers plus nine scalar float layers per pixel.
    // Sliced blur reuses the three-buffer shared spatial arena.
    FilmScratchGates prodGrain{};
    prodGrain.grainPreview = true;
    prodGrain.grainProduction = true;
    assert(estimateFilmScratchBytes(4080, 3064, prodGrain) == 53u * px(4080, 3064) / 4u);
    // Incident config class (20MP, grain + DIR + halation): was 15 bufs
    // (>4GiB) before P1 aliasing, now 11 bufs (~3.3GiB, fits 4GB devices).
    FilmScratchGates incident{};
    incident.grainPreview = true;
    incident.halation = true;
    incident.dir = true;
    const std::uint64_t incidentBytes = estimateFilmScratchBytes(5158, 3874, incident);
    assert(incidentBytes == 11u * px(5158, 3874));
    assert(incidentBytes < 4ull * 1024 * 1024 * 1024);
    assert(incidentBytes > 1ull * 1024 * 1024 * 1024);
    // All effects: 20 vec4 buffers plus nine scalar float layers per pixel.
    FilmScratchGates all{true, true, true, true, true, true, true};
    const std::uint64_t huge = estimateFilmScratchBytes(8192, 8192, all);
    assert(huge == 89u * px(8192, 8192) / 4u);
    assert(huge < (1ull << 63));
    // Monotonic in geometry.
    assert(estimateFilmScratchBytes(4096, 3072, none) > estimateFilmScratchBytes(2048, 1536, none));
    std::cout << "Film scratch estimate tests passed\n";
}
