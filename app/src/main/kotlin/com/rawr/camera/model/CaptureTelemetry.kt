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
    val sensitivityBoost: Int?
)

fun CaptureUiState.monitorProjection() = CaptureMonitorState(
    exposureControl.mode, whiteBalanceTemperatureK, whiteBalanceTint, exposureApplied,
    focus.appliedFocusDiopters, rawFps, viewfinderFps, sensitivityBoost
)

/** Excludes observation fields that have dedicated render slots. Applied control seeds are retained. */
fun CaptureUiState.controlProjection(): CaptureUiState = copy(
    rawFps = null,
    viewfinderFps = null,
    sensitivityBoost = null,
    faceDetections = emptyList()
)
