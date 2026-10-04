#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <numeric>
#include <stdexcept>
#include <vector>

#include "image_scopes_cpu.h"
using namespace image_scopes;
using namespace image_scopes::cpu;
static void req(bool v, const char* s) {
    if (!v) throw std::runtime_error(s);
}
static std::vector<std::uint8_t> solid(std::uint32_t w, std::uint32_t h, std::array<std::uint8_t, 4> c) {
    std::vector<std::uint8_t> a(static_cast<std::size_t>(w) * h * 4);
    for (std::size_t i = 0; i < a.size(); i += 4)
        std::copy(c.begin(), c.end(), a.begin() + static_cast<std::ptrdiff_t>(i));
    return a;
}
static Rgba8ImageView view(const std::vector<std::uint8_t>& a, std::uint32_t w, std::uint32_t h) {
    return {a.data(), w, h, w * 4};
}
int main() {
    try {
        req(encodedLumaQ16(255, 255, 255) == 255, "white luma");
        req(encodedLumaQ16(0, 0, 0) == 0, "black luma");
        {
            std::uint8_t cb = 0, cr = 0;
            bt709DerivedCbCrQ16(128, 128, 128, cb, cr);
            req(cb == 128 && cr == 128, "neutral vectorscope center");
            std::uint8_t rcb = 0, rcr = 0, bcb = 0, bcr = 0;
            bt709DerivedCbCrQ16(255, 0, 0, rcb, rcr);
            bt709DerivedCbCrQ16(0, 0, 255, bcb, bcr);
            req(rcr > 128 && bcb > 128, "primary chroma directions");
        }
        {
            auto a = solid(64, 64, {128, 128, 128, 255});
            for (auto m : {SamplingMode::FullReference, SamplingMode::Production50, SamplingMode::Production25}) {
                auto w = measureDisplayWaveform(view(a, 64, 64), WaveformMode::Luma, m);
                auto sum = std::accumulate(w.density.begin(), w.density.end(), std::uint64_t{0});
                req(sum == w.sampling.sampledPixelCount, "wave sampled count conservation");
                req(w.sampling.sourcePixelCount == 4096, "wave source count");
                auto v = measureVectorscope(view(a, 64, 64), m);
                auto vs = std::accumulate(v.density.begin(), v.density.end(), std::uint64_t{0});
                req(vs == v.sampling.sampledPixelCount, "vector sampled count conservation");
                req(v.density[128u * 256u + 128u] == v.sampling.sampledPixelCount, "gray vector center");
            }
        }
        {
            std::uint32_t c25 = 0, c50 = 0;
            for (std::uint32_t y = 0; y < 64; ++y)
                for (std::uint32_t x = 0; x < 64; ++x) {
                    c25 += sampleSelected(x, y, SamplingMode::Production25);
                    c50 += sampleSelected(x, y, SamplingMode::Production50);
                }
            req(c25 == 1024, "25 percent sampler exact complete blocks");
            req(c50 == 2048, "50 percent sampler exact complete blocks");
        }
        {
            const std::uint32_t w = 63, h = 65;
            std::vector<std::uint8_t> a(static_cast<std::size_t>(w) * h * 4);
            for (std::uint32_t y = 0; y < h; ++y)
                for (std::uint32_t x = 0; x < w; ++x) {
                    auto* p = &a[(static_cast<std::size_t>(y) * w + x) * 4];
                    p[0] = p[1] = p[2] = static_cast<std::uint8_t>((x * 255u) / (w - 1u));
                    p[3] = 255;
                }
            auto wf = measureDisplayWaveform(view(a, w, h), WaveformMode::Luma, SamplingMode::FullReference);
            req(wf.sampling.sampledPixelCount == w * h, "awkward waveform source count");
        }
        std::cout << "IMAGE_SCOPES_CPU_VALIDATION_PASS\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "FAIL: " << e.what() << "\n";
        return 1;
    }
}
