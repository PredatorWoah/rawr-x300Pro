package com.rawr.camera.model

import com.rawr.camera.fixtures.CaptureFixtures
import kotlin.test.*

class CaptureTelemetryTest {
    @Test fun monitorAndFaceUpdatesLeaveTheControlProjectionUnchanged() {
        val state = CaptureUiState(CaptureFixtures.baseline())
        val observed = state.copy(rawFps = 29.8, viewfinderFps = 30.0, sensitivityBoost = 150,
            faceDetections = listOf(FaceDetection(.1f, .2f, .3f, .4f, 90)))
        assertEquals(state.controlProjection(), observed.controlProjection())
        assertNotEquals(state.monitorProjection(), observed.monitorProjection())
    }

    @Test fun appliedExposureSeedsRemainAvailableToAutomaticControlGestures() {
        val applied = ExposureAppliedState(AppliedExposureReadout("1/60"), AppliedExposureReadout("800"), null)
        val state = CaptureUiState(CaptureFixtures.baseline(), exposureApplied = applied)
        assertEquals(applied, state.controlProjection().exposureApplied)
        assertEquals(applied, state.monitorProjection().exposureApplied)
    }
}
