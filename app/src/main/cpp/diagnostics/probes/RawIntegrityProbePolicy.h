#pragma once
#include <cstdint>
namespace rawrcam::diagnostics {
struct RawIntegrityProbePolicy final {
    static constexpr bool shouldCapture(uint32_t mode) noexcept {
        return mode == 0u || mode == 6u || mode == 9u || (mode >= 11u && mode <= 15u);
    }
};
}  // namespace rawrcam::diagnostics
