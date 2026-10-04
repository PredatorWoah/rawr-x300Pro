package com.rawr.camera.integration

import com.rawr.camera.model.OverlayMode
import com.rawr.camera.model.TargetStatus
import kotlin.test.Test
import kotlin.test.assertEquals
import kotlinx.coroutines.ExperimentalCoroutinesApi
import kotlinx.coroutines.test.TestScope
import kotlinx.coroutines.test.advanceTimeBy
import kotlinx.coroutines.test.runCurrent
import kotlinx.coroutines.test.runTest

@OptIn(ExperimentalCoroutinesApi::class)
class OverlayTriggerManagerTest {
    private fun manager(
        scope: TestScope,
        masks: MutableList<Int>,
        armed: Set<OverlayMode> =
            setOf(OverlayMode.Peaking, OverlayMode.RawHighlights, OverlayMode.TonemapShadows)
    ) = OverlayTriggerManager(scope, masks::add).also {
        it.setArmed(armed)
        masks.clear()
    }

    @Test
    fun shadowPulseShowsThenHidesOnRelease() = runTest {
        val masks = mutableListOf<Int>()
        val manager = manager(this, masks)
        manager.pulseShadow()
        assertEquals(listOf(OverlayTriggerManager.BIT_SHADOW), masks)
        advanceTimeBy(OverlayTriggerManager.INACTIVITY_MS + 50)
        runCurrent()
        assertEquals(listOf(OverlayTriggerManager.BIT_SHADOW, 0), masks)
    }

    @Test
    fun unarmedOverlayNeverEmits() = runTest {
        val masks = mutableListOf<Int>()
        val manager = manager(this, masks, armed = setOf(OverlayMode.Peaking))
        manager.pulseShadow()
        manager.pulseRawHighlight()
        advanceTimeBy(1000)
        runCurrent()
        assertEquals(emptyList(), masks)
    }

    @Test
    fun exposurePulseReleasesIndependently() = runTest {
        val masks = mutableListOf<Int>()
        val manager = manager(this, masks)
        manager.pulseRawHighlight()
        manager.pulseShadow()
        assertEquals(
            listOf(OverlayTriggerManager.BIT_RAW, OverlayTriggerManager.BIT_RAW or OverlayTriggerManager.BIT_SHADOW),
            masks
        )
        advanceTimeBy(OverlayTriggerManager.INACTIVITY_MS + 50)
        runCurrent()
        // Both inactivity timers were scheduled together; each layer clears
        // independently as its job runs.
        assertEquals(
            listOf(
                OverlayTriggerManager.BIT_RAW,
                OverlayTriggerManager.BIT_RAW or OverlayTriggerManager.BIT_SHADOW,
                OverlayTriggerManager.BIT_SHADOW,
                0
            ),
            masks
        )
    }

    @Test
    fun autofocusDwellsOneSecondAfterGreen() = runTest {
        val masks = mutableListOf<Int>()
        val manager = manager(this, masks)
        manager.autofocusStart()
        assertEquals(listOf(OverlayTriggerManager.BIT_PEAK), masks)
        manager.autofocusStatus(TargetStatus.Settling, isMf = false)
        advanceTimeBy(4000)
        runCurrent()
        assertEquals(listOf(OverlayTriggerManager.BIT_PEAK), masks)
        manager.autofocusStatus(TargetStatus.Settled, isMf = false)
        advanceTimeBy(OverlayTriggerManager.AF_DWELL_MS - 100)
        runCurrent()
        assertEquals(listOf(OverlayTriggerManager.BIT_PEAK), masks)
        advanceTimeBy(200)
        runCurrent()
        assertEquals(listOf(OverlayTriggerManager.BIT_PEAK, 0), masks)
    }

    @Test
    fun autofocusFailureHidesImmediately() = runTest {
        val masks = mutableListOf<Int>()
        val manager = manager(this, masks)
        manager.autofocusStart()
        manager.autofocusStatus(TargetStatus.Failed, isMf = false)
        runCurrent()
        assertEquals(listOf(OverlayTriggerManager.BIT_PEAK, 0), masks)
    }

    @Test
    fun autofocusCancelHidesImmediately() = runTest {
        val masks = mutableListOf<Int>()
        val manager = manager(this, masks)
        manager.autofocusStart()
        assertEquals(listOf(OverlayTriggerManager.BIT_PEAK), masks)
        manager.autofocusCancel()
        runCurrent()
        assertEquals(listOf(OverlayTriggerManager.BIT_PEAK, 0), masks)
    }

    @Test
    fun manualFocusPulseHidesOnRelease() = runTest {
        val masks = mutableListOf<Int>()
        val manager = manager(this, masks)
        manager.pulsePeakManual()
        assertEquals(listOf(OverlayTriggerManager.BIT_PEAK), masks)
        advanceTimeBy(OverlayTriggerManager.INACTIVITY_MS + 50)
        runCurrent()
        assertEquals(listOf(OverlayTriggerManager.BIT_PEAK, 0), masks)
    }

    @Test
    fun falseColorBypassesTriggers() = runTest {
        val masks = mutableListOf<Int>()
        val manager = manager(this, masks, armed = emptySet())
        manager.setFalseColor(true)
        assertEquals(listOf(OverlayTriggerManager.BIT_FALSE), masks)
    }
}
