#include "camera/CameraCallbacks.h"
namespace rawrcam::camera {
void CameraCallbacks::revoke() noexcept {
    std::unique_lock<std::mutex> lock(lifetime_->mutex);
    lifetime_->owner = nullptr;
    lifetime_->idle.wait(lock, [this] { return lifetime_->inFlight == 0; });
}
CameraDeviceCallbackContext* CameraCallbacks::deviceContext(uint64_t generation) const {
    return new CameraDeviceCallbackContext{lifetime_, generation};
}
CameraSessionCallbackContext* CameraCallbacks::sessionContext(uint64_t generation) const {
    return new CameraSessionCallbackContext{lifetime_, generation};
}
ACameraDevice_StateCallbacks CameraCallbacks::deviceState(CameraDeviceCallbackContext* context) {
    ACameraDevice_StateCallbacks state{};
    state.context = context;
    state.onDisconnected = onDisconnected;
    state.onError = onError;
    return state;
}
ACameraCaptureSession_stateCallbacks CameraCallbacks::sessionState(CameraSessionCallbackContext* context) {
    ACameraCaptureSession_stateCallbacks state{};
    state.context = context;
    state.onClosed = onClosed;
    state.onReady = onReady;
    state.onActive = onActive;
    return state;
}
ACameraCaptureSession_captureCallbacks CameraCallbacks::captures(CameraSessionCallbackContext* context) {
    ACameraCaptureSession_captureCallbacks captures{};
    captures.context = context;
    captures.onCaptureCompleted = onCompleted;
    return captures;
}
ACameraCaptureSession_logicalCamera_captureCallbacks CameraCallbacks::logicalCaptures(
    CameraSessionCallbackContext* context) {
    ACameraCaptureSession_logicalCamera_captureCallbacks captures{};
    captures.context = context;
    captures.onLogicalCameraCaptureCompleted = onLogicalCompleted;
    return captures;
}
void CameraCallbacks::onDisconnected(void* context, ACameraDevice*) {
    auto* callback = static_cast<CameraDeviceCallbackContext*>(context);
    CameraCallbackGuard<CameraEventSink> guard(callback->lifetime);
    if (guard.owner) guard.owner->cameraDeviceEvent(callback->generation, "CAMERA_NDK_DISCONNECTED");
}
void CameraCallbacks::onError(void* context, ACameraDevice*, int error) {
    auto* callback = static_cast<CameraDeviceCallbackContext*>(context);
    CameraCallbackGuard<CameraEventSink> guard(callback->lifetime);
    if (guard.owner)
        guard.owner->cameraDeviceEvent(callback->generation, "CAMERA_NDK_ERROR error=" + std::to_string(error));
}
void CameraCallbacks::onClosed(void* context, ACameraCaptureSession* session) {
    auto* callback = static_cast<CameraSessionCallbackContext*>(context);
    CameraCallbackGuard<CameraEventSink> guard(callback->lifetime);
    if (guard.owner) guard.owner->cameraSessionClosed(callback->generation, session);
    delete callback;
}
void CameraCallbacks::onCompleted(void* context, ACameraCaptureSession*, ACaptureRequest* request,
                                  const ACameraMetadata* result) {
    auto* callback = static_cast<CameraSessionCallbackContext*>(context);
    CameraCallbackGuard<CameraEventSink> guard(callback->lifetime);
    if (guard.owner) guard.owner->cameraCaptureCompleted(callback->generation, request, result);
}
void CameraCallbacks::onLogicalCompleted(void* context, ACameraCaptureSession*, ACaptureRequest* request,
                                         const ACameraMetadata* result, size_t physicalResultCount,
                                         const char** physicalCameraIds, const ACameraMetadata** physicalResults) {
    auto* callback = static_cast<CameraSessionCallbackContext*>(context);
    const ACameraMetadata* chosen = result;
    for (size_t i = 0; i < physicalResultCount; ++i) {
        if (physicalCameraIds && physicalResults && physicalCameraIds[i] &&
            callback->physicalCameraId == physicalCameraIds[i]) {
            chosen = physicalResults[i];
            break;
        }
    }
    CameraCallbackGuard<CameraEventSink> guard(callback->lifetime);
    if (guard.owner) guard.owner->cameraCaptureCompleted(callback->generation, request, chosen);
}
}  // namespace rawrcam::camera
