package com.rawr.camera.settings.architecture

import com.rawr.camera.model.ImageToneState
import com.rawr.camera.settings.model.*

/** Edits owned by the tone settings feature. */
internal fun reduceToneSettings(
    state: SettingsUiState, action: SettingsApplicationAction, defaults: SettingsValues
): SettingsUiState = when (action) {
    is SetNumericValue -> {
        val spec = action.parameter.reviewSpec()
        if (!spec.accepts(action.value)) {
            state
        } else if (action.parameter.isPerProfileTone()) {
            // TONE lives on the active render profile: RAWR NTRL owns
            // rawrBaseTone, each LUT profile owns its own tone.
            state.withValues(
                state.values.editActiveProfileTone { it.withNumeric(action.parameter, action.value) }
            ).withImageToneAcknowledged()
        } else {
            state
                .withValues(
                    state.values.copy(
                        imageTone = updateNumeric(state.values.imageTone, action.parameter, action.value)
                    )
                ).withImageToneAcknowledged()
        }
    }

    is ConfirmReset -> {
        val reset = resetValues(state, action.target, defaults)
        state
            .withValues(
                reset
            ).withImageToneAcknowledged()
            .copy(presentation = state.presentation.copy(pendingReset = null))
    }
    else -> error("Unsupported tone settings action: $action")
}

private fun updateNumeric(t: ImageToneState, parameter: ImageToneNumericParameter, value: Float): ImageToneState =
    when (parameter) {
        ImageToneNumericParameter.RenderExposure -> t.copy(renderExposure = value)
        ImageToneNumericParameter.BlackToe -> t.copy(blacks = value)
        ImageToneNumericParameter.Shadows -> t.copy(shadows = value)
        ImageToneNumericParameter.Contrast -> t.copy(contrast = value)
        ImageToneNumericParameter.MidtonePivot -> t.copy(midtones = value)
        ImageToneNumericParameter.Highlights -> t.copy(highlights = value)
        ImageToneNumericParameter.ShoulderWhitePoint -> t.copy(whites = value)
        ImageToneNumericParameter.Saturation -> t.copy(saturation = value)
        ImageToneNumericParameter.Vibrance -> t.copy(vibrance = value)
        ImageToneNumericParameter.ColorRenderingStrength -> t.copy(colorRenderingStrength = value)
        ImageToneNumericParameter.WbTemperature -> t.copy(wbTemperature = value)
        ImageToneNumericParameter.WbTint -> t.copy(wbTint = value)
        ImageToneNumericParameter.JpegQuality -> t.copy(jpegQuality = value)
    }

private fun resetValues(state: SettingsUiState, target: ResetTarget, defaults: SettingsValues): SettingsValues {
    val values = state.values
    val current = values.imageTone
    val toneDefaults = defaults.imageTone
    val profileDefaults = defaults.rawrBaseTone
    // Applies the reset to the active profile's tone; global-only fields
    // stay on imageTone.
    fun resetActiveProfile(transform: (ProfileTone) -> ProfileTone): SettingsValues =
        values.editActiveProfileTone(transform)
    return when (target) {
        ResetTarget.ExposureTonality -> {
            resetActiveProfile {
                it.copy(
                    renderExposure = profileDefaults.renderExposure,
                    blacks = profileDefaults.blacks,
                    shadows = profileDefaults.shadows,
                    contrast = profileDefaults.contrast,
                    midtones = profileDefaults.midtones,
                    highlights = profileDefaults.highlights,
                    whites = profileDefaults.whites
                )
            }
        }

        ResetTarget.Color -> {
            val withProfile = resetActiveProfile {
                it.copy(
                    saturation = profileDefaults.saturation,
                    vibrance = profileDefaults.vibrance
                )
            }
            withProfile.copy(
                imageTone = withProfile.imageTone.copy(
                    colorRenderingStrength = toneDefaults.colorRenderingStrength,
                    wbTemperature = toneDefaults.wbTemperature,
                    wbTint = toneDefaults.wbTint
                )
            )
        }

        ResetTarget.Output -> {
            values.copy(
                imageTone = current.copy(
                    outputColorSpaceId = toneDefaults.outputColorSpaceId,
                    transferFunctionId = toneDefaults.transferFunctionId,
                    jpegQuality = toneDefaults.jpegQuality,
                    jpegChromaSubsamplingId = toneDefaults.jpegChromaSubsamplingId
                )
            )
        }

        ResetTarget.AllImageTone -> {
            resetActiveProfile { ProfileTone.Neutral }.copy(imageTone = toneDefaults)
        }
    }
}
