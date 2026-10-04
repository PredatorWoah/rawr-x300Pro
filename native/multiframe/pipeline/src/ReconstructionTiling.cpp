#include <rawr/raw_gpu_pipeline/ReconstructionTiling.h>

#include <algorithm>
#include <stdexcept>
namespace rawr::raw_gpu_pipeline {
ReconstructionTiling makeReconstructionTiling(Extent2D e, std::uint32_t h) {
    if (e.width == 0 || e.height == 0) throw std::invalid_argument("reconstruction tiling: empty output");
    if (h == 0) throw std::invalid_argument("reconstruction tiling: stripe height must be nonzero");
    ReconstructionTiling r{};
    r.fullOutput = e;
    r.requestedStripeHeight = h;
    for (std::uint32_t y = 0; y < e.height;) {
        const std::uint32_t n = std::min(h, e.height - y);
        r.stripes.push_back({y, n});
        y += n;
    }
    return r;
}
std::uint64_t rgba32AccumulatorBytes(Extent2D e) noexcept {
    return static_cast<std::uint64_t>(e.width) * e.height * 16u * 2u;
}
}  // namespace rawr::raw_gpu_pipeline
