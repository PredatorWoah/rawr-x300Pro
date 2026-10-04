package com.rawr.camera.settings.architecture

import com.rawr.camera.settings.model.LensProfile
import com.rawr.camera.settings.model.SettingsDestination
import com.rawr.camera.settings.model.SettingsSection
import com.rawr.camera.settings.model.moveLens
import com.rawr.camera.settings.model.newLensName
import com.rawr.camera.settings.model.removeLens
import com.rawr.camera.settings.model.setLensEnabled
import com.rawr.camera.settings.model.upsertLens
import kotlin.test.Test
import kotlin.test.assertEquals
import kotlin.test.assertNull

class LensSettingsTest {
    private val a = LensProfile("14", "4")
    private val b = LensProfile("35", "3")
    private val c = LensProfile("85", "5")
    private val list = listOf(a, b, c)

    @Test
    fun moveReordersAndIgnoresOutOfRange() {
        assertEquals(listOf(b, c, a), list.moveLens(0, 2))
        assertEquals(listOf(c, a, b), list.moveLens(2, 0))
        assertEquals(list, list.moveLens(0, 3))
    }

    @Test
    fun lastEnabledLensStays() {
        val oneLeft = list.setLensEnabled(0, false).setLensEnabled(1, false)
        assertEquals(listOf(false, false, true), oneLeft.map { it.enabled })
        assertEquals(oneLeft, oneLeft.setLensEnabled(2, false))
        assertEquals(oneLeft, oneLeft.removeLens("85"))
        assertEquals(listOf("35", "85"), list.removeLens("14").map { it.name })
    }

    @Test
    fun upsertReplacesByOriginalNameOrAppends() {
        val renamed = list.upsertLens("35", b.copy(name = " UW "))
        assertEquals(listOf("14", "UW", "85"), renamed.map { it.name })
        assertEquals(listOf("14", "35", "85", "L1"), list.upsertLens(null, LensProfile("L1", "0")).map { it.name })
        assertEquals("L1", list.newLensName())
        assertEquals("L2", (list + LensProfile("l1", "0")).newLensName())
    }

    @Test
    fun lensEditorOpensFromLensAndBackReturnsThere() {
        val controller = PersistentSettingsController()
        controller.dispatch(OpenSection(SettingsSection.Capture))
        controller.dispatch(OpenSection(SettingsSection.Lens))
        controller.dispatch(OpenLensEditor("35"))
        assertEquals(SettingsDestination.LensEditor("35"), controller.state.value.presentation.destination)
        controller.dispatch(NavigateBack)
        assertEquals(SettingsDestination.Section(SettingsSection.Lens), controller.state.value.presentation.destination)
        controller.dispatch(NavigateBack)
        assertEquals(SettingsDestination.Section(SettingsSection.Capture), controller.state.value.presentation.destination)
    }

    @Test
    fun setLensProfilesStoresAndResets() {
        val controller = PersistentSettingsController()
        controller.dispatch(SetLensProfiles(list))
        assertEquals(list, controller.state.value.values.lensProfiles)
        controller.dispatch(SetLensProfiles(null))
        assertNull(controller.state.value.values.lensProfiles)
    }
}
