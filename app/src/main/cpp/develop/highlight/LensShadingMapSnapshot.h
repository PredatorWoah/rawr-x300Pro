#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

#include "metadata/FrameMetadataSnapshot.h"
#include "rawr/shading/LensShadingMapView.h"

namespace rawrcam::develop::highlight {

using LensShadingMapView = rawr::shading::LensShadingMapView;

// Immutable worker-owned snapshot of Camera2's interleaved
// [R, G_even, G_odd, B] lens-shading gain grid.
struct LensShadingMapSnapshot final {
    uint32_t width = 0;
    uint32_t height = 0;
    std::vector<float> gains;

    [[nodiscard]] bool valid() const noexcept {
        return width >= 2u && height >= 2u && gains.size() == static_cast<size_t>(width) * height * 4u;
    }
    [[nodiscard]] LensShadingMapView view() const noexcept { return {width, height, gains.data(), gains.size()}; }

    static LensShadingMapSnapshot fromMetadata(const rawrcam::metadata::FrameMetadataSnapshot& metadata, bool enabled) {
        LensShadingMapSnapshot out{};
        if (!enabled) return out;
        out.width = metadata.lensShadingMapWidth;
        out.height = metadata.lensShadingMapHeight;
        out.gains = metadata.lensShadingMap;
        if (!out.valid()) return {};
        return out;
    }
};

}  // namespace rawrcam::develop::highlight
