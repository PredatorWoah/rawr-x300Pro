#pragma once

#include <algorithm>
#include <cmath>

namespace rawr::raw_gpu_pipeline::mergemath {

struct KernelMultipliers {
    float across = 1.0f;
    float along = 1.0f;
};

inline KernelMultipliers linearKernelMultipliers(float anisotropy, float shrink, float stretch) noexcept {
    const float blend = anisotropy - 1.0f;
    return {1.0f + blend * (1.0f / shrink - 1.0f), 1.0f + blend * (stretch - 1.0f)};
}

inline float clippedIcaUpdate(float update, float searchRadius) noexcept {
    return std::clamp(update, -searchRadius, searchRadius);
}

inline int interpolationBase(float coordinate) noexcept { return static_cast<int>(std::floor(coordinate)); }

inline float sqrtGuide(float normalizedRaw) noexcept { return std::sqrt(std::max(0.0f, normalizedRaw)); }

inline float sqrtDomainNoiseVariance(float sqrtBrightness, float rawVariance) noexcept {
    const float rawSignal = sqrtBrightness * sqrtBrightness;
    const float standardDeviation =
        std::min(.25f, std::sqrt(std::max(0.0f, rawVariance)) /
                            (2.0f * std::sqrt(std::max(rawSignal, rawVariance))));
    return standardDeviation * standardDeviation;
}

struct RobustnessNoise {
    float sigmaSquared = 0.0f;
    float distanceSquared = 0.0f;
};

inline RobustnessNoise aggregateRobustnessNoise(float redVariance, float greenVariance,
                                                float blueVariance) noexcept {
    // Upstream's LUT measures the sum of three 3x3 local variances and the
    // squared distance between two independent 3x3 means. This delta-method
    // fallback keeps those exact aggregate semantics until a calibrated Monte
    // Carlo LUT is available for the active sensor noise profile.
    const float transformedVariance = redVariance + greenVariance + blueVariance;
    return {(8.0f / 9.0f) * transformedVariance, (2.0f / 9.0f) * transformedVariance};
}

}  // namespace rawr::raw_gpu_pipeline::mergemath
