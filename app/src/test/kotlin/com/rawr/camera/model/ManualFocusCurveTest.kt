package com.rawr.camera.model

import kotlin.math.abs
import kotlin.math.pow
import kotlin.test.Test
import kotlin.test.assertEquals
import kotlin.test.assertNotNull
import kotlin.test.assertNull
import kotlin.test.assertTrue

class ManualFocusCurveTest {
    private val minD = 10f

    // Same mapping as native ManualFocusCurve.h manualFocusDiopters.
    private fun diopters(normalized: Float) = (1f - normalized.coerceIn(0f, 1f)).pow(MF_CURVE_EXPONENT) * minD

    @Test
    fun endpointsMapToTheRailEnds() {
        assertEquals(1f, mfNormalizedFromDiopters(0f, minD)!!, 1e-4f)
        assertEquals(0f, mfNormalizedFromDiopters(minD, minD)!!, 1e-4f)
    }

    @Test
    fun roundTripsThroughTheNativeMapping() {
        var p = 0f
        while (p <= 1f) {
            assertEquals(p, mfNormalizedFromDiopters(diopters(p), minD)!!, 2e-3f)
            p += 0.05f
        }
    }

    @Test
    fun threeMetresIsNotTheLastFewPercentOfTheRail() {
        val at3m = mfNormalizedFromDiopters(1f / 3f, minD)!!
        val at30m = mfNormalizedFromDiopters(1f / 30f, minD)!!
        assertTrue(1f - at3m > 0.25f, "3 m should leave a good part of the rail for farther focus, was $at3m")
        assertTrue(at30m > at3m + 0.1f, "3 m and 30 m must be clearly different rail positions")
    }

    @Test
    fun unknownMinimumFocusDistanceHasNoRail() {
        assertNull(mfNormalizedFromDiopters(1f, 0f))
        assertNotNull(mfNormalizedFromDiopters(1f, minD))
    }

    @Test
    fun valuesBeyondTheLensRangeClamp() {
        assertEquals(0f, mfNormalizedFromDiopters(99f, minD)!!, 1e-4f)
        assertTrue(abs(mfNormalizedFromDiopters(-1f, minD)!! - 1f) < 1e-4f)
    }
}
