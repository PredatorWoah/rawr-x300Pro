#include <cmath>
#include <cstdio>
#include <vector>

#include "renderer/LinearResample.h"
int main() {
    constexpr unsigned w = 256, h = 192;
    std::vector<_Float16> input(w * h * 4), output(90 * 68 * 4);
    for (unsigned y = 0; y < h; ++y)
        for (unsigned x = 0; x < w; ++x)
            for (unsigned c = 0; c < 4; ++c) input[(y * w + x) * 4 + c] = c == 3 ? 1 : _Float16(.25);
    rawrcam::renderer::resampleLinear(input.data(), w, h, output.data(), 90, 68, {0, 0, w, h}, [](auto) {});
    for (unsigned i = 0; i < 90 * 68; ++i)
        for (unsigned c = 0; c < 3; ++c)
            if (std::abs(float(output[i * 4 + c]) - .25f) > .0005f) return 1;
    for (unsigned y = 0; y < h; ++y)
        for (unsigned x = 0; x < w; ++x)
            for (unsigned c = 0; c < 3; ++c) input[(y * w + x) * 4 + c] = x > y ? 1 : 0;
    rawrcam::renderer::resampleLinear(input.data(), w, h, output.data(), 90, 68, {0, 0, w, h}, [](auto) {});
    for (unsigned i = 0; i < 90 * 68; ++i) {
        for (unsigned c = 0; c < 3; ++c)
            if (!std::isfinite(float(output[i * 4 + c])) || output[i * 4 + c] < 0 || output[i * 4 + c] > 1) return 2;
        if (output[i * 4] != output[i * 4 + 1] || output[i * 4] != output[i * 4 + 2]) return 3;
    }
    // A source-Nyquist checkerboard must average to gray, not alias into a
    // low-frequency pattern when reduced by the 200MP -> 25MP ratio.
    for (unsigned y = 0; y < h; ++y)
        for (unsigned x = 0; x < w; ++x)
            for (unsigned c = 0; c < 3; ++c) input[(y * w + x) * 4 + c] = (x + y) % 2;
    rawrcam::renderer::resampleLinear(input.data(), w, h, output.data(), 90, 68, {0, 0, w, h}, [](auto) {});
    for (unsigned y = 4; y < 64; ++y)
        for (unsigned x = 4; x < 86; ++x)
            if (std::abs(float(output[(y * 90 + x) * 4]) - .5f) > .002f) return 4;
    puts("RENDERER_RESAMPLE_PASS");
}
