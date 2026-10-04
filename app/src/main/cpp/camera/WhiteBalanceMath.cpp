#include "camera/WhiteBalanceMath.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace rawrcam::camera {
namespace {
constexpr int32_t kWbTempMinK = 2000;
constexpr int32_t kWbTempMaxK = 10000;
constexpr int32_t kWbTintMin = -50;
constexpr int32_t kWbTintMax = 50;

float clampGain(float v) { return std::clamp(v, 1.0f, 4.0f); }

}  // namespace

std::array<float, 4> whiteBalanceGainsForTempTint(int32_t temperatureK, int32_t tint) {
    const float temp = static_cast<float>(std::clamp(temperatureK, kWbTempMinK, kWbTempMaxK)) / 100.0f;
    // Tanner Helland Kelvin-to-RGB approximation for the illuminant color.
    // Correction gains are the inverse: boost the complement of the
    // illuminant (warm light -> boost blue, cool light -> boost red),
    // normalized to green = 1.
    float illuminantR;
    if (temp <= 66.0f) {
        illuminantR = 255.0f;
    } else {
        illuminantR = 329.698727446f * std::pow(temp - 60.0f, -0.1332047592f);
    }
    float illuminantB;
    if (temp >= 66.0f) {
        illuminantB = 255.0f;
    } else if (temp <= 19.0f) {
        illuminantB = 0.0f;
    } else {
        illuminantB = 138.5177312231f * std::log(temp - 10.0f) - 305.0447927307f;
    }
    float illuminantG = temp <= 66.0f ? 99.4708025861f * std::log(temp) - 161.1195681661f : 255.0f;
    illuminantR = std::clamp(illuminantR, 1.0f, 255.0f);
    illuminantG = std::clamp(illuminantG, 1.0f, 255.0f);
    illuminantB = std::clamp(illuminantB, 1.0f, 255.0f);
    // Ride tint on the red/blue pair, then renormalize the triplet by its
    // minimum so all gains land in [1.0, 4.0]. Positive tint pushes green
    // (negative magenta), matching the +/-50 tint convention; the
    // quarter-stop span keeps the slider photographic. Renormalization (not
    // a fixed G=1) is what keeps tint visible: otherwise the 1.0 clamp floor
    // would pin R across the whole warm range and swallow the axis.
    const float clampedTint = static_cast<float>(std::clamp(tint, kWbTintMin, kWbTintMax));
    const float tintFactor = std::pow(2.0f, -clampedTint / 50.0f * 0.25f);
    const float rawR = (illuminantG / illuminantR) * tintFactor;
    const float rawB = (illuminantG / illuminantB) * tintFactor;
    const float norm = std::min({rawR, rawB, 1.0f});
    const float r = clampGain(rawR / norm);
    const float g = clampGain(1.0f / norm);
    const float b = clampGain(rawB / norm);
    return {r, g, g, b};
}

std::array<int32_t, 2> whiteBalanceTempTintForGains(float gainR, float gainG, float gainB) {
    // Fast inverse for seeding only (manual entry / snapshot, never per-frame
    // or per-request). Coarse-to-fine over the quantized slider grid:
    // temp 2000..10000 step 500 x tint -50..50 step 10, then refine around the
    // best to 100K / 1-tint. ~200 forward evals total, each a few pow/log.
    // Distance is 3D over (R, G, B): green disambiguates tint where R/B clamp.
    if (!std::isfinite(gainR) || !std::isfinite(gainG) || !std::isfinite(gainB)) return {5200, 0};
    const float targetR = std::clamp(gainR, 1.0f, 4.0f);
    const float targetG = std::clamp(gainG, 1.0f, 4.0f);
    const float targetB = std::clamp(gainB, 1.0f, 4.0f);
    int32_t bestTemp = 5200;
    int32_t bestTint = 0;
    float bestDist = std::numeric_limits<float>::max();
    auto consider = [&](int32_t tempK, int32_t tint) {
        const auto gains = whiteBalanceGainsForTempTint(tempK, tint);
        const float dr = gains[0] - targetR;
        const float dg = gains[1] - targetG;
        const float db = gains[3] - targetB;
        const float dist = dr * dr + dg * dg + db * db;
        if (dist < bestDist) {
            bestDist = dist;
            bestTemp = tempK;
            bestTint = tint;
        }
    };
    for (int32_t tempK = kWbTempMinK; tempK <= kWbTempMaxK; tempK += 500) {
        for (int32_t tint = kWbTintMin; tint <= kWbTintMax; tint += 10) {
            consider(tempK, tint);
        }
    }
    const int32_t coarseTemp = bestTemp;
    const int32_t coarseTint = bestTint;
    for (int32_t tempK = std::max(kWbTempMinK, coarseTemp - 500); tempK <= std::min(kWbTempMaxK, coarseTemp + 500);
         tempK += 100) {
        for (int32_t tint = std::max(kWbTintMin, coarseTint - 10); tint <= std::min(kWbTintMax, coarseTint + 10);
             tint += 1) {
            consider(tempK, tint);
        }
    }
    return {bestTemp, bestTint};
}

}  // namespace rawrcam::camera
