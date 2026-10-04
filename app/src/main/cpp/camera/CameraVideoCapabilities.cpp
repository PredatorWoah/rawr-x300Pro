#include "camera/CameraVideoCapabilities.h"

#include <camera/NdkCameraMetadata.h>
#include <camera/NdkCameraMetadataTags.h>
#include <media/NdkImage.h>

#include <memory>
#include <optional>

#include "camera/CameraDiscovery.h"
#include "camera/CameraVideoFacts.h"

namespace rawrcam::camera {
namespace {
std::optional<ACameraMetadata_const_entry> entry(const ACameraMetadata* metadata, uint32_t tag, uint8_t type) {
    ACameraMetadata_const_entry value{};
    if (ACameraMetadata_getConstEntry(metadata, tag, &value) != ACAMERA_OK || value.type != type || !value.count) {
        return std::nullopt;
    }
    return value;
}
std::optional<int32_t> integer(const ACameraMetadata* metadata, uint32_t tag, uint8_t type) {
    const auto value = entry(metadata, tag, type);
    if (!value) return std::nullopt;
    if (type == ACAMERA_TYPE_BYTE && value->data.u8) return value->data.u8[0];
    if (type == ACAMERA_TYPE_INT32 && value->data.i32) return value->data.i32[0];
    return std::nullopt;
}
}  // namespace

std::string cameraVideoCapabilitiesSnapshot() {
    std::unique_ptr<ACameraManager, decltype(&ACameraManager_delete)> manager(ACameraManager_create(), ACameraManager_delete);
    if (!manager) return serializeCameraVideoFacts({}, "Camera manager unavailable");
    std::vector<CameraVideoFacts> cameras;
    for (const auto& id : cameraDiscoveryCandidates(manager.get())) {
        CameraVideoFacts camera;
        camera.id = id;
        ACameraMetadata* raw = nullptr;
        const auto status = ACameraManager_getCameraCharacteristics(manager.get(), id.c_str(), &raw);
        std::unique_ptr<ACameraMetadata, decltype(&ACameraMetadata_free)> metadata(raw, ACameraMetadata_free);
        if (status != ACAMERA_OK || !metadata) {
            camera.error = "Camera characteristics unavailable (status " + std::to_string(status) + ")";
        } else {
            camera.sensorOrientationDegrees = integer(raw, ACAMERA_SENSOR_ORIENTATION, ACAMERA_TYPE_INT32);
            camera.lensFacing = integer(raw, ACAMERA_LENS_FACING, ACAMERA_TYPE_BYTE);
            camera.timestampSource = integer(raw, ACAMERA_SENSOR_INFO_TIMESTAMP_SOURCE, ACAMERA_TYPE_BYTE);
            const auto configurations = entry(raw, ACAMERA_SCALER_AVAILABLE_STREAM_CONFIGURATIONS, ACAMERA_TYPE_INT32);
            const auto durations = entry(raw, ACAMERA_SCALER_AVAILABLE_MIN_FRAME_DURATIONS, ACAMERA_TYPE_INT64);
            camera.raw = readRawVideoModes(configurations ? configurations->data.i32 : nullptr,
                                          configurations ? configurations->count : 0,
                                          durations ? durations->data.i64 : nullptr, durations ? durations->count : 0,
                                          AIMAGE_FORMAT_RAW16, ACAMERA_SCALER_AVAILABLE_STREAM_CONFIGURATIONS_OUTPUT);
        }
        cameras.push_back(std::move(camera));
    }
    return serializeCameraVideoFacts(cameras);
}
}  // namespace rawrcam::camera
