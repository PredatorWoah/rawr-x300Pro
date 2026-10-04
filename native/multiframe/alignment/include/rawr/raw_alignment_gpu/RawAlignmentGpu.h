#pragma once
#include <cstdint>
namespace rawr::raw_alignment_gpu {
struct Config {
    std::uint32_t lkIterations = 3;
    float hessianEpsilon = 1e-10f;
    bool cfaSuppressionLocal5 = true;
    std::uint32_t workgroupX = 16, workgroupY = 16;
};
inline bool valid(const Config& c) { return c.lkIterations > 0 && c.workgroupX > 0 && c.workgroupY > 0; }
}  // namespace rawr::raw_alignment_gpu
