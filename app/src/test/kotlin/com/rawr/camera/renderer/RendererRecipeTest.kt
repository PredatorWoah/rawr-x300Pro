package com.rawr.camera.renderer

import com.rawr.camera.settings.model.*
import kotlin.test.*

class RendererRecipeTest {
    @Test fun srgbPhotoRecipeRetainsItsProfileAndTone() {
        val values = SettingsCatalog.initialState().values.copy(
            colorRenderProfile = ColorRenderProfile.SRgb,
            srgbTone = ProfileTone(contrast = 21f, renderExposure = -.5f))
        val decoded = RendererRecipe.decode(RendererRecipe.encode(values), values)
        assertEquals(ColorRenderProfile.SRgb, decoded.colorRenderProfile)
        assertEquals(values.srgbTone, decoded.activeProfileTone())
        assertEquals("photo", decoded.captureModeId)
    }
}
