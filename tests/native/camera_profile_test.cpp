#include <cassert>
#include <iostream>
#include <string>
#include <vector>

#include "camera/CameraProfileJson.h"
#include "camera/CameraRouting.h"
#include "geometry/RawGeometry.h"

using rawrcam::geometry::isSupportedRawGeometry;
using rawrcam::geometry::negotiateRawStream;
using rawrcam::geometry::RawPixelFormat;
using rawrcam::geometry::RawStreamOption;
using rawrcam::geometry::RawStreamPreference;

namespace {

void testGeometryGate() {
    assert(isSupportedRawGeometry(4080, 3064));
    assert(isSupportedRawGeometry(4096, 3072));
    assert(isSupportedRawGeometry(4000, 3000));
    assert(isSupportedRawGeometry(8192, 6144));
    assert(isSupportedRawGeometry(12288, 4096));
    assert(!isSupportedRawGeometry(4081, 3064));   // odd width
    assert(!isSupportedRawGeometry(4080, 3065));   // odd height
    assert(!isSupportedRawGeometry(640, 480));     // below multiframe minimum
    assert(!isSupportedRawGeometry(16386, 4096));  // above maxImageDimension2D
}

void testNegotiation() {
    const std::vector<RawStreamOption> x200u = {
        {RawPixelFormat::Raw16, 4096, 3072},
        {RawPixelFormat::Raw10, 4096, 3072},
    };
    // Preferred size missing (dev-phone 4080x3064 route on another phone) -> largest RAW16.
    auto chosen = negotiateRawStream(x200u, {RawPixelFormat::Raw16, 4080, 3064});
    assert(chosen && *chosen == (RawStreamOption{RawPixelFormat::Raw16, 4096, 3072}));

    // Exact preferred size is kept even when a larger one exists.
    const std::vector<RawStreamOption> devPhone = {
        {RawPixelFormat::Raw16, 4080, 3072},
        {RawPixelFormat::Raw16, 4080, 3064},
    };
    chosen = negotiateRawStream(devPhone, {RawPixelFormat::Raw16, 4080, 3064});
    assert(chosen && *chosen == (RawStreamOption{RawPixelFormat::Raw16, 4080, 3064}));

    // "Negotiate" preference picks the largest area.
    chosen = negotiateRawStream(devPhone, {});
    assert(chosen && chosen->height == 3072);

    // RAW10-only device falls back to RAW10.
    const std::vector<RawStreamOption> raw10Only = {{RawPixelFormat::Raw10, 4000, 3000},
                                                    {RawPixelFormat::Raw10, 2000, 1500}};
    chosen = negotiateRawStream(raw10Only, {});
    assert(chosen && *chosen == (RawStreamOption{RawPixelFormat::Raw10, 4000, 3000}));

    // An explicit RAW10 preference wins over available RAW16.
    chosen = negotiateRawStream(x200u, {RawPixelFormat::Raw10, 0, 0});
    assert(chosen && chosen->format == RawPixelFormat::Raw10);

    // Unusable sizes are skipped: odd, too large, RAW10 width not a multiple of 4.
    const std::vector<RawStreamOption> awkward = {{RawPixelFormat::Raw16, 4081, 3064},
                                                  {RawPixelFormat::Raw16, 20000, 15000},
                                                  {RawPixelFormat::Raw10, 4002, 3000},
                                                  {RawPixelFormat::Raw16, 3264, 2448}};
    chosen = negotiateRawStream(awkward, {});
    assert(chosen && *chosen == (RawStreamOption{RawPixelFormat::Raw16, 3264, 2448}));

    assert(!negotiateRawStream({}, {}));
    assert(!negotiateRawStream({{RawPixelFormat::Raw16, 640, 480}}, {}));
}

// The seeded V2562 profile must reproduce the old hardcoded vivo sensor-mode
// table: DCG readout modes (main/wide/tele) and ISZ crop modes with 10-bit levels.
void testProfiles() {
    using namespace rawrcam::camera;
    assert(builtInProfileIdForModel("V2562") == "v2562");
    assert(builtInProfileIdForModel("V2454A") == "generic");

    const auto& dev = builtInCameraProfile("v2562");
    assert(dev.lenses.size() == 5);
    const char* order[] = {"14", "35", "85", "170", "340"};
    for (size_t i = 0; i < 5; ++i) assert(dev.lenses[i].lensId == order[i]);

    struct Expected {
        const char* lens;
        const char* camera;
        int mode;
        float black, white;
    };
    // DCG readout modes (main/wide/tele) and ISZ crop modes are plain session keys.
    const Expected table[] = {{"14", "4", 23, 1024, 8712},
                              {"35", "3", 17, 1024, 8712},
                              {"85", "5", 19, 1024, 16383},
                              {"170", "5", 31, 64, 1023},
                              {"340", "5", 6, 64, 1023}};
    for (const auto& e : table) {
        const auto r = routeForLens(dev, e.lens);
        assert(r && r->cameraId == e.camera && r->physicalCameraId.empty());
        assert(r->preferredStream.format == RawPixelFormat::Raw16);
        const auto& keys = r->keys;
        const auto& levels = r->levels;
        assert(keys.size() == 1 && keys[0].tag == "vivo.control.forceSensorMode");
        assert(keys[0].type == CameraKeySetting::Type::Int32 && keys[0].scope == CameraKeySetting::Scope::Session);
        assert(keys[0].values == std::vector<double>{double(e.mode)} && keys[0].enabled);
        assert(levels.isStatic && levels.white == e.white);
        for (float b : levels.blackRggb) assert(b == e.black);
    }
    const auto main = routeForLens(dev, "35");
    assert(main->preferredStream.width == 4080 && main->preferredStream.height == 3064);

    const auto& generic = builtInCameraProfile("generic");
    assert(generic.lenses.size() == 1);
    const auto only = routeForLens(generic, "1x");
    assert(only && only->cameraId == "0" && only->keys.empty() && !only->levels.isStatic);
    assert(only->preferredStream.width == 0 && only->preferredStream.height == 0);
    // Unknown ids fall back to the generic profile.
    assert(builtInCameraProfile("nope").id == "generic");

    // Camera-id override: known id keeps its tuned route, unknown id gets a negotiated one.
    assert(routeForCameraId(dev, "5").lensId == "85");
    const auto adHoc = routeForCameraId(dev, "7");
    assert(adHoc.cameraId == "7" && adHoc.keys.empty() && adHoc.preferredStream.width == 0);
}

void testProfileJson() {
    using namespace rawrcam::camera;
    // Round trip keeps every field of the built-in profiles.
    for (const char* id : {"v2562", "generic"}) {
        const auto& original = builtInCameraProfile(id);
        const auto json = serializeCameraProfile(original);
        std::string error;
        const auto parsed = parseCameraProfile(json, &error);
        assert(parsed && error.empty());
        assert(serializeCameraProfile(*parsed) == json);
    }

    // A user profile: escaped name, physical camera, RAW10 stream, mixed key scopes.
    const std::string user = R"({"id":"user","lenses":[{"id":"W\"1","cameraId":"2","physicalCameraId":"5",
        "stream":{"format":"RAW10","width":4000,"height":3000},
        "levels":{"static":true,"black":[64],"white":1023},
        "keys":[{"tag":"0x80020000","type":"int64","scope":"request","values":[1,2],"enabled":false},
                {"tag":"com.vendor.mode","type":"byte","scope":"session","values":[3]}],
        "unknown":{"nested":[null,true]}}]})";
    std::string error;
    const auto parsed = parseCameraProfile(user, &error);
    assert(parsed && parsed->lenses.size() == 1);
    const auto& r = parsed->lenses[0];
    assert(r.lensId == "W\"1" && r.cameraId == "2" && r.physicalCameraId == "5");
    assert(r.preferredStream.format == RawPixelFormat::Raw10 && r.preferredStream.width == 4000);
    assert(r.levels.isStatic && r.levels.white == 1023 && r.levels.blackRggb[3] == 64);
    assert(r.keys.size() == 2 && !r.keys[0].enabled && r.keys[0].scope == CameraKeySetting::Scope::Request);
    assert(r.keys[0].type == CameraKeySetting::Type::Int64 && r.keys[0].values.size() == 2);
    assert(r.keys[1].enabled && r.keys[1].scope == CameraKeySetting::Scope::Session);
    // The escaped name survives a second round trip.
    assert(parseCameraProfile(serializeCameraProfile(*parsed))->lenses[0].lensId == "W\"1");

    // Rejected: malformed JSON, no lenses, duplicate ids, unknown key type, static levels without white.
    assert(!parseCameraProfile("{\"lenses\":[", &error) && !error.empty());
    assert(!parseCameraProfile(R"({"lenses":[]})"));
    assert(!parseCameraProfile(R"({"lenses":[{"id":"a","cameraId":"0"},{"id":"a","cameraId":"1"}]})"));
    assert(!parseCameraProfile(R"({"lenses":[{"id":"a","cameraId":"0","keys":[{"tag":"x","type":"half"}]}]})"));
    assert(!parseCameraProfile(R"({"lenses":[{"id":"a","cameraId":"0","levels":{"static":true,"black":[1]}}]})"));
}

}  // namespace

int main() {
    testGeometryGate();
    testNegotiation();
    testProfiles();
    testProfileJson();
    std::cout << "camera_profile_test passed\n";
    return 0;
}
