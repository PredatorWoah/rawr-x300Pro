#include "metadata/MetadataDiagnostics.h"

#include <sstream>

namespace rawrcam::metadata {
namespace {

void appendMatrix(std::ostringstream& s, const char* name, const Matrix3x3& matrix) {
    s << ' ' << name << '=';
    if (!matrix.valid) {
        s << "<missing>";
        return;
    }
    s << '[';
    for (size_t i = 0; i < matrix.rowMajor.size(); ++i) {
        if (i) s << ',';
        s << matrix.rowMajor[i];
    }
    s << ']';
}

}  // namespace

std::string describe(const CameraContextMetadata& m) {
    std::ostringstream s;
    s << "CAMERA_CONTEXT_METADATA generation=" << m.cameraContextGeneration << " cameraId=" << m.cameraId
      << " lensId=" << m.lensId << " raw=" << m.geometry.rawBufferWidth << 'x' << m.geometry.rawBufferHeight
      << " pixelArray=" << m.geometry.pixelArrayWidth << 'x' << m.geometry.pixelArrayHeight
      << " cfaCamera2=" << m.camera2Cfa << " cfaRawPreview=" << m.rawPreviewCfa << " white=" << m.baselineWhiteLevel
      << " blackRggb=[" << m.baselineBlackLevelPhysicalRggb[0] << ',' << m.baselineBlackLevelPhysicalRggb[1] << ','
      << m.baselineBlackLevelPhysicalRggb[2] << ',' << m.baselineBlackLevelPhysicalRggb[3] << ']' << " preCorrection=";
    if (m.geometry.preCorrectionActiveArray.valid) {
        const auto& r = m.geometry.preCorrectionActiveArray;
        s << '[' << r.left << ',' << r.top << ',' << r.right << ',' << r.bottom << ']';
    } else
        s << "<missing>";
    s << " active=";
    if (m.geometry.activeArray.valid) {
        const auto& r = m.geometry.activeArray;
        s << '[' << r.left << ',' << r.top << ',' << r.right << ',' << r.bottom << ']';
    } else
        s << "<missing>";
    s << " orientation=" << m.geometry.sensorOrientationDegrees << " refIlluminants=" << m.color.referenceIlluminant1
      << ',' << m.color.referenceIlluminant2;
    appendMatrix(s, "color1", m.color.colorTransform1);
    appendMatrix(s, "color2", m.color.colorTransform2);
    appendMatrix(s, "calibration1", m.color.calibrationTransform1);
    appendMatrix(s, "calibration2", m.color.calibrationTransform2);
    appendMatrix(s, "forward1", m.color.forwardMatrix1);
    appendMatrix(s, "forward2", m.color.forwardMatrix2);
    s << " shadingApplied=" << (m.hasLensShadingApplied ? (m.lensShadingApplied ? "true" : "false") : "<unknown>")
      << " maxAnalog=" << m.maxAnalogSensitivity
      << " greenSplit=" << (m.hasGreenSplit ? std::to_string(m.greenSplit) : std::string("<unknown>"))
      << " opticalBlackRegions=" << m.opticalBlackRegions.size() << " lensDistortion=";
    if (m.lensDistortion.empty()) {
        s << "<missing>";
    } else {
        s << '[' << m.lensDistortion[0] << ',' << m.lensDistortion[1] << ',' << m.lensDistortion[2] << ','
          << m.lensDistortion[3] << ',' << m.lensDistortion[4] << ']';
    }
    s << " lensIntrinsic=";
    if (m.lensIntrinsicCalibration.empty()) {
        s << "<missing>";
    } else {
        s << '[' << m.lensIntrinsicCalibration[0] << ',' << m.lensIntrinsicCalibration[1] << ','
          << m.lensIntrinsicCalibration[2] << ',' << m.lensIntrinsicCalibration[3] << ','
          << m.lensIntrinsicCalibration[4] << ']';
    }
    s << " flashAvailable=" << (m.hasFlashInfoAvailable ? (m.flashInfoAvailable ? "true" : "false") : "<unknown>");
    return s.str();
}

std::string describe(const FrameMetadataSnapshot& m) {
    std::ostringstream s;
    s << "FRAME_METADATA_SNAPSHOT generation=" << (m.cameraContext ? m.cameraContext->cameraContextGeneration : 0)
      << " frameOrdinal=" << m.frameOrdinal << " timestampNs=" << m.timestampNs
      << " cameraId=" << (m.cameraContext ? m.cameraContext->cameraId : std::string("<null>"))
      << " lensId=" << (m.cameraContext ? m.cameraContext->lensId : std::string("<null>"))
      << " raw=" << (m.cameraContext ? m.cameraContext->geometry.rawBufferWidth : 0) << 'x'
      << (m.cameraContext ? m.cameraContext->geometry.rawBufferHeight : 0) << " blackRggb=["
      << m.blackLevelPhysicalRggb[0] << ',' << m.blackLevelPhysicalRggb[1] << ',' << m.blackLevelPhysicalRggb[2] << ','
      << m.blackLevelPhysicalRggb[3] << "] staticWhite=" << m.staticWhiteLevel << " reportedDynamicWhite="
      << (m.reportedDynamicWhiteLevel ? std::to_string(*m.reportedDynamicWhiteLevel) : std::string("<unavailable>"))
      << " effectiveWhite=" << m.effectiveWhiteLevel
      << " effectiveWhiteSource=" << effectiveWhiteLevelSourceName(m.effectiveWhiteLevelSource)
      << " gainsRggb=[" << m.colorCorrectionGainsRggb[0] << ',' << m.colorCorrectionGainsRggb[1] << ','
      << m.colorCorrectionGainsRggb[2] << ',' << m.colorCorrectionGainsRggb[3] << ']' << " neutralPoint=";
    if (m.hasNeutralColorPoint)
        s << '[' << m.neutralColorPoint[0] << ',' << m.neutralColorPoint[1] << ',' << m.neutralColorPoint[2] << ']';
    else
        s << "<missing>";
    appendMatrix(s, "resultTransform", m.colorCorrectionTransform);
    s << " colorMode=" << m.colorCorrectionMode << " awbMode=" << m.awbMode << " awbState=" << m.awbState
      << " exposureNs=" << m.exposureTimeNs << " sensitivity=" << m.sensitivity << " scalerCrop=";
    if (m.scalerCropRegion.valid)
        s << '[' << m.scalerCropRegion.left << ',' << m.scalerCropRegion.top << ',' << m.scalerCropRegion.right << ','
          << m.scalerCropRegion.bottom << ']';
    else
        s << "<missing>";
    s << " rawCrop=";
    if (m.rawCropRegion.valid)
        s << '[' << m.rawCropRegion.left << ',' << m.rawCropRegion.top << ',' << m.rawCropRegion.right << ','
          << m.rawCropRegion.bottom << ']';
    else
        s << "<unavailable>";
    s << " flashState=" << m.flashState << " hotPixels=" << (m.hotPixelMap.size() / 2)
      << " noiseProfile=" << m.sensorNoiseProfile.size();
    return s.str();
}

}  // namespace rawrcam::metadata
