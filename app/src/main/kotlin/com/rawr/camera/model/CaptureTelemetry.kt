package com.rawr.camera.model

/** Only values displayed in the compact monitor; this stream is collected inside the monitor slot. */
data class CaptureMonitorState(
    val exposureMode: ExposureMode,
    val whiteBalanceTemperatureK: Int,
    val whiteBalanceTint: Int,
    val exposureApplied: ExposureAppliedState,
    val focusDiopters: Float?,
    val rawFps: Double?,
    val viewfinderFps: Double?,
    val sensitivityBoost: Int?,
    /** Compact controls already show WB, SS, ISO and EV under the preview, so the monitor must not repeat them. */
    val compactLayout: Boolean = false,
    val videoMode: Boolean = false
)

fun CaptureUiState.monitorProjection() = CaptureMonitorState(
    exposureControl.mode, whiteBalanceTemperatureK, whiteBalanceTint, exposureApplied,
    focus.appliedFocusDiopters, rawFps, viewfinderFps, sensitivityBoost,
    compactLayout = captureLayout == CaptureControlLayout.Compact,
    videoMode = captureMode == CaptureMode.Video
)

/** Excludes observation fields that have dedicated render slots. Applied control seeds are retained. */
fun CaptureUiState.controlProjection(): CaptureUiState = copy(
    rawFps = null,
    viewfinderFps = null,
    sensitivityBoost = null,
    faceDetections = emptyList()
)
