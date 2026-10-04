#pragma once

#include <algorithm>
#include <cstdint>
#include <limits>

namespace rawrcam::renderer {

// Filter a same-color Bayer lattice at a common 2X center for all four CFA
// sites. The caller supplies linear, black-subtracted and optionally shaded
// samples; averaging stored integer DNG codes would mishandle linearization
// tables and spatial gain maps. Output CFA phase is identical to input phase.
template <class Sample>
float binSameColor2x(uint32_t x, uint32_t y, uint32_t sourceWidth, uint32_t sourceHeight, Sample sample) {
    auto axis = [](uint32_t coordinate, int32_t (&positions)[5], int32_t (&weights)[5]) {
        const int32_t center = int32_t(coordinate * 2u);
        if (coordinate & 1u) {
            positions[0] = center - 3;
            positions[1] = center - 1;
            positions[2] = center + 1;
            positions[3] = center + 3;
            weights[0] = 1;
            weights[1] = 19;
            weights[2] = 19;
            weights[3] = 1;
            return 4u;
        }
        positions[0] = center - 4;
        positions[1] = center - 2;
        positions[2] = center;
        positions[3] = center + 2;
        positions[4] = center + 4;
        weights[0] = weights[4] = -2;
        weights[1] = weights[3] = 5;
        weights[2] = 14;
        return 5u;
    };
    auto clampSite = [](int32_t position, uint32_t size, uint32_t parity) {
        return uint32_t(std::clamp(position, int32_t(parity), int32_t(size - 2u + parity)));
    };
    int32_t xs[5]{}, ys[5]{};
    int32_t wx[5]{}, wy[5]{};
    const auto nx = axis(x, xs, wx), ny = axis(y, ys, wy);
    double value = 0.0;
    double lo = std::numeric_limits<double>::infinity();
    double hi = -std::numeric_limits<double>::infinity();
    for (uint32_t j = 0; j < ny; ++j)
        for (uint32_t i = 0; i < nx; ++i) {
            const double pixel = sample(clampSite(xs[i], sourceWidth, x & 1u),
                                        clampSite(ys[j], sourceHeight, y & 1u));
            value += double(wx[i] * wy[j]) * pixel;
            lo = std::min(lo, pixel);
            hi = std::max(hi, pixel);
        }
    // Match rawr-dng's sharp kernel, including its local overshoot bound.
    return float(std::clamp(value / ((x & 1u ? 40.0 : 20.0) *
                                    (y & 1u ? 40.0 : 20.0)), lo, hi));
}

}  // namespace rawrcam::renderer
