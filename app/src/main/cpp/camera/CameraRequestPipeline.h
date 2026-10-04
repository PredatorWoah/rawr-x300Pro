#pragma once
#include <functional>
#include <memory>
#include <vector>

#include "camera/CameraCallbacks.h"
#include "camera/CameraRequestControls.h"
#include "camera/CameraRequestProvenance.h"
namespace rawrcam::camera {
class CameraRequestPipeline final {
   public:
    using Diagnostic = std::function<void(const std::string&)>;
    explicit CameraRequestPipeline(Diagnostic diagnostic) : diagnostic_(std::move(diagnostic)) {}
    bool submit(ACameraCaptureSession*, ACaptureRequest*, CameraSessionCallbackContext*, const CameraControlState&,
                const CameraMeteringRequest&);
    camera_status_t applySessionCadence(ACaptureRequest*, const CameraControlState&);
    uint64_t latestSubmittedSerial() const noexcept { return latestSubmittedRequestSerial_; }
    void retire() noexcept { provenance_.clear(); }

   private:
    const CameraRequestProvenance* attach(ACaptureRequest*, const CameraControlState&);
    void diag(const std::string& line) const {
        if (diagnostic_) diagnostic_(line);
    }
    Diagnostic diagnostic_;
    uint64_t nextRequestSerial_ = 1, latestSubmittedRequestSerial_ = 0;
    std::vector<std::unique_ptr<CameraRequestProvenance>> provenance_;
};
}  // namespace rawrcam::camera
