#include <chrono>
#include <cstdint>
#include <iostream>
#include <vector>

#include "image_scopes_cpu.h"
using namespace image_scopes;
using namespace image_scopes::cpu;
template <class F>
double bench(F&& f, int n = 20) {
    for (int i = 0; i < 3; ++i) f();
    auto a = std::chrono::steady_clock::now();
    for (int i = 0; i < n; ++i) f();
    auto b = std::chrono::steady_clock::now();
    return std::chrono::duration<double, std::milli>(b - a).count() / n;
}
int main() {
    for (auto [w, h] : {std::pair{2048u, 1536u}, std::pair{2040u, 1532u}, std::pair{2040u, 1536u}}) {
        std::vector<std::uint8_t> a(static_cast<std::size_t>(w) * h * 4);
        std::uint32_t s = 1;
        for (auto& v : a) {
            s = s * 1664525u + 1013904223u;
            v = static_cast<std::uint8_t>(s >> 24);
        }
        Rgba8ImageView im{a.data(), w, h, w * 4};
        auto tw25 = bench(
            [&] {
                auto x = measureDisplayWaveform(im, WaveformMode::RgbOverlay, SamplingMode::Production25);
                (void)x;
            },
            5);
        auto tw50 = bench(
            [&] {
                auto x = measureDisplayWaveform(im, WaveformMode::RgbOverlay, SamplingMode::Production50);
                (void)x;
            },
            5);
        auto tv25 = bench(
            [&] {
                auto x = measureVectorscope(im, SamplingMode::Production25);
                (void)x;
            },
            5);
        std::cout << w << "x" << h << " CPU wave25_ms=" << tw25 << " wave50_ms=" << tw50 << " vector25_ms=" << tv25
                  << "\n";
    }
}
