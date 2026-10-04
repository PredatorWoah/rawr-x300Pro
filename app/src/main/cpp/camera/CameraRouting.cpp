#include "camera/CameraRouting.h"

namespace rawrcam::camera {

namespace {
using geometry::RawPixelFormat;

constexpr const char* kVivoForceSensorMode = "vivo.control.forceSensorMode";

LensRoute route(const char* lensId, const char* cameraId, uint32_t width, uint32_t height) {
    LensRoute r;
    r.lensId = lensId;
    r.cameraId = cameraId;
    r.preferredStream = {RawPixelFormat::Raw16, width, height};
    return r;
}

CameraKeySetting forceSensorMode(int32_t mode) {
    return {kVivoForceSensorMode, CameraKeySetting::Type::Int32, CameraKeySetting::Scope::Session, {double(mode)}};
}

LevelOverride staticLevels(float black, float white) { return {true, {black, black, black, black}, white}; }

// vivo sensor modes are keyed by lens, not camera: the ISZ lenses share
// camera 5 with tele but need their own crop-readout modes (31/6, 10-bit).
// The other lenses use their DCG readout modes (more than 10 bits).
LensRoute withSensorMode(LensRoute r, int32_t mode, float black, float white) {
    r.keys = {forceSensorMode(mode)};
    r.levels = staticLevels(black, white);
    return r;
}

CameraProfile makeV2562() {
    return {"v2562",
            {
                withSensorMode(route("14", "4", 4096, 3072), 23, 1024.0f, 8712.0f),
                withSensorMode(route("35", "3", 4080, 3064), 17, 1024.0f, 8712.0f),
                withSensorMode(route("85", "5", 4080, 3072), 19, 1024.0f, 16383.0f),
                withSensorMode(route("170", "5", 4080, 3072), 31, 64.0f, 1023.0f),
                withSensorMode(route("340", "5", 4080, 3072), 6, 64.0f, 1023.0f),
            }};
}

CameraProfile makeGeneric() { return {"generic", {route("1x", "0", 0, 0)}}; }
}  // namespace

const CameraProfile& builtInCameraProfile(const std::string& profileId) {
    static const CameraProfile v2562 = makeV2562();
    static const CameraProfile generic = makeGeneric();
    return profileId == v2562.id ? v2562 : generic;
}

const std::string& builtInProfileIdForModel(const std::string& productModel) {
    static const std::string v2562 = "v2562";
    static const std::string generic = "generic";
    return productModel == "V2562" ? v2562 : generic;
}

std::optional<LensRoute> routeForLens(const CameraProfile& profile, const std::string& lensId) {
    for (const auto& r : profile.lenses)
        if (r.lensId == lensId) return r;
    return std::nullopt;
}

LensRoute routeForCameraId(const CameraProfile& profile, const std::string& cameraId) {
    for (const auto& r : profile.lenses)
        if (r.cameraId == cameraId && r.physicalCameraId.empty()) return r;
    LensRoute adHoc;
    adHoc.lensId = "1x";
    adHoc.cameraId = cameraId;
    return adHoc;
}

}  // namespace rawrcam::camera
