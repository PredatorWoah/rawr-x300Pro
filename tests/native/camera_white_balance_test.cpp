#include <cassert>
#include <cmath>
#include <iostream>
#include <limits>
#include <string>

#include "camera/CameraControlSerialization.h"
#include "camera/CameraWhiteBalanceControls.h"
#include "camera/WhiteBalanceMath.h"
#include "color/WbDisplayEstimate.h"

using namespace rawrcam::camera;

CameraControlState autoState() {
    CameraControlState state;
    state.capabilities.manualGainsSupported = true;
    state.capabilities.supportedAwbModes = {1, 5};
    state.lastHalAwbGains = whiteBalanceGainsForTempTint(3200, 10);
    state.hasLastHalAwbGains = true;
    return state;
}

void gainMath() {
    for (const auto point : {std::array<int32_t, 2>{5600, 0}, {3200, 10}, {4200, -5}}) {
        const auto gains = whiteBalanceGainsForTempTint(point[0], point[1]);
        const auto inverse = whiteBalanceTempTintForGains(gains[0], gains[1], gains[3]);
        const auto roundTrip = whiteBalanceGainsForTempTint(inverse[0], inverse[1]);
        for (size_t i = 0; i < gains.size(); ++i) assert(std::abs(gains[i] - roundTrip[i]) < 0.02f);
    }
    for (int temperature : {2000, 3200, 5600, 10000}) {
        const auto positive = whiteBalanceGainsForTempTint(temperature, 50);
        const auto negative = whiteBalanceGainsForTempTint(temperature, -50);
        float shift = 0;
        for (size_t i = 0; i < positive.size(); ++i) {
            assert(positive[i] >= 1 && positive[i] <= 4);
            assert(negative[i] >= 1 && negative[i] <= 4);
            shift += std::abs(positive[i] - negative[i]);
        }
        assert(shift > 0.2f);
    }
    assert(whiteBalanceGainsForTempTint(20000, -100) == whiteBalanceGainsForTempTint(10000, -50));
    const auto exotic = whiteBalanceTempTintForGains(3.5f, 3.5f, 3.5f);
    assert(exotic[0] >= 2000 && exotic[0] <= 10000 && exotic[1] >= -50 && exotic[1] <= 50);
}

void entryPolicy() {
    auto state = autoState();
    const auto estimate = estimateWhiteBalance(state);
    assert(estimate && !estimate->calibrated);
    const auto gains = state.lastHalAwbGains;
    assert(requestWhiteBalanceMode(state, WhiteBalanceControlMode::ManualTempTint, 1));
    assert(state.requestedWhiteBalanceTemperatureK == estimate->temperatureK);
    assert(state.requestedWhiteBalanceTint == estimate->tint);
    assert(state.hasManualWhiteBalanceEntryGains && state.manualWhiteBalanceEntryGains == gains);
    assert(state.whiteBalanceRequestId == 1);
    const auto baseline = state.manualWhiteBalanceEntryGains;
    state.lastHalAwbGains = whiteBalanceGainsForTempTint(5600, 0);
    assert(requestWhiteBalanceTempTint(state, 4000, -12, WbBoth, 2));
    assert(state.manualWhiteBalanceEntryGains == baseline);  // manual drags never reseed
    assert(state.requestedWhiteBalanceTemperatureK == 4000 && state.requestedWhiteBalanceTint == -12);

    state = autoState();
    // Native request values need not equal the displayed AUTO values. Explicit
    // axes ensure a temp gesture cannot accidentally claim the untouched tint.
    state.requestedWhiteBalanceTint = -40;
    assert(requestWhiteBalanceTempTint(state, 4000, 2, WbTemperature, 3));
    assert(state.requestedWhiteBalanceTemperatureK == 4000 && state.requestedWhiteBalanceTint == estimate->tint);
    state = autoState();
    assert(requestWhiteBalanceTempTint(state, 5200, -12, WbTint, 4));
    assert(state.requestedWhiteBalanceTemperatureK == estimate->temperatureK && state.requestedWhiteBalanceTint == -12);
    state = autoState();
    assert(requestWhiteBalanceTempTint(state, 5200, 0, WbSeedBoth, 5));
    assert(state.requestedWhiteBalanceTemperatureK == estimate->temperatureK &&
           state.requestedWhiteBalanceTint == estimate->tint);

    state = autoState();
    assert(requestWhiteBalanceLocked(state, 5475, 3, 6));
    assert(state.requestedWhiteBalanceTemperatureK == 5475 && state.requestedWhiteBalanceTint == 3);
    assert(state.manualWhiteBalanceEntryGains == gains);
    assert(state.manualWhiteBalanceEntryTempK == estimate->temperatureK);
}

void missingAndRejected() {
    CameraControlState state;
    state.capabilities.manualGainsSupported = true;
    assert(!estimateWhiteBalance(state));
    assert(requestWhiteBalanceTempTint(state, 20000, -100, WbBoth, 1));
    assert(state.requestedWhiteBalanceTemperatureK == 10000 && state.requestedWhiteBalanceTint == -50);
    assert(!state.hasManualWhiteBalanceEntryGains);
    assert(requestWhiteBalanceLocked(state, 4200, -5, 2));
    assert(state.manualWhiteBalanceEntryGains == whiteBalanceGainsForTempTint(4200, -5));
    assert(state.manualWhiteBalanceEntryTempK == 4200 && state.manualWhiteBalanceEntryTint == -5);
    state = autoState();
    state.lastHalAwbGains[2] = std::numeric_limits<float>::quiet_NaN();
    assert(!estimateWhiteBalance(state));
    state = autoState();
    state.capabilities.manualGainsSupported = false;
    assert(!requestWhiteBalanceMode(state, WhiteBalanceControlMode::ManualTempTint, 10));
    assert(!requestWhiteBalanceTempTint(state, 4200, 2, WbBoth, 11));
    assert(!requestWhiteBalanceLocked(state, 4200, 2, 12));
    assert(!requestWhiteBalanceMode(state, WhiteBalanceControlMode::Shade, 13));
    assert(state.whiteBalanceMode == WhiteBalanceControlMode::Auto && state.whiteBalanceRequestId == 13);
    assert(requestWhiteBalanceMode(state, WhiteBalanceControlMode::Daylight, 14));
    assert(state.whiteBalanceRequestId == 14);
    state.capabilities.manualGainsSupported = true;
    assert(!requestWhiteBalanceTempTint(state, 4200, 2, 4, 15));
    assert(state.whiteBalanceMode == WhiteBalanceControlMode::Daylight && state.whiteBalanceRequestId == 15);
}

void calibratedEstimate() {
    auto state = autoState();
    auto& calibration = state.capabilities.wbDisplayCalibration;
    calibration.referenceIlluminant1 = 17;  // standard A
    calibration.referenceIlluminant2 = 21;  // D65
    for (auto* matrix :
         {&calibration.colorTransform1, &calibration.colorTransform2, &calibration.calibrationTransform1,
          &calibration.calibrationTransform2, &calibration.forwardMatrix1, &calibration.forwardMatrix2}) {
        matrix->valid = true;
        matrix->rowMajor = {1, 0, 0, 0, 1, 0, 0, 0, 1};
    }
    state.hasLastHalNeutral = true;
    state.lastHalNeutral = {0.95f, 1, 1.09f};
    const auto calibrated = rawrcam::color::estimateWbDisplayTempTint(state.lastHalNeutral, calibration);
    assert(calibrated);
    const auto estimate = estimateWhiteBalance(state);
    assert(estimate && estimate->calibrated);
    assert(estimate->temperatureK == (*calibrated)[0] && estimate->tint == (*calibrated)[1]);
    publishWhiteBalanceEstimate(state);
    assert(state.hasAutoWbEstimate && state.autoWbEstimateCalibrated);
    assert(requestWhiteBalanceMode(state, WhiteBalanceControlMode::ManualTempTint, 1));
    assert(state.manualWhiteBalanceEntryTempK == state.autoWbTemperatureK);
    assert(state.manualWhiteBalanceEntryTint == state.autoWbTint);
    state.capabilities.wbDisplayCalibration = {};  // incomplete context falls back
    publishWhiteBalanceEstimate(state);
    assert(state.hasAutoWbEstimate && !state.autoWbEstimateCalibrated);
    state.hasLastHalAwbGains = false;
    publishWhiteBalanceEstimate(state);
    assert(!state.hasAutoWbEstimate && !state.autoWbEstimateCalibrated);
}

int main(int argc, char** argv) {
    if (argc > 1 && std::string(argv[1]) == "--snapshot") {
        auto state = autoState();
        state.capabilities.cameraId = "0";
        state.capabilities.lensId = "main";
        state.lastHalAwbGains = {1, 1, 1, 1};
        publishWhiteBalanceEstimate(state);
        requestWhiteBalanceMode(state, WhiteBalanceControlMode::ManualTempTint, 42);
        std::cout << serializeCameraControlState(state) << '\n';
        return 0;
    }
    gainMath();
    entryPolicy();
    missingAndRejected();
    calibratedEstimate();
    std::cout << "CAMERA_WHITE_BALANCE_PASS\n";
}
