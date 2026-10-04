package com.rawr.camera.integration

import com.rawr.camera.architecture.CaptureTransitions
import com.rawr.camera.model.CaptureUiState
import com.rawr.camera.model.WhiteBalanceMode

/** Native acknowledgment and display projection; no gain conversion or camera policy. */
internal object NativeWhiteBalancePresentation {
    fun project(current: CaptureUiState, snapshot: NativeCameraUiSnapshot, latestRequestId: Long): CaptureUiState {
        val telemetry = current.copy(
            autoWhiteBalanceTemperatureK = snapshot.autoWbTemperatureK.takeIf { snapshot.hasAutoWbEstimate },
            autoWhiteBalanceTint = snapshot.autoWbTint.takeIf { snapshot.hasAutoWbEstimate }
        )
        // Acknowledgments include rejection, so stale echoes preserve gestures and
        // a rejected command can still restore the accepted native mode/values.
        if (snapshot.whiteBalanceRequestId < latestRequestId) return telemetry
        val mode = WhiteBalanceMode.fromAwbValue(snapshot.whiteBalanceMode)
        if (mode == WhiteBalanceMode.ManualTempTint) {
            return telemetry.copy(
                whiteBalanceMode = mode,
                whiteBalanceTemperatureK = snapshot.whiteBalanceTemperatureK
                    .coerceIn(WhiteBalanceMode.TEMP_MIN_K, WhiteBalanceMode.TEMP_MAX_K),
                whiteBalanceTint = snapshot.whiteBalanceTint.coerceIn(WhiteBalanceMode.TINT_MIN, WhiteBalanceMode.TINT_MAX)
            )
        }
        val accepted = telemetry.copy(whiteBalanceMode = mode)
        return if (snapshot.hasAutoWbEstimate) {
            CaptureTransitions.followWhiteBalanceAutoDisplay(
                accepted, snapshot.autoWbTemperatureK, snapshot.autoWbTint, snapshot.autoWbEstimateCalibrated
            )
        } else {
            CaptureTransitions.snapWhiteBalanceDisplayToMode(
                accepted, mode, snapshot.whiteBalanceTemperatureK, snapshot.whiteBalanceTint
            )
        }
    }
}
