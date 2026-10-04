#include "tonemap/lut/LutChain.h"

#include <stdexcept>
namespace tonemap::lut {
color::Vec3 LutChain::evaluateStages(color::Vec3 value) const {
    if (stages.empty()) throw std::runtime_error("LUT chain must contain at least one LUT");
    auto out = value;
    for (const auto& stage : stages) out = stage.sampleTetrahedral(out);
    return out;
}
}  // namespace tonemap::lut
