#pragma once
#include <atomic>

#include "capture/persistence/CaptureJob.h"
#include "capture/persistence/CaptureReservation.h"
#include "capture/single/SingleFrameCaptureTypes.h"
namespace rawrcam::capture {
class SingleFrameCaptureJournal final {
   public:
    explicit SingleFrameCaptureJournal(CaptureDiagnostic diagnostic) : diagnostic_(std::move(diagnostic)) {}
    void setAdmission(persistence::CaptureReservation reservation, std::string path);
    void setResident(bool value) noexcept { keepRawResident_.store(value); }
    void freeze(const encoding::dng::DngCaptureContext& dng, const JpegCaptureRequest* jpeg);
    void commit(SingleFrameSnapshot& snapshot);
    void reloadRaw(SingleFrameSnapshot& snapshot) const;
    void markFilmFallback() const { persistence::markFilmFallback(durablePath_); }
    void setRecoveryPath(std::string path) { durablePath_ = std::move(path); }
    std::string resolvedRecipe() const { return frozen_.dng.resolvedRecipe; }

   private:
    void emit(const std::string& line) const {
        if (diagnostic_) diagnostic_(line);
    }
    CaptureDiagnostic diagnostic_;
    std::string durablePath_;
    persistence::CaptureReservation reservation_;
    std::atomic<bool> keepRawResident_{false};
    persistence::CaptureJob frozen_;
};
}  // namespace rawrcam::capture
