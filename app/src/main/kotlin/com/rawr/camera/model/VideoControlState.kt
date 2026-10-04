package com.rawr.camera.model

/** Presentation of the recording controls, separate from recorder lifecycle ownership. */
data class VideoControlState(
    val resolution: String = "1080p",
    val fps: Int = 30,
    val recording: Boolean = false,
    val busy: Boolean = false,
    val status: String = "REC",
    val timing: VideoTimingState? = null
)

data class VideoTimingState(
    val actualFps: Double,
    val dropped: Long,
    val shortfall: Long,
    val gpuMs: Double,
    val encoderMs: Double,
    val micDbfs: Double,
    val dropReason: String? = null
)
