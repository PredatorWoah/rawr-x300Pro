#include "camera/CameraRequestPipeline.h"

#include <cstdlib>
#include <utility>

#include "diagnostics/logging/RuntimeTraceRecorder.h"

namespace rawrcam::camera {
namespace {
bool ok(camera_status_t status) { return status == ACAMERA_OK; }
std::string statusText(camera_status_t status) { return std::to_string(static_cast<int>(status)); }
}  // namespace
const CameraRequestProvenance* CameraRequestPipeline::attach(ACaptureRequest* request,
                                                             const CameraControlState& control) {
    if (!request) return nullptr;
    auto provenance =
        std::make_unique<CameraRequestProvenance>(CameraRequestProvenance::from(control, nextRequestSerial_++));
    CameraRequestProvenance* const ptr = provenance.get();
    if (ACaptureRequest_setUserContext(request, ptr) == ACAMERA_OK) {
        provenance_.push_back(std::move(provenance));
        return ptr;
    }
    diag("CAMERA_REQUEST_PROVENANCE_FAIL");
    return nullptr;
}
bool CameraRequestPipeline::submit(ACameraCaptureSession* session, ACaptureRequest* request,
                                   CameraSessionCallbackContext* context, const CameraControlState& control,
                                   const CameraMeteringRequest& metering) {
    if (!session || !request || !context) return false;
    const auto applyResult = applyCameraControlState(request, control, metering);
    const bool priorityMode = control.exposureMode == ExposureControlMode::ShutterPriority ||
                              control.exposureMode == ExposureControlMode::IsoPriority;
    if (priorityMode && (!applyResult.aePriorityRequested || applyResult.aePriorityStatus != ACAMERA_OK)) {
        diag("CAMERA_AE_PRIORITY_REQUEST_FAIL mode=" + std::to_string(static_cast<int>(control.exposureMode)) +
             " status=" + statusText(applyResult.aePriorityStatus));
        diag("CAMERA_AE_PRIORITY_SUBMIT_BLOCKED mode=" + std::to_string(static_cast<int>(control.exposureMode)) +
             " reason=priority_tag status=" + statusText(applyResult.aePriorityStatus));
        return false;
    }
    if ((priorityMode || control.recordingFps > 0) && applyResult.aeTargetFpsRequested &&
        applyResult.aeTargetFpsStatus != ACAMERA_OK) {
        diag("CAMERA_AE_PRIORITY_SUBMIT_BLOCKED mode=" + std::to_string(static_cast<int>(control.exposureMode)) +
             " reason=target_fps status=" + statusText(applyResult.aeTargetFpsStatus));
        return false;
    }
    if (applyResult.wbManualRequested && applyResult.wbManualFallbackToAuto) {
        diag(std::string("CAMERA_WB_MANUAL_FALLBACK_TO_AUTO reason=") +
             (!control.capabilities.manualGainsSupported ? "unsupported" : "no_transform_seed") +
             " status=" + statusText(applyResult.wbStatus));
    }
    const CameraRequestProvenance* const provenance = attach(request, control);
    if (!provenance) return false;
    int sequenceId = 0;
    camera_status_t s = ACAMERA_OK;
    if (context->physicalCameraId.empty()) {
        auto captures = CameraCallbacks::captures(context);
        s = ACameraCaptureSession_setRepeatingRequest(session, &captures, 1, &request, &sequenceId);
    } else {
        auto captures = CameraCallbacks::logicalCaptures(context);
        s = ACameraCaptureSession_logicalCamera_setRepeatingRequest(session, &captures, 1, &request, &sequenceId);
    }
    if (!ok(s)) {
        diag("CAMERA_CONTROL_REPEATING_FAILURE status=" + statusText(s));
        return false;
    }
    latestSubmittedRequestSerial_ = provenance->requestSerial;
    rawrcam::diagnostics::RuntimeTraceRecorder::instance().record(
        rawrcam::diagnostics::RuntimeTraceStage::CameraRequestSubmit, 0, provenance->requestSerial, -1,
        provenance->requestedExposureTimeNs, provenance->requestedSensitivity, 0u, sequenceId,
        std::atoi(control.capabilities.cameraId.c_str()));
    rawrcam::diagnostics::RuntimeTraceRecorder::instance().record(
        rawrcam::diagnostics::RuntimeTraceStage::ExposureMode, 0, provenance->requestSerial, -1,
        provenance->requestedExposureTimeNs, provenance->requestedSensitivity,
        static_cast<std::uint32_t>(provenance->exposureMode), static_cast<std::int64_t>(provenance->exposureMode),
        std::atoi(control.capabilities.cameraId.c_str()));
    if (priorityMode) {
        diag("CAMERA_AE_PRIORITY_SUBMIT serial=" + std::to_string(provenance->requestSerial) +
             " mode=" + std::to_string(static_cast<int>(provenance->exposureMode)) +
             " priority=" + std::to_string(provenance->expectedAePriority) +
             " shutterNs=" + std::to_string(provenance->requestedExposureTimeNs) + " sensitivity=" +
             std::to_string(provenance->requestedSensitivity) + " fps=" + std::to_string(provenance->targetFpsMin) +
             ".." + std::to_string(provenance->targetFpsMax) + " sequenceId=" + std::to_string(sequenceId));
    }
    return true;
}
camera_status_t CameraRequestPipeline::applySessionCadence(ACaptureRequest* sessionRequest,
                                                           const CameraControlState& control) {
    const auto result = applyCameraSessionCadence(sessionRequest, control);
    if (!result.fpsRange) return result.status;
    const bool priority = control.exposureMode == ExposureControlMode::ShutterPriority ||
                          control.exposureMode == ExposureControlMode::IsoPriority;
    if (control.recordingFps == 0) {
        diag(std::string(priority ? "CAMERA_AE_PRIORITY_SESSION_FPS" : "CAMERA_AE_AUTO_SESSION_FPS") +
             " requested=" + std::to_string((*result.fpsRange)[0]) + ".." + std::to_string((*result.fpsRange)[1]) +
             " status=" + statusText(result.status));
    }
    return result.status;
}
}  // namespace rawrcam::camera
