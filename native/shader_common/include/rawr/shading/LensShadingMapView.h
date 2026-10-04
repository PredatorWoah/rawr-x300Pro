#pragma once

#include <cstddef>
#include <cstdint>

namespace rawr::shading {

// Borrowed view of Camera2's interleaved [R, G_even, G_odd, B] lens-shading
// gain grid, the host-side layout read by shaders/lens_shading.glsl.
struct LensShadingMapView final {
    uint32_t width = 0;
    uint32_t height = 0;
    const float* gains = nullptr;
    size_t count = 0;
    [[nodiscard]] bool valid() const noexcept {
        return width >= 2u && height >= 2u && gains && count == static_cast<size_t>(width) * height * 4u;
    }
};

}  // namespace rawr::shading
