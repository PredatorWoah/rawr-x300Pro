// Host-side validation for the sharpness reference + selection policy.
// No GPU required.
//
// Build with the module (RAW_SHARPNESS_BUILD_TESTS=ON) and run via ctest.

#include "raw_sharpness/raw_sharpness_types.hpp"

#include <cstdint>
#include <cstdio>
#include <vector>

namespace {

int failures = 0;

void check(bool condition, const char* message) {
    if (condition) {
        std::printf("PASS %s\n", message);
    } else {
        std::printf("FAIL %s\n", message);
        ++failures;
    }
}

}  // namespace

int main() {
    using namespace raw_sharpness;
    using namespace raw_sharpness::reference;

    constexpr std::uint32_t kWidth = 128;
    constexpr std::uint32_t kHeight = 128;
    std::vector<std::uint8_t> sharp(kWidth * kHeight * 2u);
    std::vector<std::uint8_t> blur(kWidth * kHeight * 2u);
    auto* s = reinterpret_cast<std::uint16_t*>(sharp.data());
    auto* b = reinterpret_cast<std::uint16_t*>(blur.data());
    for (std::uint32_t y = 0; y < kHeight; ++y) {
        for (std::uint32_t x = 0; x < kWidth; ++x) {
            const std::uint16_t v = (((x / 2u) + (y / 2u)) & 1u) ? 4000 : 1000;
            s[y * kWidth + x] = v;
            b[y * kWidth + x] = static_cast<std::uint16_t>(2500 + (int(v) - 2500) / 4);
        }
    }
    const float sharpScore = scoreCpu(sharp.data(), kWidth, kHeight, BayerPattern::RGGB, 65535.f);
    const float blurScore = scoreCpu(blur.data(), kWidth, kHeight, BayerPattern::RGGB, 65535.f);
    std::printf("sharp=%f blur=%f\n", sharpScore, blurScore);
    check(sharpScore > blurScore && sharpScore > 0.f && blurScore >= 0.f, "sharp outscores blur");

    std::vector<std::uint8_t> flat(kWidth * kHeight * 2u, 0);
    auto* f = reinterpret_cast<std::uint16_t*>(flat.data());
    for (std::uint32_t i = 0; i < kWidth * kHeight; ++i) f[i] = 2000;
    check(scoreCpu(flat.data(), kWidth, kHeight, BayerPattern::RGGB, 65535.f) == 0.f, "flat field scores zero");

    for (std::uint32_t cfa = 0; cfa < 4u; ++cfa) {
        check(scoreCpu(sharp.data(), kWidth, kHeight, static_cast<BayerPattern>(cfa), 65535.f) > 0.f,
              "all CFA patterns score");
    }
    check(scoreCpu(nullptr, kWidth, kHeight, BayerPattern::RGGB, 65535.f) == 0.f, "null input scores zero");
    check(scoreCpu(sharp.data(), 4u, 4u, BayerPattern::RGGB, 65535.f) == 0.f, "tiny frame scores zero");

    check(selectSharpest({1.f, 5.f, 2.f}, 1) == 1, "argmax wins");
    check(selectSharpest({1.f, 1.f, 1.f}, 1) == 1, "tie keeps middle");
    check(selectSharpest({5.f, 1.f, 1.f}, 1) == 0, "off-middle winner found");
    check(selectSharpest({-1.f, -1.f}, 1) == 1, "degenerate keeps middle");
    check(selectSharpest({}, 0) == 0, "empty keeps middle");

    if (failures == 0) std::printf("RAW_SHARPNESS_CPU_VALIDATION_OK\n");
    return failures;
}
