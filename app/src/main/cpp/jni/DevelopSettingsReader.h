#pragma once
#include "develop/DevelopSettings.h"
#include "jni/JsonObject.h"
namespace rawrcam::jni {
struct DenoiseIntent {
    bool master = false;
    int method = 0;
    float waveletStrength = 0.0f;
    float waveletDetail = 1.0f;
    float waveletForceY = 0.25f;
    int waveletMaxScale = 7;
    int rawMode = 0;
    float rawStrength = 1.0f;
    float rawLuma = 1.0f;
    float rawChroma = 1.0f;
    int yuvMode = 0;
    float yuvStrengthY = 1.0f;
    float yuvStrengthC = 1.0f;
};
DenoiseIntent parseDenoiseSpec(JNIEnv* env, jintArray modes, jfloatArray strengths);
struct CaptureDevelopInput {
    jboolean pipelineDiagnosticsEnabled;
    jint colorRenderProfile;
    std::string importedLutProfileId;
    jboolean dualAutoContrast;
    jfloat dualContrastPercent;
    jint fccSteps;
    jboolean defringeEnabled;
    jfloat defringeStrength, defringeEdgeThreshold, defringeLumaFloor;
    DenoiseIntent denoise;
    jboolean lensShadingCorrectionEnabled, highlightReconstructionEnabled, distortionCorrectionEnabled;
    jint demosaicAlgorithm;
};
develop::DevelopSettings readCaptureDevelopSettings(const CaptureDevelopInput& input);
develop::DevelopSettings readRendererDevelopSettings(Json& json);
}  // namespace rawrcam::jni
