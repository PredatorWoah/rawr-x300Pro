#pragma once

#include <optional>

#include "camera/CameraControlTypes.h"
#include "metadata/FrameMetadataSnapshot.h"

namespace rawrcam::camera {

struct WhiteBalanceEstimate {
    int32_t temperatureK;
    int32_t tint;
    bool calibrated;
};

// Gesture axes are explicit: native requested values can lag the UI readout.
enum WhiteBalanceEditedAxis : int32_t { WbSeedBoth = 0, WbTemperature = 1, WbTint = 2, WbBoth = 3 };

// Pure camera policy. Caller owns locking and Camera2 submission. Estimation runs
// on entry or snapshot publication, never on the CaptureResult callback.
std::optional<WhiteBalanceEstimate> estimateWhiteBalance(const CameraControlState& state);
void observeWhiteBalanceResult(CameraControlState& state, const metadata::FrameMetadataSnapshot& frame);
void publishWhiteBalanceEstimate(CameraControlState& snapshot);
bool requestWhiteBalanceMode(CameraControlState& state, WhiteBalanceControlMode mode, int64_t requestId);
bool requestWhiteBalanceTempTint(CameraControlState& state, int32_t temperatureK, int32_t tint, int32_t editedAxes,
                                 int64_t requestId);
bool requestWhiteBalanceLocked(CameraControlState& state, int32_t temperatureK, int32_t tint, int64_t requestId);

}  // namespace rawrcam::camera
