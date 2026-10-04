#include "color/AePostGainCurve.h"

#include <cassert>
#include <cmath>

int main() {
    using rawrcam::color::effectiveAePostGain;
    // Identity below/at cap.
    assert(std::abs(effectiveAePostGain(100, 4.0f) - 1.0f) < 1e-6f);
    assert(std::abs(effectiveAePostGain(200, 4.0f) - 2.0f) < 1e-6f);
    assert(std::abs(effectiveAePostGain(400, 4.0f) - 4.0f) < 1e-5f);
    // Soft knee above cap: 8x raw with 4x cap -> ~6.20x.
    {
        const float eff = effectiveAePostGain(800, 4.0f);
        assert(eff > 4.0f && eff < 8.0f);
        assert(std::abs(eff - 6.20f) < 0.05f);
    }
    // 16x raw with 4x cap -> ~7.3x, still under tonemap ceiling.
    {
        const float eff = effectiveAePostGain(1600, 4.0f);
        assert(eff > 4.0f && eff <= 16.0f);
        assert(std::abs(eff - 7.3f) < 0.1f);
    }
    // 2x cap: raw 4x -> ~2.86x? rawEV=2, capEV=1, excess=1 -> 1+1*(1-e^-1)=1.632EV -> 3.10x.
    {
        const float eff = effectiveAePostGain(400, 2.0f);
        assert(eff > 2.0f && eff < 4.0f);
        assert(std::abs(eff - 3.10f) < 0.05f);
    }
    // 1x cap is hard: no knee.
    assert(std::abs(effectiveAePostGain(800, 1.0f) - 1.0f) < 1e-6f);
    assert(std::abs(effectiveAePostGain(1600, 1.0f) - 1.0f) < 1e-6f);
    // Knee width: High (0.5EV) compresses harder, Low (2EV) lifts more.
    {
        const float high = effectiveAePostGain(1600, 4.0f, 0.5f);
        const float normal = effectiveAePostGain(1600, 4.0f, 1.0f);
        const float low = effectiveAePostGain(1600, 4.0f, 2.0f);
        assert(high < normal && normal < low);
        assert(std::abs(high - 5.62f) < 0.06f);
        assert(std::abs(low - 9.61f) < 0.08f);
        assert(low <= 16.0f);
    }
    // Width clamps: huge widths behave like the 4EV clamp bound.
    assert(effectiveAePostGain(1600, 4.0f, 100.0f) <= 16.0f);
    // Monotonic in raw.
    {
        float prev = 0.0f;
        for (int boost : {100, 150, 200, 300, 400, 600, 800, 1600}) {
            const float eff = effectiveAePostGain(boost, 4.0f);
            assert(eff >= prev);
            prev = eff;
        }
    }
    // C1 at hinge: derivative ~1 just above cap (finite difference).
    {
        const float at = effectiveAePostGain(400, 4.0f);
        const float above = effectiveAePostGain(410, 4.0f);
        const float slope = (above - at) / 0.1f;  // per linear gain
        assert(slope > 0.7f && slope < 1.1f);
    }
    return 0;
}
