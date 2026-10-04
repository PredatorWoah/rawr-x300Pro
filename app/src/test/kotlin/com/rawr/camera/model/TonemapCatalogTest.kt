package com.rawr.camera.model

import com.rawr.camera.settings.model.ImageToneNumericParameter
import com.rawr.camera.settings.model.ImageToneSpecs
import com.rawr.camera.settings.model.tonemapDescriptor
import com.rawr.camera.settings.model.withNumeric
import org.junit.Assert.assertEquals
import org.junit.Assert.assertTrue
import org.junit.Test

/**
 * Guards the single-source-of-truth contract: the strip, Settings Tone group
 * and Renderer recipe all read the same parameter list, labels and ranges.
 */
class TonemapCatalogTest {
    @Test
    fun canonicalOrderCoversEveryToneParameterExactlyOnce() {
        assertEquals(ToneParameter.entries.toSet(), TonemapCatalog.tone.mapNotNull { it.param }.toSet())
        assertEquals(ToneParameter.entries.size, TonemapCatalog.tone.size)
        assertEquals(TonemapCatalog.tone.size + 1, TonemapCatalog.all.size)
    }

    @Test
    fun jsonKeysAreUnique() {
        val keys = TonemapCatalog.all.map { it.jsonKey }
        assertEquals(keys.size, keys.toSet().size)
    }

    @Test
    fun settingsSpecsDelegateToCatalog() {
        fun check(parameter: ImageToneNumericParameter, expected: TonemapParam) {
            val descriptor = parameter.tonemapDescriptor()
            val spec = ImageToneSpecs.let {
                when (parameter) {
                    ImageToneNumericParameter.RenderExposure -> it.renderExposure
                    ImageToneNumericParameter.BlackToe -> it.blacks
                    ImageToneNumericParameter.Shadows -> it.shadows
                    ImageToneNumericParameter.Contrast -> it.contrast
                    ImageToneNumericParameter.MidtonePivot -> it.midtones
                    ImageToneNumericParameter.Highlights -> it.highlights
                    ImageToneNumericParameter.ShoulderWhitePoint -> it.whites
                    ImageToneNumericParameter.Saturation -> it.saturation
                    ImageToneNumericParameter.Vibrance -> it.vibrance
                    else -> error("not a tone parameter")
                }
            }
            assertEquals(expected, descriptor)
            assertEquals(expected.longLabel, spec.label)
            assertEquals(expected.minimum, spec.minimum, 0f)
            assertEquals(expected.maximum, spec.maximum, 0f)
            assertEquals(expected.defaultValue, spec.defaultValue, 0f)
            assertEquals(expected.step, spec.step, 0f)
            assertEquals(expected.unit, spec.unit)
            assertEquals(expected.decimals, spec.decimals)
        }
        TonemapCatalog.all.forEach { descriptor ->
            val parameter = when (descriptor) {
                TonemapCatalog.renderExposure -> ImageToneNumericParameter.RenderExposure
                TonemapCatalog.blacks -> ImageToneNumericParameter.BlackToe
                TonemapCatalog.shadows -> ImageToneNumericParameter.Shadows
                TonemapCatalog.contrast -> ImageToneNumericParameter.Contrast
                TonemapCatalog.midtones -> ImageToneNumericParameter.MidtonePivot
                TonemapCatalog.highlights -> ImageToneNumericParameter.Highlights
                TonemapCatalog.whites -> ImageToneNumericParameter.ShoulderWhitePoint
                TonemapCatalog.saturation -> ImageToneNumericParameter.Saturation
                else -> ImageToneNumericParameter.Vibrance
            }
            check(parameter, descriptor)
        }
    }

    @Test
    fun defaultsAreNeutralAndRangesValid() {
        assertTrue(TonemapCatalog.all.all { it.defaultValue in it.minimum..it.maximum })
        assertTrue(TonemapCatalog.tone.all { it.defaultValue == TonemapControlContract.TONE_UI_NEUTRAL })
    }

    @Test
    fun activeImageToneReflectsPerProfileWrites() {
        val base = com.rawr.camera.settings.model.SettingsCatalog.initialState().values
        val viaSettings =
            base.copy(
                rawrBaseTone =
                    base.rawrBaseTone.withNumeric(ImageToneNumericParameter.BlackToe, 7f)
            )
        assertEquals(7f, viaSettings.activeImageTone().blacks, 0f)

        val viaCapture =
            base.copy(rawrBaseTone = base.rawrBaseTone.withToneParameter(ToneParameter.Blacks, 9f))
        assertEquals(9f, viaCapture.activeImageTone().blacks, 0f)
        // Whole-set equivalence between the two write paths.
        assertEquals(
            viaSettings.rawrBaseTone.withToneParameter(ToneParameter.Blacks, 9f),
            viaCapture.rawrBaseTone.withNumeric(ImageToneNumericParameter.BlackToe, 9f)
        )
    }
}