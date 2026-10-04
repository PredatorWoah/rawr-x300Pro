#pragma once
#include <utility>

#include "imaging/RawFrameIngress.h"
#include "support/UniqueFd.h"
namespace rawrcam::imaging {
// AHardwareBuffer is borrowed from the owned AImage. Release transfers both with its fence.
class RawFrameLease final {
   public:
    explicit RawFrameLease(AcquiredRawFrame frame) noexcept : frame_(frame), fence_(frame.acquireFenceFd) {}
    ~RawFrameLease() {
        if (frame_.image) AImage_delete(frame_.image);
    }
    RawFrameLease(const RawFrameLease&) = delete;
    RawFrameLease& operator=(const RawFrameLease&) = delete;
    RawFrameLease(RawFrameLease&& other) noexcept : RawFrameLease(other.release()) {}
    RawFrameLease& operator=(RawFrameLease&& other) noexcept {
        if (this != &other) {
            fence_.reset();
            if (frame_.image) AImage_delete(frame_.image);
            frame_ = other.release();
            fence_.reset(frame_.acquireFenceFd);
        }
        return *this;
    }
    const AcquiredRawFrame& get() const noexcept { return frame_; }
    AcquiredRawFrame release() noexcept {
        auto frame = std::exchange(frame_, {});
        frame.acquireFenceFd = fence_.release();
        return frame;
    }

   private:
    AcquiredRawFrame frame_;
    support::UniqueFd fence_;
};
}  // namespace rawrcam::imaging
