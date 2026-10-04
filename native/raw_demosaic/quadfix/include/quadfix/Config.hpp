#pragma once
#include "Types.hpp"
#include <cstdint>

namespace quadfix {

// Frozen adapt004 parameters. Oracle:
//   .cache/renderer-grid-20260921/host-comparison/adaptive_guided.py
//   (guide_median / texture_mask / adaptive) and
//   .cache/renderer-grid-20260921/full50_guided/filter_full_adapt.py
// Working domain is 0..255 float, thresholds are DN codes in that domain.
struct PipelineConfig {
    uint32_t width = 0;   // full-res tile width incl. halo, must be % 16 == 0
    uint32_t height = 0;  // full-res tile height incl. halo, must be % 16 == 0

    // Guide variant: false = exact 9x9 median (sorting network, default),
    // true = separable row-then-column 9-tap median. Python A/B
    // (spatial_kernel_check.py, section B) shows fast-median end-to-end sky
    // delta rms 0.018 DN / max 0.20 DN vs exact; both within gates.
    bool fastMedian = false;

    // Frozen filter constants (DO NOT RETUNE without re-running the oracle
    // validation in spatial_kernel_check.py sections D/E/G):
    //   guide 9x9 median, notch sigma 0.004 cycles/sample over the 15
    //   non-DC 4x4 lattice points, E_FLAT 1.5, E_TEX 5.0, mask smooth sigma 6.
    // The FFT notch is realized as a Gabor bank: demodulate the 15 carriers,
    // 8x8 block-mean downsample, coarse Gaussian sigma ~= 4.97 (== 39.79/8),
    // repeat upsample, remodulate and subtract. Validated vs FFT oracle:
    // sky rms 0.019 DN, end-to-end sky out delta rms 0.009 DN (section G).

    uint32_t workgroupX = 16;
    uint32_t workgroupY = 16;
};

struct PipelineAssets {};

}  // namespace quadfix
