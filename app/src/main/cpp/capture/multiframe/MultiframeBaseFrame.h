#pragma once

#include <cstdint>

namespace rawrcam::capture::multiframe {

// Global reference-frame policy. Middle preserves the validated chronological
// middle; Sharpest selects the burst frame with the highest GPU-measured
// green-channel Laplacian variance.
enum class MultiframeBaseFrameMode : std::uint32_t {
    Middle = 0,
    Sharpest = 1,
};

}  // namespace rawrcam::capture::multiframe
