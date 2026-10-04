package com.rawr.camera.renderer

import com.rawr.camera.settings.architecture.*
import com.rawr.camera.settings.model.*
import kotlin.test.*

class RendererEditorSessionTest {
    private val defaults = SettingsCatalog.initialState().values

    @Test fun stagedDevelopEditsWaitForApplyWhileToneRemainsLive() {
        val pushed = mutableListOf<SettingsValues>()
        val session = RendererEditorSession(defaults, pushed::add)
        session.controller.value.dispatch(SetPhotoLensShadingEnabled(!defaults.photoLensShadingEnabled))
        assertTrue(session.dirty.value)
        assertTrue(pushed.isEmpty())
        session.controller.value.dispatch(SetNumericValue(ImageToneNumericParameter.Contrast, 20f))
        assertEquals(20f, pushed.last().activeImageTone().contrast)
        assertEquals(defaults.photoLensShadingEnabled, pushed.last().photoLensShadingEnabled)
        session.apply()
        assertFalse(session.dirty.value)
        assertEquals(!defaults.photoLensShadingEnabled, pushed.last().photoLensShadingEnabled)
    }

    @Test fun discardKeepsAppliedToneAndDropsStagedDevelopChanges() {
        val session = RendererEditorSession(defaults) {}
        session.controller.value.dispatch(SetPhotoLensShadingEnabled(!defaults.photoLensShadingEnabled))
        session.controller.value.dispatch(SetNumericValue(ImageToneNumericParameter.Contrast, 17f))
        session.discard()
        assertFalse(session.dirty.value)
        assertEquals(defaults.photoLensShadingEnabled, session.controller.value.state.value.values.photoLensShadingEnabled)
        assertEquals(17f, session.controller.value.state.value.values.activeImageTone().contrast)
        assertEquals(SettingsDestination.Section(SettingsSection.ImageTone), session.controller.value.state.value.presentation.destination)
    }

    @Test fun detailCropRespectsExifTransposeAndMirroring() {
        val viewport = RendererViewport(zoom = 2f, offsetX = 20f, offsetY = -10f, width = 100, height = 100)
        val base = detailRegion(1, viewport)
        assertContentEquals(floatArrayOf(0.15f, 0.3f, 0.5f, 0.5f), base)
        assertContentEquals(floatArrayOf(base[1], 1 - base[0] - base[2], base[2], base[3]), detailRegion(6, viewport))
        assertContentEquals(floatArrayOf(1 - base[0] - base[2], base[1], base[2], base[3]), detailRegion(2, viewport))
    }
}
