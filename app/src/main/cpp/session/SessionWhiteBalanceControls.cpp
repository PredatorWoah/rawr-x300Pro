// White-balance controls (HAL AWB presets + manual temp/tint gains).
// Split from SessionEngine.cpp; mirrors SessionExposureControls ownership:
// the native camera owner accepts/rejects, Kotlin snapshots confirm.
#include "camera/NativeCameraController.h"
#include "diagnostics/logging/RuntimeTraceRecorder.h"
#include "session/SessionEngine.h"

namespace rawrcam::session {

void SessionEngine::setWhiteBalanceMode(int mode, int64_t requestId) {
    if (!cameraControls_.controller()) return;
    rawrcam::camera::WhiteBalanceControlMode wb = rawrcam::camera::WhiteBalanceControlMode::Auto;
    if (mode >= 2 && mode <= 8) {
        wb = static_cast<rawrcam::camera::WhiteBalanceControlMode>(mode);
    } else if (mode == 9) {
        wb = rawrcam::camera::WhiteBalanceControlMode::ManualTempTint;
    }
    // Capability-gated like exposure S/I: do not publish UI state the native
    // camera owner rejected. The snapshot round-trip confirms acceptance.
    const bool accepted = cameraControls_.controller()->setWhiteBalanceMode(wb, requestId);
    if (!accepted) return;
    {
        const auto tracedState = cameraControls_.controller()->controlState();
        rawrcam::diagnostics::RuntimeTraceRecorder::instance().record(
            rawrcam::diagnostics::RuntimeTraceStage::ExposureMode, 0, 0, -1, tracedState.requestedExposureTimeNs,
            tracedState.requestedSensitivity, static_cast<std::uint32_t>(mode),
            static_cast<std::int64_t>(tracedState.whiteBalanceMode),
            std::atoi(tracedState.capabilities.cameraId.c_str()));
    }
}

void SessionEngine::setWhiteBalanceTempTint(int32_t temperatureK, int32_t tint, int32_t editedAxes, int64_t requestId) {
    if (!cameraControls_.controller()) return;
    cameraControls_.controller()->setWhiteBalanceTempTint(temperatureK, tint, editedAxes, requestId);
}

void SessionEngine::setWhiteBalanceLocked(int32_t temperatureK, int32_t tint, int64_t requestId) {
    if (!cameraControls_.controller()) return;
    cameraControls_.controller()->setWhiteBalanceLocked(temperatureK, tint, requestId);
}

}  // namespace rawrcam::session
