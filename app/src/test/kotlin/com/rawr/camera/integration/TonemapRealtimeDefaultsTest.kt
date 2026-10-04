package com.rawr.camera.integration

import com.rawr.camera.model.TonemapControlContract
import com.rawr.camera.settings.fixtures.SettingsFixtures
import org.junit.Assert.assertEquals
import org.junit.Test

class TonemapRealtimeDefaultsTest {
    @Test
    fun defaultsMatchNeutralEngineContract() {
        val tone = SettingsFixtures.initialState().values.imageTone
        assertEquals(TonemapControlContract.EXPOSURE_NEUTRAL_EV, tone.renderExposure, 0f)
        assertEquals(TonemapControlContract.TONE_UI_NEUTRAL, tone.blacks, 0f)
        assertEquals(TonemapControlContract.TONE_UI_NEUTRAL, tone.shadows, 0f)
        assertEquals(TonemapControlContract.TONE_UI_NEUTRAL, tone.contrast, 0f)
        assertEquals(TonemapControlContract.TONE_UI_NEUTRAL, tone.highlights, 0f)
        assertEquals(TonemapControlContract.TONE_UI_NEUTRAL, tone.whites, 0f)
        assertEquals(TonemapControlContract.SATURATION_NEUTRAL, tone.saturation, 0f)
        assertEquals(TonemapControlContract.VIBRANCE_NEUTRAL, tone.vibrance, 0f)
    }
}
