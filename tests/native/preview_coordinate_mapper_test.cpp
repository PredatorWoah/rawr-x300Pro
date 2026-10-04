#include "geometry/DisplayToSensorMapper.h"
#include <cassert>
#include <cmath>

static bool near(float a, float b) { return std::fabs(a-b) < 1e-5f; }
int main() {
    using rawrcam::geometry::displayToSource;
    // 2040x1532 source rotated 90 degrees into ~3:4 portrait display.
    auto c = displayToSource(0.5f, 0.5f, 2040, 1532, 1440, 1920, 90);
    assert(c && near(c->x, 0.5f) && near(c->y, 0.5f));
    // The real source/display aspect differs by ~0.1%, so extreme top/bottom pixels are
    // letterboxed. Test horizontal edges at vertical center instead.
    auto left = displayToSource(0.0f, 0.5f, 2040, 1532, 1440, 1920, 90);
    assert(left);
    // Present shader mapping at quarterTurns=1: source=(q.y,1-q.x).
    assert(near(left->x, 0.5f));
    assert(near(left->y, 1.0f));
    auto right = displayToSource(1.0f, 0.5f, 2040, 1532, 1440, 1920, 90);
    assert(right && near(right->x, 0.5f) && near(right->y, 0.0f));
    return 0;
}
