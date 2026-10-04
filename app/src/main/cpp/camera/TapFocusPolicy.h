#pragma once

#include <cstdint>
#include <optional>

namespace rawrcam::camera {

// Native AF region lifetime. Locked AF has no deadline; a new tap replaces the
// continuous-AF dwell. Resource retirement and mode changes cancel the timer.
class TapFocusPolicy final {
   public:
    void acceptedTap(bool continuous, int64_t nowMs) {
        deadline_ = continuous ? std::optional<int64_t>{nowMs + 4000} : std::nullopt;
    }
    void clear() { deadline_.reset(); }
    bool shouldClear(int64_t nowMs) {
        if (!deadline_ || nowMs < *deadline_) return false;
        deadline_.reset();
        return true;
    }

   private:
    std::optional<int64_t> deadline_;
};

}  // namespace rawrcam::camera
