#include "camera/FullResProbe.h"

#include <android/native_window.h>
#include <camera/NdkCameraDevice.h>
#include <camera/NdkCameraManager.h>
#include <camera/NdkCameraMetadata.h>
#include <camera/NdkCameraMetadataTags.h>
#include <camera/NdkCaptureRequest.h>
#include <media/NdkImage.h>
#include <media/NdkImageReader.h>

#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <mutex>
#include <sstream>
#include <thread>
#include <vector>

namespace rawrcam::camera {
namespace {

// ACAMERA_SENSOR_PIXEL_MODE, spelled out so this builds against any NDK.
constexpr uint32_t kSensorPixelMode = 0xe0020;
constexpr auto kStepTimeout = std::chrono::seconds(8);

struct Wait {
    std::mutex mutex;
    std::condition_variable cv;
    bool imageReady = false;
    bool completed = false;
    bool failed = false;
    std::string resultInfo;
};

void onImageAvailable(void* context, AImageReader*) {
    auto* w = static_cast<Wait*>(context);
    std::lock_guard<std::mutex> lock(w->mutex);
    w->imageReady = true;
    w->cv.notify_all();
}

std::string describeEntry(const ACameraMetadata* result, uint32_t tag) {
    ACameraMetadata_const_entry e{};
    if (ACameraMetadata_getConstEntry(result, tag, &e) != ACAMERA_OK) return "";
    std::ostringstream s;
    for (uint32_t i = 0; i < e.count && i < 8; ++i) {
        if (i) s << ',';
        switch (e.type) {
            case ACAMERA_TYPE_INT32: s << e.data.i32[i]; break;
            case ACAMERA_TYPE_BYTE: s << int(e.data.u8[i]); break;
            case ACAMERA_TYPE_FLOAT: s << e.data.f[i]; break;
            default: s << '?'; break;
        }
    }
    return s.str();
}

void onCompleted(void* context, ACameraCaptureSession*, ACaptureRequest*, const ACameraMetadata* result) {
    auto* w = static_cast<Wait*>(context);
    std::ostringstream info;
    info << "pixelMode=" << describeEntry(result, kSensorPixelMode)
         << " scalerCrop=" << describeEntry(result, ACAMERA_SCALER_CROP_REGION);
    std::lock_guard<std::mutex> lock(w->mutex);
    w->resultInfo = info.str();
    w->completed = true;
    w->cv.notify_all();
}

void onFailed(void* context, ACameraCaptureSession*, ACaptureRequest*, ACameraCaptureFailure* failure) {
    auto* w = static_cast<Wait*>(context);
    std::lock_guard<std::mutex> lock(w->mutex);
    w->resultInfo = "captureFailed reason=" + std::to_string(failure ? failure->reason : -1);
    w->failed = true;
    w->cv.notify_all();
}

void noopDisconnected(void*, ACameraDevice*) {}
void noopError(void*, ACameraDevice*, int) {}
void noopSession(void*, ACameraCaptureSession*) {}

// Mean 16-bit value on a 16x12 grid (all four Bayer phases), so the frame can be compared with a reference one.
std::string gridOf(const uint8_t* data, int width, int height, int rowStrideBytes, uint16_t* minOut, uint16_t* maxOut) {
    constexpr int kCols = 16, kRows = 12;
    uint16_t lo = 0xffff, hi = 0;
    std::ostringstream s;
    s << "grid=" << kCols << 'x' << kRows << ":[";
    for (int gy = 0; gy < kRows; ++gy) {
        for (int gx = 0; gx < kCols; ++gx) {
            const int x0 = gx * width / kCols, x1 = (gx + 1) * width / kCols;
            const int y0 = gy * height / kRows, y1 = (gy + 1) * height / kRows;
            const int step = std::max(2, ((x1 - x0) / 24) & ~1);
            uint64_t sum = 0, n = 0;
            for (int y = y0 & ~1; y + 1 < y1; y += step) {
                const auto* row0 = reinterpret_cast<const uint16_t*>(data + static_cast<size_t>(y) * rowStrideBytes);
                const auto* row1 = reinterpret_cast<const uint16_t*>(data + static_cast<size_t>(y + 1) * rowStrideBytes);
                for (int x = x0 & ~1; x + 1 < x1; x += step) {
                    const uint16_t v[4] = {row0[x], row0[x + 1], row1[x], row1[x + 1]};
                    for (const uint16_t p : v) {
                        sum += p;
                        lo = std::min(lo, p);
                        hi = std::max(hi, p);
                    }
                    n += 4;
                }
            }
            s << (gy || gx ? "," : "") << (n ? sum / n : 0);
        }
    }
    s << ']';
    *minOut = lo;
    *maxOut = hi;
    return s.str();
}

struct Stream {
    int width = 0;
    int height = 0;
    AImageReader* reader = nullptr;
    ANativeWindow* window = nullptr;
    Wait wait;
};

bool makeStream(Stream& s, int width, int height, int maxImages) {
    s.width = width;
    s.height = height;
    if (AImageReader_new(width, height, AIMAGE_FORMAT_RAW16, maxImages, &s.reader) != AMEDIA_OK || !s.reader)
        return false;
    AImageReader_ImageListener listener{&s.wait, onImageAvailable};
    AImageReader_setImageListener(s.reader, &listener);
    return AImageReader_getWindow(s.reader, &s.window) == AMEDIA_OK && s.window;
}

void freeStream(Stream& s) {
    if (s.reader) AImageReader_delete(s.reader);
    s.reader = nullptr;
    s.window = nullptr;
}

// One trial: a session with `streams` (the first is captured from), optionally in maximum-resolution pixel mode.
void trial(ACameraDevice* device, const std::string& label, std::vector<Stream*> streams, bool maxResPixelMode,
           const std::function<void(const std::string&)>& diag) {
    const std::string head = "FULLRES_PROBE trial=" + label;
    ACaptureSessionOutputContainer* container = nullptr;
    std::vector<ACaptureSessionOutput*> outputs;
    ACameraCaptureSession* session = nullptr;
    ACaptureRequest* request = nullptr;
    ACameraOutputTarget* target = nullptr;

    auto cleanup = [&] {
        if (session) {
            ACameraCaptureSession_stopRepeating(session);
            ACameraCaptureSession_close(session);
        }
        if (request) ACaptureRequest_free(request);
        if (target) ACameraOutputTarget_free(target);
        for (auto* o : outputs) ACaptureSessionOutput_free(o);
        if (container) ACaptureSessionOutputContainer_free(container);
    };

    if (ACaptureSessionOutputContainer_create(&container) != ACAMERA_OK) {
        diag(head + " result=container_failed");
        return;
    }
    for (Stream* s : streams) {
        ACaptureSessionOutput* o = nullptr;
        if (ACaptureSessionOutput_create(s->window, &o) != ACAMERA_OK || !o ||
            ACaptureSessionOutputContainer_add(container, o) != ACAMERA_OK) {
            diag(head + " result=output_failed");
            if (o) ACaptureSessionOutput_free(o);
            cleanup();
            return;
        }
        outputs.push_back(o);
    }

    ACameraCaptureSession_stateCallbacks stateCallbacks{};
    stateCallbacks.onClosed = noopSession;
    stateCallbacks.onReady = noopSession;
    stateCallbacks.onActive = noopSession;
    const camera_status_t cs = ACameraDevice_createCaptureSession(device, container, &stateCallbacks, &session);
    if (cs != ACAMERA_OK || !session) {
        diag(head + " result=session_rejected status=" + std::to_string(cs));
        session = nullptr;
        cleanup();
        return;
    }

    Stream& primary = *streams.front();
    if (ACameraDevice_createCaptureRequest(device, TEMPLATE_STILL_CAPTURE, &request) != ACAMERA_OK || !request ||
        ACameraOutputTarget_create(primary.window, &target) != ACAMERA_OK ||
        ACaptureRequest_addTarget(request, target) != ACAMERA_OK) {
        diag(head + " result=request_failed");
        cleanup();
        return;
    }
    if (maxResPixelMode) {
        const uint8_t mode = 1;  // ANDROID_SENSOR_PIXEL_MODE_MAXIMUM_RESOLUTION
        const camera_status_t ps = ACaptureRequest_setEntry_u8(request, kSensorPixelMode, 1, &mode);
        if (ps != ACAMERA_OK) diag(head + " note=pixelMode_set_status=" + std::to_string(ps));
    }

    ACameraCaptureSession_captureCallbacks captureCallbacks{};
    captureCallbacks.context = &primary.wait;
    captureCallbacks.onCaptureCompleted = onCompleted;
    captureCallbacks.onCaptureFailed = onFailed;
    {
        std::lock_guard<std::mutex> lock(primary.wait.mutex);
        primary.wait.imageReady = false;
        primary.wait.completed = false;
        primary.wait.failed = false;
        primary.wait.resultInfo.clear();
    }
    int sequence = 0;
    const camera_status_t capStatus = ACameraCaptureSession_capture(session, &captureCallbacks, 1, &request, &sequence);
    if (capStatus != ACAMERA_OK) {
        diag(head + " result=capture_rejected status=" + std::to_string(capStatus));
        cleanup();
        return;
    }

    bool gotImage = false;
    {
        std::unique_lock<std::mutex> lock(primary.wait.mutex);
        primary.wait.cv.wait_for(lock, kStepTimeout, [&] {
            return (primary.wait.imageReady && primary.wait.completed) || primary.wait.failed;
        });
        gotImage = primary.wait.imageReady;
    }
    std::string info;
    {
        std::lock_guard<std::mutex> lock(primary.wait.mutex);
        info = primary.wait.resultInfo;
    }
    if (!gotImage) {
        diag(head + " result=no_image " + info);
        cleanup();
        return;
    }

    AImage* image = nullptr;
    if (AImageReader_acquireNextImage(primary.reader, &image) != AMEDIA_OK || !image) {
        diag(head + " result=acquire_failed " + info);
        cleanup();
        return;
    }
    int32_t w = 0, h = 0, fmt = 0, stride = 0, len = 0;
    uint8_t* data = nullptr;
    AImage_getWidth(image, &w);
    AImage_getHeight(image, &h);
    AImage_getFormat(image, &fmt);
    AImage_getPlaneRowStride(image, 0, &stride);
    AImage_getPlaneData(image, 0, &data, &len);
    std::ostringstream out;
    out << head << " result=image requested=" << primary.width << 'x' << primary.height << " delivered=" << w << 'x' << h
        << " format=0x" << std::hex << fmt << std::dec << " bytes=" << len << " rowStride=" << stride << ' ' << info;
    if (data && w > 0 && h > 0 && stride >= w * 2 && static_cast<int64_t>(len) >= static_cast<int64_t>(stride) * (h - 1) + w * 2) {
        uint16_t lo = 0, hi = 0;
        const std::string grid = gridOf(data, w, h, stride, &lo, &hi);
        out << " min=" << lo << " max=" << hi << ' ' << grid;
    }
    diag(out.str());
    AImage_delete(image);
    cleanup();
}

}  // namespace

void runFullResProbe(const std::string& cameraId, int baseWidth, int baseHeight,
                     const std::function<void(const std::string&)>& diag) {
    diag("FULLRES_PROBE_BEGIN cameraId=" + cameraId + " base=" + std::to_string(baseWidth) + "x" +
         std::to_string(baseHeight));
    ACameraManager* manager = ACameraManager_create();
    if (!manager) {
        diag("FULLRES_PROBE_END reason=no_manager");
        return;
    }
    ACameraDevice* device = nullptr;
    ACameraDevice_StateCallbacks deviceCallbacks{};
    deviceCallbacks.onDisconnected = noopDisconnected;
    deviceCallbacks.onError = noopError;
    // The live session was just released; the HAL can take a moment to report the camera free again.
    camera_status_t opened = ACAMERA_ERROR_CAMERA_IN_USE;
    for (int attempt = 0; attempt < 8 && opened != ACAMERA_OK; ++attempt) {
        opened = ACameraManager_openCamera(manager, cameraId.c_str(), &deviceCallbacks, &device);
        if (opened != ACAMERA_OK) std::this_thread::sleep_for(std::chrono::milliseconds(250));
    }
    if (opened != ACAMERA_OK || !device) {
        diag("FULLRES_PROBE_END reason=open_failed status=" + std::to_string(opened));
        ACameraManager_delete(manager);
        return;
    }

    Stream base;
    if (!makeStream(base, baseWidth, baseHeight, 2)) {
        diag("FULLRES_PROBE_END reason=base_reader_failed");
        ACameraDevice_close(device);
        ACameraManager_delete(manager);
        return;
    }
    trial(device, "base", {&base}, false, diag);

    for (const int factor : {2, 4}) {
        const int w = baseWidth * factor, h = baseHeight * factor;
        const std::string name = std::to_string(w) + "x" + std::to_string(h);
        // A 16-bit frame this size is 100 MB (50 MP) to 400 MB (200 MP): one buffer for the biggest.
        Stream big;
        if (!makeStream(big, w, h, factor == 2 ? 2 : 1)) {
            diag("FULLRES_PROBE trial=" + name + " result=reader_failed");
            freeStream(big);
            continue;
        }
        trial(device, name + "_alone", {&big}, false, diag);
        trial(device, name + "_alone_maxres", {&big}, true, diag);
        trial(device, name + "_with_base", {&big, &base}, false, diag);
        freeStream(big);
    }

    freeStream(base);
    ACameraDevice_close(device);
    ACameraManager_delete(manager);
    diag("FULLRES_PROBE_END reason=done");
}

}  // namespace rawrcam::camera
