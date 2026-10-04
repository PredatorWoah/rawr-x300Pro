#pragma once

#include <array>
#include <cstdint>

namespace rawrcam::camera {

// Derives Bayer-domain white-balance gains [R, Geven, Godd, B] from a
// correlated color temperature (Kelvin, clamped to [2000, 10000]) and a
// green-magenta tint in [-50, 50].
//
// The Kelvin-to-RGB illuminant (Tanner Helland) is inverted: a warm 3200K
// illuminant is red-heavy, so correction boosts blue (green/blue > 1) and
// leaves red at 1. Positive tint pushes green, matching the in-app +/-50
// convention. The (R, B) pair is ridden on the tint factor and the triplet
// is then renormalized by its minimum so every gain stays inside the NDK
// guaranteed [1.0, 4.0] range: without this the 1.0 floor would swallow the
// whole tint axis (and much of the temperature axis) in clamped regions,
// making the TINT rail appear dead.
std::array<float, 4> whiteBalanceGainsForTempTint(int32_t temperatureK, int32_t tint);

// Inverse of the above for AUTO->MANUAL seeding: finds the temp/tint whose
// forward gains best match observed HAL AWB gains (raw RGGB triple).
// Distance is measured in 3D (R, G, B): the green channel is what disam-
// biguates tint inside clamped regions where R and B coincide. Runs only on
// manual entry / snapshot publish, never per-frame. Returns
// {temperatureK, tint} clamped to the slider ranges.
std::array<int32_t, 2> whiteBalanceTempTintForGains(float gainR, float gainG, float gainB);

}  // namespace rawrcam::camera
