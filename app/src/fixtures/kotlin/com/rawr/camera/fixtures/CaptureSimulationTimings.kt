package com.rawr.camera.fixtures

/** Timing policy for the in-memory capture simulation used by tests. */
data class CaptureSimulationTimings(
    val settleDelayMs: Long = 650L,
    val captureFlashMs: Long = 90L,
    val captureProcessingBaseMs: Long = 1_400L,
    val captureProcessingStaggerMs: Long = 300L
) {
    init {
        require(settleDelayMs >= 0)
        require(captureFlashMs >= 0)
        require(captureProcessingBaseMs >= 0)
        require(captureProcessingStaggerMs >= 0)
    }
}
