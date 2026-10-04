#include <cmath>
#include <cstdio>

#include "renderer/BayerBin.h"

int main() {
    using rawrcam::renderer::binSameColor2x;
    constexpr uint32_t width = 128, height = 96;
    auto constant = [](uint32_t x, uint32_t y) { return float((x & 1u) + 2u * (y & 1u)); };
    for (uint32_t y = 0; y < height / 2; ++y)
        for (uint32_t x = 0; x < width / 2; ++x)
            if (std::abs(binSameColor2x(x, y, width, height, constant) - constant(x, y)) > 1e-6f) return 1;

    // Every parity must have the same edge center. A constant-center filter
    // also suppresses a source-period-4 checkerboard on each color lattice.
    for (uint32_t parityY = 0; parityY < 2; ++parityY)
        for (uint32_t parityX = 0; parityX < 2; ++parityX) {
            auto edge = [](uint32_t x, uint32_t) { return x >= 64 ? 1.0f : 0.0f; };
            const float a = binSameColor2x(30u + parityX, 20u + parityY, width, height, edge);
            const float b = binSameColor2x(32u + parityX, 20u + parityY, width, height, edge);
            if (a < 0.0f || a > 1.0f || b < 0.0f || b > 1.0f || a > b) return 2;
        }
    // At a vertical edge the sharp kernel places 85% of the bright side
    // into its centered even site, matching rawr-dng's selected filter.
    auto edge = [](uint32_t x, uint32_t) { return x >= 64 ? 1.0f : 0.0f; };
    if (std::abs(binSameColor2x(32, 20, width, height, edge) - 0.85f) > 1e-6f) return 4;
    auto ramp = [](uint32_t x, uint32_t y) { return float(x + 2u * y); };
    for (uint32_t y = 3; y < height / 2 - 3; ++y)
        for (uint32_t x = 3; x < width / 2 - 3; ++x)
            if (std::abs(binSameColor2x(x, y, width, height, ramp) - ramp(2u * x, 2u * y)) > 1e-4f)
                return 5;
    auto neutral = [](uint32_t x, uint32_t y) { return float((x / 2u + y / 2u) & 1u); };
    for (uint32_t y = 3; y < height / 2 - 3; ++y)
        for (uint32_t x = 3; x < width / 2 - 3; ++x)
            if (std::abs(binSameColor2x(x, y, width, height, neutral) - .5f) > .001f) return 3;
    puts("RENDERER_BAYER_BIN_PASS");
}
