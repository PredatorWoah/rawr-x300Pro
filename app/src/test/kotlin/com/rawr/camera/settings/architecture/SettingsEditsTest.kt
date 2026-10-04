package com.rawr.camera.settings.architecture

import com.rawr.camera.model.ToneParameter
import com.rawr.camera.model.withToneParameter
import com.rawr.camera.settings.model.*
import kotlin.test.*

class SettingsEditsTest {
    private val defaults = SettingsCatalog.initialState().values

    @Test fun toneEditsOnlyAffectTheSelectedRenderProfile() {
        val profile = ImportedLutProfile("lut", "LUT", emptyList())
        val other = ImportedLutProfile("other", "Other", emptyList())
        val original = defaults.copy(
            colorRenderProfile = ColorRenderProfile.UserLut,
            selectedUserLutProfileId = profile.id,
            userLutProfiles = listOf(profile, other)
        )
        val edited = original.editActiveProfileTone { it.withToneParameter(ToneParameter.Contrast, 30f) }
        assertEquals(30f, edited.userLutProfiles.first().tone.contrast)
        assertEquals(other, edited.userLutProfiles.last())
        assertEquals(original.rawrBaseTone, edited.rawrBaseTone)
    }

    @Test fun neutralProfileEditsLeaveLutToneAlone() {
        val profile = ImportedLutProfile("lut", "LUT", emptyList())
        val edited = defaults.copy(userLutProfiles = listOf(profile)).editActiveProfileTone {
            it.withToneParameter(ToneParameter.Shadows, -20f)
        }
        assertEquals(-20f, edited.rawrBaseTone.shadows)
        assertEquals(profile, edited.userLutProfiles.single())
    }

    @Test fun captureScrubClampsQuantizesAndKeepsPresetIdentity() {
        val initial = defaults.copy(selectedFilmPresetId = "origin")
        val edited = initial.withFilmNumeric(
            SetFilmSimNumericValue(FilmSimNumericParameter.GrainAmount, 0.726f),
            quantize = true, enableGrain = true
        )
        assertEquals(0.73f, edited.filmSimLook.grainAmount)
        assertTrue(edited.filmSimLook.grainEnabled)
        assertEquals("origin", edited.selectedFilmPresetId)
        val clamped = initial.withFilmNumeric(
            SetFilmSimNumericValue(FilmSimNumericParameter.GrainAmount, 100f), quantize = true
        )
        assertEquals(FilmSimSpecs.forParameter(FilmSimNumericParameter.GrainAmount).maximum, clamped.filmSimLook.grainAmount)
    }

    @Test fun capturePreferenceEditPreservesNewerValuesFromOtherScreens() {
        val before = com.rawr.camera.model.CaptureUiState(com.rawr.camera.fixtures.CaptureFixtures.baseline())
        val latest = defaults.copy(filmSimEnabled = true, videoFps = 24)
        val edited = latest.withCapturePreferenceChanges(before, before.copy(jpegEnabled = false))
        assertFalse(edited.jpegEnabled)
        assertTrue(edited.filmSimEnabled)
        assertEquals(24, edited.videoFps)
    }

    @Test fun toneOnlyEditsDoNotTriggerACapturePreferenceRestore() {
        val edited = defaults.editActiveProfileTone { it.copy(contrast = 20f) }
        assertEquals(defaults.capturePreferences(), edited.capturePreferences())
    }

    @Test fun resetPreservesDisabledGrain() {
        val initial = defaults.copy(filmSimLook = defaults.filmSimLook.copy(grainEnabled = false))
        val edited = initial.withFilmNumeric(SetFilmSimNumericValue(
            FilmSimNumericParameter.GrainAmount,
            FilmSimSpecs.forParameter(FilmSimNumericParameter.GrainAmount).defaultValue
        ))
        assertFalse(edited.filmSimLook.grainEnabled)
        assertSame(initial, initial.withFilmPreset("missing"))
    }
}
