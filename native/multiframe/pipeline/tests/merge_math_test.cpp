#include "rawr/raw_gpu_pipeline/MergeMath.h"

#include <cassert>
#include <cmath>

int main() {
    namespace m = rawr::raw_gpu_pipeline::mergemath;
    const auto isotropic = m::linearKernelMultipliers(1.0f, 2.0f, 4.0f);
    assert(isotropic.across == 1.0f && isotropic.along == 1.0f);
    const auto directional = m::linearKernelMultipliers(2.0f, 2.0f, 4.0f);
    assert(directional.across == .5f && directional.along == 4.0f);
    assert(m::interpolationBase(-.2f) == -1);
    assert(m::interpolationBase(.2f) == 0);
    assert(m::clippedIcaUpdate(-7.0f, 4.0f) == -4.0f);
    assert(m::clippedIcaUpdate(7.0f, 4.0f) == 4.0f);
    assert(m::sqrtGuide(-1.0f) == 0.0f);
    assert(std::abs(m::sqrtGuide(.25f) - .5f) < 1.0e-7f);
    const float darkNoise = m::sqrtDomainNoiseVariance(0.0f, 1e-4f);
    const float brightNoise = m::sqrtDomainNoiseVariance(1.0f, 1e-4f);
    assert(std::isfinite(darkNoise));
    assert(darkNoise >= brightNoise);
    const auto aggregate = m::aggregateRobustnessNoise(0.09f, 0.18f, 0.27f);
    assert(std::abs(aggregate.sigmaSquared - 0.48f) < 1e-7f);
    assert(std::abs(aggregate.distanceSquared - 0.12f) < 1e-7f);
    return 0;
}
