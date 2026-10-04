#pragma once
#include <memory>
#include <utility>
namespace rawrcam::capture::persistence {
class CaptureReservation final {
   public:
    explicit CaptureReservation(std::shared_ptr<void> token = {}) : token_(std::move(token)) {}
    CaptureReservation(CaptureReservation&&) noexcept = default;
    CaptureReservation& operator=(CaptureReservation&&) noexcept = default;
    CaptureReservation(const CaptureReservation&) = delete;
    CaptureReservation& operator=(const CaptureReservation&) = delete;
    void reset() noexcept { token_.reset(); }

   private:
    std::shared_ptr<void> token_;
};
}  // namespace rawrcam::capture::persistence
