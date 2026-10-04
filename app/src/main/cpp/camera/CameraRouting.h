#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "geometry/RawGeometry.h"

namespace rawrcam::camera {

// One Camera2 metadata entry to inject. The tag is a metadata name (public or
// vendor, e.g. "vivo.control.forceSensorMode") or a numeric id ("0x80020000").
// Session keys go into the session parameters of every capture session;
// request keys go into every capture request (stills and video both come from
// the repeating RAW request).
struct CameraKeySetting {
    enum class Type : uint8_t { Byte, Int32, Int64, Float, Double };
    enum class Scope : uint8_t { Session, Request };
    std::string tag;
    Type type = Type::Int32;
    Scope scope = Scope::Session;
    std::vector<double> values;
    bool enabled = true;
};

// Black/white levels for a lens. Dynamic uses the camera's characteristics
// and per-frame dynamic levels; Static replaces both on every frame (needed
// when a vendor sensor mode changes the readout bit depth).
struct LevelOverride {
    bool isStatic = false;
    std::array<float, 4> blackRggb{0, 0, 0, 0};
    float white = 0.0f;
};

// How one UI lens maps onto the device. A camera profile is the full set of
// these for a device: either a built-in default or the user's configuration.
struct LensRoute {
    std::string lensId;            // App lens id; also the capture-screen label.
    std::string cameraId;          // Camera2 id to open (logical, or a hidden/physical id).
    std::string physicalCameraId;  // Physical sub-camera of cameraId to stream from; empty for none.
    geometry::RawStreamPreference preferredStream;
    // The stream actually configured. Zero until CameraDeviceSession::select
    // negotiates it against the camera's advertised RAW outputs.
    geometry::RawStreamOption stream;
    std::vector<CameraKeySetting> keys;
    LevelOverride levels;
};

struct CameraProfile {
    std::string id;
    std::vector<LensRoute> lenses;
};

// "v2562": the development device's tuned routes. "generic": logical back
// camera 0 with a negotiated RAW stream and no vendor keys.
[[nodiscard]] const CameraProfile& builtInCameraProfile(const std::string& profileId);
[[nodiscard]] const std::string& builtInProfileIdForModel(const std::string& productModel);

[[nodiscard]] std::optional<LensRoute> routeForLens(const CameraProfile& profile, const std::string& lensId);
// Explicit camera-id override (debug intent / diagnostics): the profile's
// route for that camera when it has one, otherwise a negotiated generic route.
[[nodiscard]] LensRoute routeForCameraId(const CameraProfile& profile, const std::string& cameraId);

}  // namespace rawrcam::camera
