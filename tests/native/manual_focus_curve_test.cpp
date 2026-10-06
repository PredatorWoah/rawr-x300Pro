#include <cassert>
#include <cmath>
#include <iostream>

#include "camera/ManualFocusCurve.h"

using rawrcam::camera::manualFocusDiopters;
using rawrcam::camera::manualFocusNormalized;

namespace {

bool near(float a, float b, float eps = 1e-4f) { return std::fabs(a - b) <= eps; }

void testEndpoints() {
    const float minD = 10.0f;  // closest focus 10 cm
    assert(near(manualFocusDiopters(0.0f, minD), minD));
    assert(near(manualFocusDiopters(1.0f, minD), 0.0f));
    // Out-of-range rail values clamp instead of extrapolating.
    assert(near(manualFocusDiopters(-1.0f, minD), minD));
    assert(near(manualFocusDiopters(2.0f, minD), 0.0f));
    // A fixed-focus camera has no travel.
    assert(near(manualFocusDiopters(0.5f, 0.0f), 0.0f));
}

void testRoundTrip() {
    const float minD = 10.0f;
    for (float p = 0.0f; p <= 1.0f; p += 0.05f) {
        assert(near(manualFocusNormalized(manualFocusDiopters(p, minD), minD), p, 1e-3f));
    }
    assert(near(manualFocusNormalized(0.0f, minD), 1.0f));
    assert(near(manualFocusNormalized(minD, minD), 0.0f));
    assert(near(manualFocusNormalized(50.0f, minD), 0.0f));  // beyond the near limit clamps
    assert(near(manualFocusNormalized(1.0f, 0.0f), 1.0f));    // unknown range reads as infinity
}

// The reason for the curve: with a rail linear in diopters, 3 m to infinity is the last 3% of travel.
void testFarRangeHasUsableTravel() {
    const float minD = 10.0f;
    const float at3m = manualFocusNormalized(1.0f / 3.0f, minD);
    const float at30m = manualFocusNormalized(1.0f / 30.0f, minD);
    std::cout << "3 m at " << at3m << ", 30 m at " << at30m << "\n";
    const float linearAt3m = 1.0f - (1.0f / 3.0f) / minD;
    assert(1.0f - linearAt3m < 0.05f);  // the old mapping: 3% of the rail
    assert(1.0f - at3m > 0.25f);        // now about a third of it
    assert(at30m > at3m + 0.1f);        // 3 m and 30 m are clearly separate positions
    // Each percent of rail out to about 40 m changes the distance by a bounded factor, so no single nudge can jump from
    // a few metres to infinity.
    for (float p = 0.5f; p < 0.85f; p += 0.01f) {
        const float d0 = 1.0f / manualFocusDiopters(p, minD);
        const float d1 = 1.0f / manualFocusDiopters(p + 0.01f, minD);
        assert(d1 / d0 < 1.26f);
    }
}

void testMonotonic() {
    float last = manualFocusDiopters(0.0f, 10.0f);
    for (float p = 0.01f; p <= 1.0f; p += 0.01f) {
        const float d = manualFocusDiopters(p, 10.0f);
        assert(d <= last + 1e-6f);
        last = d;
    }
}

}  // namespace

int main() {
    testEndpoints();
    testRoundTrip();
    testFarRangeHasUsableTravel();
    testMonotonic();
    std::cout << "manual_focus_curve_test passed\n";
    return 0;
}
