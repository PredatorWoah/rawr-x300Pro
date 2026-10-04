#pragma once
#include <array>
#include <cstdint>
#include <string>
#include <vector>

#include "tonemap/color/ColorSpace.h"

namespace tonemap::lut {
struct Lut3D {
    uint32_t size = 0;
    std::array<float, 3> domainMin{0, 0, 0};
    std::array<float, 3> domainMax{1, 1, 1};
    std::vector<color::Vec3> values;  // .cube order: R fastest, then G, then B
    color::Vec3 sampleTetrahedral(color::Vec3 input) const;
};
Lut3D parseCubeFile(const std::string& path, uint32_t maxSize = 65);
}  // namespace tonemap::lut
