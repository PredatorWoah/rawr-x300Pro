#include "metadata/MetadataValidation.h"

#include <algorithm>
#include <cmath>

namespace rawrcam::metadata {
namespace {

bool finiteMatrix(const Matrix3x3& matrix) {
    if (!matrix.valid) return true;
    for (const float value : matrix.rowMajor)
        if (!std::isfinite(value)) return false;
    return true;
}

bool finitePositiveGains(const std::array<float, 4>& gains) {
    for (const float value : gains)
        if (!std::isfinite(value) || value <= 0.0f) return false;
    return true;
}

bool validRectIfPresent(const RectI& r) { return !r.valid || (r.right > r.left && r.bottom > r.top); }

}  // namespace

bool validate(const CameraContextMetadata& m, std::string* error) {
    if (m.cameraContextGeneration == 0) {
        if (error) *error = "camera generation is zero";
        return false;
    }
    if (m.cameraId.empty()) {
        if (error) *error = "cameraId is empty";
        return false;
    }
    if (m.geometry.rawBufferWidth == 0 || m.geometry.rawBufferHeight == 0) {
        if (error) *error = "RAW buffer geometry is empty";
        return false;
    }
    if (m.rawPreviewCfa > 3) {
        if (error) *error = "unsupported raw_preview CFA";
        return false;
    }
    const float maxBlack =
        *std::max_element(m.baselineBlackLevelPhysicalRggb.begin(), m.baselineBlackLevelPhysicalRggb.end());
    if (!std::isfinite(m.baselineWhiteLevel) || m.baselineWhiteLevel <= maxBlack) {
        if (error) *error = "white level must be finite and greater than black levels";
        return false;
    }
    for (const float value : m.baselineBlackLevelPhysicalRggb) {
        if (!std::isfinite(value) || value < 0.0f) {
            if (error) *error = "invalid black level";
            return false;
        }
    }
    if (!validRectIfPresent(m.geometry.preCorrectionActiveArray) || !validRectIfPresent(m.geometry.activeArray)) {
        if (error) *error = "invalid camera-context active geometry";
        return false;
    }
    const auto& c = m.color;
    if (!finiteMatrix(c.colorTransform1) || !finiteMatrix(c.colorTransform2) ||
        !finiteMatrix(c.calibrationTransform1) || !finiteMatrix(c.calibrationTransform2) ||
        !finiteMatrix(c.forwardMatrix1) || !finiteMatrix(c.forwardMatrix2)) {
        if (error) *error = "non-finite static color matrix";
        return false;
    }
    return true;
}

bool validate(const FrameMetadataSnapshot& m, std::string* error) {
    if (!m.cameraContext) {
        if (error) *error = "frame has no camera-context snapshot";
        return false;
    }
    std::string contextError;
    if (!validate(*m.cameraContext, &contextError)) {
        if (error) *error = "invalid pinned camera context: " + contextError;
        return false;
    }
    if (m.frameOrdinal == 0) {
        if (error) *error = "frame ordinal is zero";
        return false;
    }
    if (m.timestampNs == 0) {
        if (error) *error = "frame timestamp is zero";
        return false;
    }
    // White-level values reported by Camera2 are observations, not a frame-admission
    // invariant. In particular, forced Vivo sensor/DCG modes can make the static or
    // dynamic Camera2 white level inconsistent with that mode's authoritative RAW
    // normalization. Do not reject an otherwise valid CaptureResult here. The native
    // camera/session owner resolves the effective processing WL/BL after this snapshot
    // is created and preserves the reported values as evidence.
    for (const float value : m.blackLevelPhysicalRggb) {
        if (!std::isfinite(value) || value < 0.0f) {
            if (error) *error = "invalid frame black level";
            return false;
        }
    }
    if (!finitePositiveGains(m.colorCorrectionGainsRggb)) {
        if (error) *error = "invalid color-correction gains";
        return false;
    }
    if (!finiteMatrix(m.colorCorrectionTransform)) {
        if (error) *error = "non-finite capture-result color transform";
        return false;
    }
    if (m.hasNeutralColorPoint) {
        for (const float value : m.neutralColorPoint) {
            if (!std::isfinite(value) || value <= 0.0f) {
                if (error) *error = "invalid neutral color point";
                return false;
            }
        }
    }
    if (!validRectIfPresent(m.scalerCropRegion) || !validRectIfPresent(m.rawCropRegion)) {
        if (error) *error = "invalid per-frame crop geometry";
        return false;
    }
    if (m.exposureTimeNs < 0 || m.sensitivity < 0) {
        if (error) *error = "invalid exposure metadata";
        return false;
    }
    return true;
}

}  // namespace rawrcam::metadata
