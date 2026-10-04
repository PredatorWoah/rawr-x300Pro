#include "capture/single/SingleShotSlot.h"
#include "imaging/Raw16CpuSnapshot.h"
#include "color/FrameColorTransform.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <iostream>
#include <memory>
#include <vector>

struct AImage {
    int32_t width = 0;
    int32_t height = 0;
    int32_t rowStrideBytes = 0;
    int32_t pixelStrideBytes = 2;
};

struct AHardwareBuffer {
    AHardwareBuffer_Desc desc{};
    std::vector<uint8_t> storage;
};

extern "C" media_status_t AImage_getFormat(const AImage*, int32_t* out) { *out = AIMAGE_FORMAT_RAW16; return AMEDIA_OK; }
extern "C" media_status_t AImage_getWidth(const AImage* image, int32_t* out) { *out = image->width; return AMEDIA_OK; }
extern "C" media_status_t AImage_getHeight(const AImage* image, int32_t* out) { *out = image->height; return AMEDIA_OK; }
extern "C" media_status_t AImage_getNumberOfPlanes(const AImage*, int32_t* out) { *out = 1; return AMEDIA_OK; }
extern "C" media_status_t AImage_getPlaneRowStride(const AImage* image, int, int32_t* out) { *out = image->rowStrideBytes; return AMEDIA_OK; }
extern "C" media_status_t AImage_getPlanePixelStride(const AImage* image, int, int32_t* out) { *out = image->pixelStrideBytes; return AMEDIA_OK; }
extern "C" void AHardwareBuffer_describe(const AHardwareBuffer* buffer, AHardwareBuffer_Desc* out) { *out = buffer->desc; }
static int lastLockFence = -1;
extern "C" int AHardwareBuffer_lock(AHardwareBuffer* buffer, uint64_t, int fence, const void*, void** out) { lastLockFence = fence; *out = buffer->storage.data(); return 0; }
extern "C" int AHardwareBuffer_unlock(AHardwareBuffer*, int* fence) { *fence = -1; return 0; }

namespace {
void require(bool value, const char* message) {
    if (!value) {
        std::cerr << "RAW_STILL_CAPTURE_MANAGER_TEST_FAIL " << message << '\n';
        std::exit(1);
    }
}
}  // namespace

int main() {
    constexpr uint32_t width = 4;
    constexpr uint32_t height = 3;
    constexpr uint32_t stridePixels = 6;
    constexpr uint32_t rowBytes = stridePixels * sizeof(uint16_t);

    AImage image{};
    image.width = width;
    image.height = height;
    image.rowStrideBytes = rowBytes;

    AHardwareBuffer buffer{};
    buffer.desc.width = width;
    buffer.desc.height = height;
    buffer.desc.stride = stridePixels;
    buffer.storage.assign(static_cast<size_t>(rowBytes) * height, 0xEE);
    std::array<uint16_t, width * height> expected{};
    for (uint32_t y = 0; y < height; ++y) {
        auto* row = reinterpret_cast<uint16_t*>(buffer.storage.data() + static_cast<size_t>(y) * rowBytes);
        for (uint32_t x = 0; x < width; ++x) {
            const uint16_t value = static_cast<uint16_t>(100 + y * width + x);
            row[x] = value;
            expected[y * width + x] = value;
        }
    }

    std::vector<std::string> diagnostics;
    rawrcam::capture::SingleShotSlot manager{
        [&](const std::string& line) { diagnostics.push_back(line); }};
    manager.configure(width, height, 11);
    require(manager.requestCapture(7), "first request rejected");
    require(!manager.requestCapture(8), "second pending request accepted");

    auto context = std::make_shared<rawrcam::metadata::CameraContextMetadata>();
    context->cameraId = "3";
    context->cameraContextGeneration = 11;
    context->geometry.rawBufferWidth = width;
    context->geometry.rawBufferHeight = height;
    rawrcam::metadata::FrameMetadataSnapshot metadata{};
    metadata.cameraContext = context;
    metadata.frameOrdinal = 42;
    metadata.timestampNs = 123456789;
    metadata.exposureTimeNs = 10'000'000;
    metadata.sensitivity = 200;

    rawrcam::color::FrameColorTransform colorState{};
    int gpuFence = 99;
    require(manager.captureMatchedFrame(&image, &buffer, -1, metadata, colorState, &gpuFence), "snapshot failed");
    require(gpuFence == -1, "unexpected GPU acquire fence");
    const auto* captured = manager.completedCapture();
    require(captured != nullptr, "completed snapshot missing");
    require(captured->requestId == 7, "request ID mismatch");
    require(captured->timestampNs == metadata.timestampNs, "timestamp mismatch");
    require(captured->metadata.frameOrdinal == 42, "frame ordinal mismatch");
    require(captured->metadata.cameraContext == context, "camera context not pinned");
    require(captured->packedRowStrideBytes == width * sizeof(uint16_t), "packed stride mismatch");
    require(captured->sourceRowStrideBytes == static_cast<int32_t>(rowBytes), "source stride mismatch");
    require(captured->raw16.size() == expected.size() * sizeof(uint16_t), "packed byte count mismatch");
    require(std::equal(captured->raw16.begin(), captured->raw16.end(),
                       reinterpret_cast<const uint8_t*>(expected.data())), "RAW samples not preserved");
    require(!manager.requestCapture(9), "occupied snapshot slot accepted new request");

    const auto retained = manager.completedCaptureHandle();
    require(retained != nullptr, "immutable snapshot handle missing");
    const auto retainedBytes = retained->raw16;
    manager.configure(width + 2, height + 1, 12);
    require(retained->requestId == 7, "retained request ID changed after reconfigure");
    require(retained->timestampNs == metadata.timestampNs, "retained timestamp changed after reconfigure");
    require(retained->width == width && retained->height == height, "retained geometry changed after reconfigure");
    require(retained->raw16 == retainedBytes, "retained RAW changed after reconfigure");
    require(manager.completedCapture() == nullptr, "manager should release old completed slot on reconfigure");

    manager.configure(width, height, 13);
    manager.releaseCompletedCapture();
    require(manager.completedCapture() == nullptr, "slot release failed");
    require(manager.requestCapture(10), "released slot not reusable");
    manager.cancelPendingCapture("camera_ingress_destroyed");
    require(!manager.captureRequested(), "ingress teardown did not cancel pending request");
    require(manager.requestCapture(11), "fresh request rejected after ingress teardown cancellation");

    // A request is pinned to the generation in which it was accepted.
    auto replacementContext = std::make_shared<rawrcam::metadata::CameraContextMetadata>();
    replacementContext->cameraId = "4";
    replacementContext->cameraContextGeneration = 14;
    replacementContext->geometry.rawBufferWidth = width;
    replacementContext->geometry.rawBufferHeight = height;
    rawrcam::metadata::FrameMetadataSnapshot replacementMetadata = metadata;
    replacementMetadata.cameraContext = replacementContext;
    replacementMetadata.timestampNs += 1;
    int replacementFence = -1;
    require(!manager.captureMatchedFrame(
                &image, &buffer, -1, replacementMetadata, colorState, &replacementFence),
            "request crossed camera generation");
    require(!manager.captureRequested(), "generation mismatch did not retire stale request");

    // Preserve the intentional pre-start semantic: an unconfigured request binds
    // to the first subsequent camera configuration.
    rawrcam::capture::SingleShotSlot prestartManager{};
    require(prestartManager.requestCapture(21), "pre-start request rejected");
    prestartManager.configure(width, height, 31);
    auto firstContext = std::make_shared<rawrcam::metadata::CameraContextMetadata>();
    firstContext->cameraContextGeneration = 31;
    firstContext->geometry.rawBufferWidth = width;
    firstContext->geometry.rawBufferHeight = height;
    rawrcam::metadata::FrameMetadataSnapshot firstMetadata = metadata;
    firstMetadata.cameraContext = firstContext;
    firstMetadata.timestampNs += 2;
    int firstFence = -1;
    require(prestartManager.captureMatchedFrame(
                &image, &buffer, -1, firstMetadata, colorState, &firstFence),
            "pre-start request did not bind to first configured generation");

    // A tagged optimized request must ignore interleaved repeating frames and
    // claim only the CaptureResult carrying the exact app request ID.
    rawrcam::capture::SingleShotSlot optimizedManager{};
    optimizedManager.configure(width, height, 41);
    require(optimizedManager.requestCapture(30), "optimized request rejected");
    optimizedManager.expectOptimizedFrame(30);
    auto optimizedContext = std::make_shared<rawrcam::metadata::CameraContextMetadata>();
    optimizedContext->cameraContextGeneration = 41;
    optimizedContext->geometry.rawBufferWidth = width;
    optimizedContext->geometry.rawBufferHeight = height;
    auto interleaved = metadata;
    interleaved.cameraContext = optimizedContext;
    interleaved.timestampNs += 3;
    int optimizedFence = -1;
    require(!optimizedManager.captureMatchedFrame(
                &image, &buffer, -1, interleaved, colorState, &optimizedFence),
            "interleaved Camera2 frame was mistaken for the optimized still");
    auto optimized = interleaved;
    optimized.timestampNs += 1;
    optimized.optimizedStillRequestId = 30;
    require(optimizedManager.captureMatchedFrame(
                &image, &buffer, -1, optimized, colorState, &optimizedFence),
            "exact optimized request ID was not captured");

    // A camera frame copied without a preview submission must still wait for
    // its acquire fence; post-preview copies explicitly use no acquire fence.
    std::vector<uint8_t> backgroundRaw(width * height * sizeof(uint16_t));
    const auto backgroundCopy = rawrcam::imaging::copyCompletedRaw16AhbToPacked(
        &buffer, width, height, backgroundRaw, 123);
    require(backgroundCopy.success && lastLockFence == 123, "background copy ignored camera acquire fence");
    require(backgroundRaw == retainedBytes, "background copy changed RAW values");
    const auto retiredCopy = rawrcam::imaging::copyCompletedRaw16AhbToPacked(
        &buffer, width, height, backgroundRaw);
    require(retiredCopy.success && lastLockFence == -1, "retired copy unexpectedly used an acquire fence");

    std::cout << "RAW_STILL_CAPTURE_MANAGER_TEST_PASS\n";
    return 0;
}
