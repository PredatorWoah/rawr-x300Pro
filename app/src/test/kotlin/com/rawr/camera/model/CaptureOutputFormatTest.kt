package com.rawr.camera.model

import com.rawr.camera.architecture.CaptureReducer
import com.rawr.camera.architecture.CycleOutputFormat
import com.rawr.camera.fixtures.CaptureFixtures
import kotlin.test.*

class CaptureOutputFormatTest {
    @Test fun cycleAlwaysSavesAnOutputAndReturnsToDefault() {
        var state = CaptureUiState(CaptureFixtures.baseline())
        for (expected in listOf(CaptureOutputFormat.Jpeg, CaptureOutputFormat.Dng, CaptureOutputFormat.DngAndJpeg)) {
            state = CaptureReducer.reduce(state, CycleOutputFormat)
            assertEquals(expected, CaptureOutputFormat.from(state.dngEnabled, state.jpegEnabled))
            assertTrue(state.dngEnabled || state.jpegEnabled)
        }
    }
    @Test fun legacyDefaultsAndInvalidEmptyChoicePreserveDng() {
        assertEquals(CaptureOutputFormat.DngAndJpeg, CaptureOutputFormat.from(true, true))
        assertEquals(CaptureOutputFormat.Dng, CaptureOutputFormat.from(true, false))
        assertEquals(CaptureOutputFormat.Dng, CaptureOutputFormat.from(false, false))
    }
}
