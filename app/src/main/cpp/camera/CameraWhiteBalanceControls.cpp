#include "camera/CameraWhiteBalanceControls.h"

#include <algorithm>
#include <cmath>

#include "camera/WhiteBalanceMath.h"
#include "color/WbDisplayEstimate.h"

namespace rawrcam::camera {
namespace {

bool validGains(const CameraControlState& state) {
    return state.hasLastHalAwbGains && std::all_of(state.lastHalAwbGains.begin(), state.lastHalAwbGains.end(),
                                                   [](float value) { return std::isfinite(value); });
}

std::optional<WhiteBalanceEstimate> captureEntry(CameraControlState& state) {
    state.hasManualWhiteBalanceEntryGains = false;
    if (!validGains(state)) return std::nullopt;
    const auto estimate = estimateWhiteBalance(state);
    if (!estimate) return std::nullopt;
    state.manualWhiteBalanceEntryGains = state.lastHalAwbGains;
    state.hasManualWhiteBalanceEntryGains = true;
    state.manualWhiteBalanceEntryTempK = estimate->temperatureK;
    state.manualWhiteBalanceEntryTint = estimate->tint;
    return estimate;
}

}  // namespace

std::optional<WhiteBalanceEstimate> estimateWhiteBalance(const CameraControlState& state) {
    if (state.hasLastHalNeutral) {
        if (const auto estimate =
                color::estimateWbDisplayTempTint(state.lastHalNeutral, state.capabilities.wbDisplayCalibration)) {
            return WhiteBalanceEstimate{(*estimate)[0], (*estimate)[1], true};
        }
    }
    if (!validGains(state)) return std::nullopt;
    const auto& gains = state.lastHalAwbGains;
    const auto estimate = whiteBalanceTempTintForGains(gains[0], 0.5f * (gains[1] + gains[2]), gains[3]);
    return WhiteBalanceEstimate{estimate[0], estimate[1], false};
}

void observeWhiteBalanceResult(CameraControlState& state, const metadata::FrameMetadataSnapshot& frame) {
    // Seed the manual-gains color transform from live HAL results so
    // AUTO->MANUAL keeps the current rendering and only moves white.
    // Frozen while in manual: the result then echoes our own
    // TRANSFORM_MATRIX seed, and re-seeding from it would feed the
    // manual rendering back into the baseline and drift.
    const bool inManualWb = state.whiteBalanceMode == WhiteBalanceControlMode::ManualTempTint;
    if (!inManualWb && frame.colorCorrectionTransform.valid) {
        state.manualWhiteBalanceTransformSeed = frame.colorCorrectionTransform.rowMajor;
        state.hasManualWhiteBalanceTransformSeed = true;
    }
    // Seed AUTO->MANUAL temp/tint from live HAL AWB gains. Cheap 4-float
    // copy per frame only; inversion runs once on manual entry, never
    // here. Skip while in manual so our own echoed gains don't overwrite
    // the auto seed. frame.awbMode==0 means AWB OFF (manual).
    if (!inManualWb && frame.awbMode != 0) {
        const auto& g = frame.colorCorrectionGainsRggb;
        if (g[0] > 0.0f && g[1] > 0.0f && g[2] > 0.0f && g[3] > 0.0f && std::isfinite(g[0]) &&
            std::isfinite(g[1]) && std::isfinite(g[2]) && std::isfinite(g[3])) {
            state.lastHalAwbGains = g;
            state.hasLastHalAwbGains = true;
        }
        // Neutral for the calibrated display estimate (CCT through the
        // device calibration). Same freeze-in-manual policy as the
        // gains: our own echoed rendering must not overwrite the seed.
        if (frame.hasNeutralColorPoint) {
            const auto& n = frame.neutralColorPoint;
            if (n[0] > 0.0f && n[1] > 0.0f && n[2] > 0.0f && std::isfinite(n[0]) &&
                std::isfinite(n[1]) && std::isfinite(n[2])) {
                state.lastHalNeutral = n;
                state.hasLastHalNeutral = true;
            }
        }
    }
}

void publishWhiteBalanceEstimate(CameraControlState& snapshot) {
    const auto estimate = estimateWhiteBalance(snapshot);
    snapshot.hasAutoWbEstimate = estimate.has_value();
    snapshot.autoWbEstimateCalibrated = estimate && estimate->calibrated;
    snapshot.autoWbTemperatureK = estimate ? estimate->temperatureK : 5200;
    snapshot.autoWbTint = estimate ? estimate->tint : 0;
}

bool requestWhiteBalanceMode(CameraControlState& state, WhiteBalanceControlMode mode, int64_t requestId) {
    state.whiteBalanceRequestId = std::max(state.whiteBalanceRequestId, requestId);
    if (mode == WhiteBalanceControlMode::ManualTempTint) {
        if (!state.capabilities.manualGainsSupported) return false;
        if (state.whiteBalanceMode != mode) {
            if (const auto estimate = captureEntry(state)) {
                state.requestedWhiteBalanceTemperatureK = estimate->temperatureK;
                state.requestedWhiteBalanceTint = estimate->tint;
            }
        }
    } else if (mode != WhiteBalanceControlMode::Auto) {
        const auto& modes = state.capabilities.supportedAwbModes;
        if (std::find(modes.begin(), modes.end(), static_cast<uint8_t>(mode)) == modes.end()) return false;
    }
    state.whiteBalanceMode = mode;
    return true;
}

bool requestWhiteBalanceTempTint(CameraControlState& state, int32_t temperatureK, int32_t tint, int32_t editedAxes,
                                 int64_t requestId) {
    state.whiteBalanceRequestId = std::max(state.whiteBalanceRequestId, requestId);
    if (!state.capabilities.manualGainsSupported || editedAxes < WbSeedBoth || editedAxes > WbBoth) return false;
    auto requestedTemp = std::clamp(temperatureK, 2000, 10000);
    auto requestedTint = std::clamp(tint, -50, 50);
    if (state.whiteBalanceMode != WhiteBalanceControlMode::ManualTempTint) {
        if (const auto estimate = captureEntry(state)) {
            if (!(editedAxes & WbTemperature)) requestedTemp = estimate->temperatureK;
            if (!(editedAxes & WbTint)) requestedTint = estimate->tint;
        }
    }
    state.requestedWhiteBalanceTemperatureK = requestedTemp;
    state.requestedWhiteBalanceTint = requestedTint;
    state.whiteBalanceMode = WhiteBalanceControlMode::ManualTempTint;
    return true;
}

bool requestWhiteBalanceLocked(CameraControlState& state, int32_t temperatureK, int32_t tint, int64_t requestId) {
    state.whiteBalanceRequestId = std::max(state.whiteBalanceRequestId, requestId);
    if (!state.capabilities.manualGainsSupported) return false;
    const auto requestedTemp = std::clamp(temperatureK, 2000, 10000);
    const auto requestedTint = std::clamp(tint, -50, 50);
    state.requestedWhiteBalanceTemperatureK = requestedTemp;
    state.requestedWhiteBalanceTint = requestedTint;
    if (!captureEntry(state)) {
        state.manualWhiteBalanceEntryGains = whiteBalanceGainsForTempTint(requestedTemp, requestedTint);
        state.manualWhiteBalanceEntryTempK = requestedTemp;
        state.manualWhiteBalanceEntryTint = requestedTint;
        state.hasManualWhiteBalanceEntryGains = true;
    }
    state.whiteBalanceMode = WhiteBalanceControlMode::ManualTempTint;
    return true;
}

}  // namespace rawrcam::camera
