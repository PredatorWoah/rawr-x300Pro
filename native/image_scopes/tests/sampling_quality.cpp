#include <algorithm>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <numeric>
#include <string>
#include <vector>

#include "image_scopes_cpu.h"
using namespace image_scopes;
using namespace image_scopes::cpu;
struct Pattern {
    std::string name;
    std::vector<std::uint8_t> px;
    std::uint32_t w, h;
};
static Pattern make(std::string n, std::uint32_t w, std::uint32_t h, int type) {
    Pattern p{std::move(n), std::vector<std::uint8_t>(static_cast<std::size_t>(w) * h * 4), w, h};
    for (std::uint32_t y = 0; y < h; ++y)
        for (std::uint32_t x = 0; x < w; ++x) {
            auto* q = &p.px[(static_cast<std::size_t>(y) * w + x) * 4];
            std::uint8_t r = 32, g = 32, b = 32;
            if (type == 0 && x == w / 2) r = g = b = 255;
            if (type == 1 && ((x / 2) & 1u)) r = g = b = 255;
            if (type == 2 && ((x + y) & 1u)) r = g = b = 255;
            if (type == 3) {
                std::uint32_t v = ((x * 13u + y * 17u + (x * y) % 23u) & 31u);
                r = g = b = static_cast<std::uint8_t>(v * 8u);
            }
            if (type == 4 && x >= w / 2 && x < w / 2 + 3 && y >= h / 2 && y < h / 2 + 7) {
                r = 255;
                g = 0;
                b = 0;
            }
            if (type == 5) {
                r = static_cast<std::uint8_t>((x * 255u) / (w - 1u));
                g = static_cast<std::uint8_t>((y * 255u) / (h - 1u));
                b = static_cast<std::uint8_t>(((x + y) * 255u) / (w + h - 2u));
            }
            q[0] = r;
            q[1] = g;
            q[2] = b;
            q[3] = 255;
        }
    return p;
}
static double normalizedL1(const std::vector<std::uint32_t>& ref, const std::vector<std::uint32_t>& s, std::uint32_t nr,
                           std::uint32_t ns) {
    double e = 0.;
    for (std::size_t i = 0; i < ref.size(); ++i) e += std::abs(double(ref[i]) / nr - double(s[i]) / ns);
    return e;
}
int main() {
    std::vector<Pattern> ps;
    ps.push_back(make("one_pixel_vertical", 257, 193, 0));
    ps.push_back(make("two_pixel_periodic", 257, 193, 1));
    ps.push_back(make("checkerboard", 257, 193, 2));
    ps.push_back(make("moire_like", 257, 193, 3));
    ps.push_back(make("small_saturated_patch", 257, 193, 4));
    ps.push_back(make("rgb_gradient", 257, 193, 5));
    for (auto& p : ps) {
        Rgba8ImageView im{p.px.data(), p.w, p.h, p.w * 4};
        auto wr = measureDisplayWaveform(im, WaveformMode::RgbOverlay, SamplingMode::FullReference);
        auto lr = measureDisplayWaveform(im, WaveformMode::Luma, SamplingMode::FullReference);
        auto vr = measureVectorscope(im, SamplingMode::FullReference);
        std::cout << p.name;
        for (auto m : {SamplingMode::Production25, SamplingMode::Production50}) {
            auto w = measureDisplayWaveform(im, WaveformMode::RgbOverlay, m);
            auto l = measureDisplayWaveform(im, WaveformMode::Luma, m);
            auto v = measureVectorscope(im, m);
            double we =
                normalizedL1(wr.density, w.density, wr.sampling.sampledPixelCount, w.sampling.sampledPixelCount);
            double le =
                normalizedL1(lr.density, l.density, lr.sampling.sampledPixelCount, l.sampling.sampledPixelCount);
            double ve =
                normalizedL1(vr.density, v.density, vr.sampling.sampledPixelCount, v.sampling.sampledPixelCount);
            std::cout << (m == SamplingMode::Production25 ? " 25" : " 50") << " wave_rgb_L1=" << we
                      << " wave_luma_L1=" << le << " vector_L1=" << ve << " sampled=" << w.sampling.sampledPixelCount
                      << "/" << w.sampling.sourcePixelCount;
        }
        std::cout << "\n";
    }
}
