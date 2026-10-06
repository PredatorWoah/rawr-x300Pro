package com.rawr.camera.model

import kotlin.test.Test
import kotlin.test.assertEquals
import kotlin.test.assertFalse
import kotlin.test.assertTrue

class CaptureControlLayoutTest {
    @Test
    fun compactAndSimpleShareTheButtonStripGeometry() {
        assertTrue(CaptureControlLayout.Compact.usesButtonStrip)
        assertTrue(CaptureControlLayout.Simple.usesButtonStrip)
        assertTrue(CaptureControlLayout.Pro.usesButtonStrip)
        assertFalse(CaptureControlLayout.Classic.usesButtonStrip)
    }

    @Test
    fun storedLayoutNamesStillResolve() {
        // Layouts persist by name; adding Simple must not disturb the two that already exist.
        assertEquals(CaptureControlLayout.Classic, CaptureControlLayout.valueOf("Classic"))
        assertEquals(CaptureControlLayout.Compact, CaptureControlLayout.valueOf("Compact"))
        assertEquals(CaptureControlLayout.Simple, CaptureControlLayout.valueOf("Simple"))
        assertEquals(CaptureControlLayout.Pro, CaptureControlLayout.valueOf("Pro"))
    }
}
