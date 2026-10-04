package com.rawr.camera.model

import com.rawr.camera.architecture.CaptureReducer
import com.rawr.camera.architecture.CaptureTransitions
import com.rawr.camera.architecture.ToggleWhiteBalancePanel
import com.rawr.camera.fixtures.CaptureFixtures
import kotlin.test.Test
import kotlin.test.assertEquals
import kotlin.test.assertFalse
import kotlin.test.assertTrue

class WhiteBalanceModelTest {
    private val capabilities = CaptureFixtures.baseline()
    private fun state() = CaptureUiState(capabilities = capabilities)

    @Test
    fun presetRequestIsGatedOnCapabilities() {
        val withoutShade =
            capabilities.copy(supportedWhiteBalanceModes = capabilities.supportedWhiteBalanceModes - WhiteBalanceMode.Shade)
        val unchanged = CaptureTransitions.requestWhiteBalanceMode(state().copy(capabilities = withoutShade), WhiteBalanceMode.Shade)
        assertEquals(WhiteBalanceMode.Auto, unchanged.whiteBalanceMode)

        val changed = CaptureTransitions.requestWhiteBalanceMode(state(), WhiteBalanceMode.Shade)
        assertEquals(WhiteBalanceMode.Shade, changed.whiteBalanceMode)
    }

    @Test
    fun manualRequestRequiresManualSupport() {
        val withoutManual = capabilities.copy(manualWhiteBalanceSupported = false)
        val rejected =
            CaptureTransitions.requestWhiteBalanceMode(
                state().copy(capabilities = withoutManual),
                WhiteBalanceMode.ManualTempTint
            )
        assertEquals(WhiteBalanceMode.Auto, rejected.whiteBalanceMode)

        val accepted = CaptureTransitions.requestWhiteBalanceMode(state(), WhiteBalanceMode.ManualTempTint)
        assertEquals(WhiteBalanceMode.ManualTempTint, accepted.whiteBalanceMode)
    }

    @Test
    fun tempTintSlidersClampAndEnterManual() {
        val next = CaptureTransitions.setWhiteBalanceTempTint(state(), 20000, -100)
        assertEquals(WhiteBalanceMode.ManualTempTint, next.whiteBalanceMode)
        assertEquals(WhiteBalanceMode.TEMP_MAX_K, next.whiteBalanceTemperatureK)
        assertEquals(WhiteBalanceMode.TINT_MIN, next.whiteBalanceTint)

        val withoutManual = capabilities.copy(manualWhiteBalanceSupported = false)
        val rejected =
            CaptureTransitions.setWhiteBalanceTempTint(
                state().copy(capabilities = withoutManual),
                3200,
                10
            )
        assertEquals(WhiteBalanceMode.Auto, rejected.whiteBalanceMode)
        assertEquals(WhiteBalanceMode.TEMP_DEFAULT_K, rejected.whiteBalanceTemperatureK)
    }

    @Test
    fun capabilitiesReconcileFallsBackToAutoButKeepsManualSliders() {
        val manual =
            state().copy(
                whiteBalanceMode = WhiteBalanceMode.ManualTempTint,
                whiteBalanceTemperatureK = 3200,
                whiteBalanceTint = 12
            )
        val supported = CaptureTransitions.applyCameraCapabilities(manual, capabilities)
        assertEquals(WhiteBalanceMode.ManualTempTint, supported.whiteBalanceMode)
        assertEquals(3200, supported.whiteBalanceTemperatureK)

        val replacement = capabilities.copy(manualWhiteBalanceSupported = false)
        val fallenBack = CaptureTransitions.applyCameraCapabilities(manual, replacement)
        assertEquals(WhiteBalanceMode.Auto, fallenBack.whiteBalanceMode)
        assertEquals(3200, fallenBack.whiteBalanceTemperatureK)
        assertEquals(12, fallenBack.whiteBalanceTint)
    }

    @Test
    fun whiteBalancePanelToggleIsMutuallyExclusive() {
        val open = CaptureReducer.reduce(state(), ToggleWhiteBalancePanel)
        assertTrue(open.whiteBalancePanelOpen)
        assertFalse(open.monitorPanelOpen)

        val closed = CaptureReducer.reduce(open, ToggleWhiteBalancePanel)
        assertFalse(closed.whiteBalancePanelOpen)
    }

    @Test
    fun awbValueRoundTrip() {
        assertEquals(WhiteBalanceMode.Daylight, WhiteBalanceMode.fromAwbValue(5))
        assertEquals(WhiteBalanceMode.Auto, WhiteBalanceMode.fromAwbValue(0))
        assertEquals(WhiteBalanceMode.Auto, WhiteBalanceMode.fromAwbValue(99))
    }

    @Test
    fun manualButtonSeedsBothAxesFromNativeEstimate() {
        val auto = state().copy(autoWhiteBalanceTemperatureK = 3300, autoWhiteBalanceTint = 7)
        val next = CaptureTransitions.setWhiteBalanceTempTint(auto, auto.whiteBalanceTemperatureK, auto.whiteBalanceTint)
        assertEquals(WhiteBalanceMode.ManualTempTint, next.whiteBalanceMode)
        assertEquals(3300, next.whiteBalanceTemperatureK)
        assertEquals(7, next.whiteBalanceTint)
    }

    @Test
    fun dragKeepsEditedAxisAndUsesNativeEstimateForOtherAxis() {
        val auto = state().copy(autoWhiteBalanceTemperatureK = 3300, autoWhiteBalanceTint = 7)
        val temp = CaptureTransitions.setWhiteBalanceTempTint(auto, 4000, auto.whiteBalanceTint)
        assertEquals(4000, temp.whiteBalanceTemperatureK)
        assertEquals(7, temp.whiteBalanceTint)
        val tint = CaptureTransitions.setWhiteBalanceTempTint(auto, auto.whiteBalanceTemperatureK, -12)
        assertEquals(3300, tint.whiteBalanceTemperatureK)
        assertEquals(-12, tint.whiteBalanceTint)
    }

    @Test
    fun calibratedDisplayQuantizesAndHoldsSmallTintChanges() {
        val followed = CaptureTransitions.followWhiteBalanceAutoDisplay(state(), 8037, 4, calibrated = true)
        assertEquals(8050, followed.whiteBalanceTemperatureK)
        assertEquals(4, followed.whiteBalanceTint)
        val heldTint = CaptureTransitions.followWhiteBalanceAutoDisplay(followed, 8080, 5, calibrated = true)
        assertEquals(8100, heldTint.whiteBalanceTemperatureK)
        assertEquals(4, heldTint.whiteBalanceTint)
        val tracked = CaptureTransitions.followWhiteBalanceAutoDisplay(heldTint, 8080, 8, calibrated = true)
        assertEquals(8, tracked.whiteBalanceTint)
    }

    @Test
    fun fallbackDisplayFollowsNativeGridWithoutAdditionalRounding() {
        val followed = CaptureTransitions.followWhiteBalanceAutoDisplay(state(), 5600, 3)
        assertEquals(5600, followed.whiteBalanceTemperatureK)
        assertEquals(3, followed.whiteBalanceTint)
        assertEquals(followed, CaptureTransitions.followWhiteBalanceAutoDisplay(followed, 5600, 3))
        val tracked = CaptureTransitions.followWhiteBalanceAutoDisplay(followed, 5700, 4)
        assertEquals(5700, tracked.whiteBalanceTemperatureK)
        assertEquals(4, tracked.whiteBalanceTint)
        assertEquals(followed, CaptureTransitions.followWhiteBalanceAutoDisplay(followed, null, null))
        val manual = followed.copy(whiteBalanceMode = WhiteBalanceMode.ManualTempTint)
        assertEquals(manual, CaptureTransitions.followWhiteBalanceAutoDisplay(manual, 9000, -12))
    }

    @Test
    fun modeSwitchUsesNativeNeutralOrRequestedFallback() {
        val auto = state().copy(autoWhiteBalanceTemperatureK = 3300, autoWhiteBalanceTint = 7)
        val snapped = CaptureTransitions.snapWhiteBalanceDisplayToMode(auto, WhiteBalanceMode.Incandescent, 5200, 0)
        assertEquals(WhiteBalanceMode.Incandescent, snapped.whiteBalanceMode)
        assertEquals(3300, snapped.whiteBalanceTemperatureK)
        assertEquals(7, snapped.whiteBalanceTint)
        val fallback = CaptureTransitions.snapWhiteBalanceDisplayToMode(state(), WhiteBalanceMode.Shade, 7000, -7)
        assertEquals(7000, fallback.whiteBalanceTemperatureK)
        assertEquals(-7, fallback.whiteBalanceTint)
    }

    @Test
    fun capabilitiesReconcileClearsNativeEstimate() {
        val seeded = state().copy(autoWhiteBalanceTemperatureK = 4200, autoWhiteBalanceTint = -5)
        val reconciled = CaptureTransitions.applyCameraCapabilities(seeded, capabilities)
        assertEquals(null, reconciled.autoWhiteBalanceTemperatureK)
        assertEquals(null, reconciled.autoWhiteBalanceTint)
    }
}
