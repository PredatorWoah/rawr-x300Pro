#include "color/FilmExposure.h"

#include <cassert>
#include <cmath>

int main() {
    spektrafilm_native::FilmLook look{};
    // Legacy auto-exposure settings must not change the live-camera policy.
    // Test below/at/above the cap, including negative gain compensation.
    struct Case { float gain; float expectedEv; };
    for (bool automatic : {false, true}) {
        look.autoExposure = automatic;
        for (const auto& test : {Case{0.5f, -1.0f}, Case{1.0f, 0.0f},
                                 Case{2.0f, 1.0f}, Case{8.0f, 1.0f}}) {
            assert(std::abs(rawrcam::color::filmExposureEv(look, test.gain) - test.expectedEv) < 1e-6f);
        }
    }
    assert(rawrcam::color::filmExposureEv(look, 1.0f) == 0.0f);
    assert(rawrcam::color::filmExposureEv(look, 0.5f) == -1.0f);
    look.filmExposureEv = 0.75f;
    assert(rawrcam::color::filmExposureEv(look, 8.0f) == 1.75f);
    look.filmExposureEv = 10.0f;
    assert(rawrcam::color::filmExposureEv(look, 8.0f) == 10.0f);
    look.filmExposureEv = -10.0f;
    assert(rawrcam::color::filmExposureEv(look, 0.0f) == -10.0f);
}
