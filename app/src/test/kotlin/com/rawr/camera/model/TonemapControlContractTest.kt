package com.rawr.camera.model

import kotlin.math.abs
import org.junit.Assert.assertEquals
import org.junit.Assert.assertTrue
import org.junit.Test

class TonemapControlContractTest {
    @Test fun quickShadowEndpointsNeutralAndRoundTrip() {
        assertEquals(-100f, TonemapControlContract.shadowFromQuickControl(-100), 0f)
        assertEquals(0f, TonemapControlContract.shadowFromQuickControl(0), 0f)
        assertEquals(100f, TonemapControlContract.shadowFromQuickControl(100), 0f)
        for (ui in -100..100) {
            val roundTrip =
                TonemapControlContract.shadowToQuickControl(
                    TonemapControlContract.shadowFromQuickControl(ui)
                )
            assertTrue(abs(roundTrip - ui) <= 1)
        }
    }

    @Test fun quickHighlightEndpointsNeutralAndRoundTrip() {
        assertEquals(-100f, TonemapControlContract.highlightFromQuickControl(-100), 0f)
        assertEquals(0f, TonemapControlContract.highlightFromQuickControl(0), 0f)
        assertEquals(100f, TonemapControlContract.highlightFromQuickControl(100), 0f)
        for (ui in -100..100) {
            val roundTrip =
                TonemapControlContract.highlightToQuickControl(
                    TonemapControlContract.highlightFromQuickControl(ui)
                )
            assertTrue(abs(roundTrip - ui) <= 1)
        }
    }

    @Test fun settingsRangesMatchPhotographicUiContract() {
        assertEquals(-5f, TonemapControlContract.EXPOSURE_MIN_EV, 0f)
        assertEquals(5f, TonemapControlContract.EXPOSURE_MAX_EV, 0f)
        assertEquals(-100f, TonemapControlContract.TONE_UI_MIN, 0f)
        assertEquals(100f, TonemapControlContract.TONE_UI_MAX, 0f)
        listOf(
            TonemapControlContract.blackPointNativeFromUi(-100f),
            TonemapControlContract.shadowNativeFromUi(-100f),
            TonemapControlContract.midtoneNativeFromUi(-100f),
            TonemapControlContract.contrastNativeFromUi(-100f),
            TonemapControlContract.highlightNativeFromUi(-100f),
            TonemapControlContract.whitePointNativeFromUi(-100f)
        ).forEach { assertEquals(-100f, it, 0f) }
        listOf(
            TonemapControlContract.blackPointNativeFromUi(100f),
            TonemapControlContract.shadowNativeFromUi(100f),
            TonemapControlContract.midtoneNativeFromUi(100f),
            TonemapControlContract.contrastNativeFromUi(100f),
            TonemapControlContract.highlightNativeFromUi(100f),
            TonemapControlContract.whitePointNativeFromUi(100f)
        ).forEach { assertEquals(100f, it, 0f) }
    }
}
