#include <filesystem>
#include "diagnostics/logging/FrameAuditWriter.h"

#include <cassert>
#include <cstdio>
#include <fstream>
#include <memory>
#include <string>

int main() {
    const std::string path = (std::filesystem::temp_directory_path() / "rawrcam_frame_audit_writer_test.jsonl").string();
    std::remove(path.c_str());

    auto context = std::make_shared<rawrcam::metadata::CameraContextMetadata>();
    context->cameraContextGeneration = 7;
    context->cameraId = "5";
    context->lensId = "tele";
    context->geometry.rawBufferWidth = 4080;
    context->geometry.rawBufferHeight = 3072;
    context->geometry.pixelArrayWidth = 4080;
    context->geometry.pixelArrayHeight = 3072;
    context->rawPreviewCfa = 0;
    context->camera2Cfa = 0;
    context->baselineBlackLevelPhysicalRggb = {64,64,64,64};
    context->baselineWhiteLevel = 1023;
    context->color.referenceIlluminant1 = 21;
    context->color.referenceIlluminant2 = 17;

    rawrcam::metadata::FrameMetadataSnapshot frame{};
    frame.cameraContext = context;
    frame.timestampNs = 123456789;
    frame.blackLevelPhysicalRggb = {64.25f,64.25f,64.25f,64.25f};
    frame.staticWhiteLevel = 1023;
    frame.reportedDynamicWhiteLevel = 1023.0f;
    frame.effectiveWhiteLevel = 1023;
    frame.effectiveWhiteLevelSource = rawrcam::metadata::EffectiveWhiteLevelSource::ReportedDynamic;
    frame.colorCorrectionGainsRggb = {2.0f,1.0f,1.0f,1.5f};
    frame.neutralColorPoint = {0.5f,1.0f,0.6666667f};
    frame.hasNeutralColorPoint = true;
    frame.exposureTimeNs = 10000000;
    frame.sensitivity = 100;

    rawrcam::color::FrameColorTransform state{};
    state.source = "TEST_COLOR_PATH";
    state.baselineWbRggb = frame.colorCorrectionGainsRggb;
    state.hasEstimatedWhite = true;
    state.estimatedWhiteX = 0.32f;
    state.estimatedWhiteY = 0.34f;
    state.estimatedCctKelvin = 5600.0f;
    state.calibrationWeight1 = 0.7f;

    {
        rawrcam::diagnostics::FrameAuditWriter writer(path);
        writer.setEnabled(true);
        writer.recordCameraContext(*context);

        // Sparse sampling: frames 1-3 plus every 15th frame.
        for (std::uint64_t ordinal = 1; ordinal <= 30; ++ordinal) {
            frame.frameOrdinal = ordinal;
            frame.timestampNs = ordinal * 1000;
            writer.recordCompletedFrame(frame, state);
        }
    }

    std::ifstream in(path);
    assert(in.good());
    std::string all((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    assert(all.find("\"schemaVersion\":2") != std::string::npos);
    assert(all.find("\"record\":\"cameraContext\"") != std::string::npos);
    assert(all.find("\"record\":\"frameEvidence\"") != std::string::npos);
    assert(all.find("\"auditReason\":\"sparse\"") != std::string::npos);
    assert(all.find("\"reportedDynamicWhiteLevel\":1023") != std::string::npos);
    assert(all.find("\"effectiveWhiteLevelSource\":\"reported_dynamic\"") != std::string::npos);
    assert(all.find("\"cameraId\":\"5\"") != std::string::npos);
    assert(all.find("\"colorSource\":\"TEST_COLOR_PATH\"") != std::string::npos);
    assert(all.find("\"frameOrdinal\":1") != std::string::npos);
    assert(all.find("\"frameOrdinal\":15") != std::string::npos);
    assert(all.find("\"frameOrdinal\":30") != std::string::npos);
    // Non-sparse frames must not be persisted.
    assert(all.find("\"frameOrdinal\":4") == std::string::npos);
    assert(all.find("\"frameOrdinal\":11") == std::string::npos);
    std::remove(path.c_str());
    return 0;
}
