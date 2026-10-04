#include <cmath>
#include <iostream>

#include "tonemap/TonemapMath.h"

static float eval(float x, const tonemap::CurveCoefficients& c) {
    if (x <= c.x[0]) return 0.0f;
    if (x >= c.x[4]) return 1.0f;
    const int i = (x < c.x[1]) ? 0 : (x < c.x[2]) ? 1 : (x < c.x[3]) ? 2 : 3;
    const float t = (x - c.x[i]) / (c.x[i + 1] - c.x[i]);
    const auto q = c.cubic[i];
    return ((q[0] * t + q[1]) * t + q[2]) * t + q[3];
}
int main() {
    for (float black : {-12.f, -10.f, -8.f})
        for (float s : {-2.f, 0.f, 1.f})
            for (float ct : {.6f, 1.f, 1.5f})
                for (float shoulder : {1.f, 2.f, 2.3f})
                    for (float hb : {-1.f, 0.f, .25f}) {
                        auto p = tonemap::TonemapPresets::NeutralBaseline().params;
                        p.blackPointEV = black;
                        p.shadowLiftEV = s;
                        p.contrast = ct;
                        p.shoulderStartEV = shoulder;
                        p.highlightBiasEV = hb;
                        p.whitePointEV = 6.f;
                        auto c = tonemap::compileCurve(p);
                        float prev = eval(c.x[0] - 1, c);
                        for (int i = 1; i <= 200000; i++) {
                            float x = c.x[0] - 1 + (c.x[4] - c.x[0] + 2) * i / 200000.f;
                            float y = eval(x, c);
                            if (!std::isfinite(y) || y + 2e-6f < prev) {
                                std::cerr << "non-monotone/nonfinite\n";
                                return 1;
                            }
                            prev = y;
                        }
                        // Middle gray remains exactly anchored.
                        const auto cfg = tonemap::TonemapPresets::NeutralBaseline().config;
                        if (std::abs(eval(0.f, c) - cfg.middleGray) > 2e-6f) {
                            std::cerr << "middle gray drift\n";
                            return 2;
                        }
                    }
    {
        const auto preset = tonemap::TonemapPresets::NeutralBaseline();
        if (preset.params.exposureEV != 0.0f || preset.params.saturation != 0.0f || preset.params.vibrance != 0.0f ||
            preset.params.aePostGain != 1.0f || tonemap::kMaxAePostGain != 16.0f) {
            std::cerr << "baseline params drift\n";
            return 3;
        }
        auto custom = preset.config;
        custom.middleGray = 0.20f;
        custom.shadowAnchorEV = -2.5f;
        custom.shoulderOutputCap = 0.88f;
        auto c = tonemap::compileCurve(preset.params, custom);
        if (std::abs(eval(0.f, c) - 0.20f) > 2e-6f || std::abs(c.x[1] + 2.5f) > 1e-6f) {
            std::cerr << "config not applied\n";
            return 4;
        }
    }
    std::cout << "CPP_CONFIG_PRESET_TEST_PASS\n";
    std::cout << "CPP_CURVE_TEST_PASS\n";
}
