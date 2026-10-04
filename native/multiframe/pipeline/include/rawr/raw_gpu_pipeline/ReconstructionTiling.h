#pragma once
#include <rawr/raw_gpu_pipeline/ResourceGeometry.h>

#include <cstdint>
#include <vector>

namespace rawr::raw_gpu_pipeline {
struct ReconstructionStripe {
    std::uint32_t yBase = 0;
    std::uint32_t height = 0;
};
struct ReconstructionTiling {
    Extent2D fullOutput{};
    std::uint32_t requestedStripeHeight = 0;
    std::vector<ReconstructionStripe> stripes;
};
ReconstructionTiling makeReconstructionTiling(Extent2D fullOutput, std::uint32_t stripeHeight);
std::uint64_t rgba32AccumulatorBytes(Extent2D extent) noexcept;
}  // namespace rawr::raw_gpu_pipeline
