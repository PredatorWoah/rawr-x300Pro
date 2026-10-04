package com.rawr.camera.settings.architecture

import com.rawr.camera.settings.model.*

/** Edits owned by the choice settings feature. */
internal fun reduceChoiceSettings(
    state: SettingsUiState, action: SettingsApplicationAction
): SettingsUiState = when (action) {
    is SetChoice -> {
        setChoice(state, action)
    }
    else -> error("Unsupported choice settings action: $action")
}

private fun setChoice(state: SettingsUiState, action: SetChoice): SettingsUiState {
    if (!state.capabilities.contains(action.kind, action.candidateId)) return state
    val v = state.values
    val next =
        when (action.kind) {
            ChoiceSelectorKind.SaveLocation -> {
                v.copy(saveLocationId = action.candidateId)
            }

            ChoiceSelectorKind.FalseColorPreset -> {
                v.copy(falseColorPresetId = action.candidateId)
            }

            ChoiceSelectorKind.PeakingSensitivity -> {
                v.copy(peakingSensitivityId = action.candidateId)
            }

            ChoiceSelectorKind.OutputColorSpace -> {
                v.copy(
                    imageTone = v.imageTone.copy(outputColorSpaceId = action.candidateId)
                )
            }

            ChoiceSelectorKind.TransferFunction -> {
                v.copy(
                    imageTone = v.imageTone.copy(transferFunctionId = action.candidateId)
                )
            }

            ChoiceSelectorKind.JpegChromaSubsampling -> {
                v.copy(
                    imageTone = v.imageTone.copy(jpegChromaSubsamplingId = action.candidateId)
                )
            }

            ChoiceSelectorKind.DngCompression -> {
                v.copy(dngCompressionId = action.candidateId)
            }

            ChoiceSelectorKind.MaxPostGain -> {
                v.copy(maxPostGainId = action.candidateId)
            }

            ChoiceSelectorKind.AutoMinFps -> {
                v.copy(autoMinFpsId = action.candidateId)
            }

            ChoiceSelectorKind.FilmOutputSpace -> {
                val index = action.candidateId.toIntOrNull() ?: return state
                v.copy(filmSimLook = v.filmSimLook.copy(outputColorSpace = index))
            }
        }
    // Choosing dismisses the selector back to wherever it was opened
    // from (the stack top); fresh states without history keep the legacy
    // logical-parent mapping.
    val presentation = state.presentation
    val stack = presentation.backStack
    val nextPresentation =
        if (presentation.destination is SettingsDestination.ChoiceSelector && stack.isNotEmpty()) {
            presentation.copy(destination = stack.last(), backStack = stack.dropLast(1))
        } else {
            presentation.copy(destination = selectorParent(action.kind))
        }
    return state
        .withValues(
            next
        ).copy(presentation = nextPresentation)
}

private fun SettingsCapabilities.contains(kind: ChoiceSelectorKind, id: String): Boolean = when (kind) {
    ChoiceSelectorKind.SaveLocation -> saveLocationChoices.any { it.id == id }
    ChoiceSelectorKind.FalseColorPreset -> falseColorPresets.any { it.id == id }
    ChoiceSelectorKind.PeakingSensitivity -> peakingSensitivityChoices.any { it.id == id }
    ChoiceSelectorKind.OutputColorSpace -> outputColorSpaces.any { it.id == id }
    ChoiceSelectorKind.TransferFunction -> transferFunctions.any { it.id == id }
    ChoiceSelectorKind.JpegChromaSubsampling -> jpegChromaSubsamplingChoices.any { it.id == id }
    ChoiceSelectorKind.DngCompression -> dngCompressionChoices.any { it.id == id }
    ChoiceSelectorKind.MaxPostGain -> maxPostGainChoices.any { it.id == id }
    ChoiceSelectorKind.AutoMinFps -> autoMinFpsChoices.any { it.id == id }
    // Film output spaces are static (not capability-dependent): ids are indices.
    ChoiceSelectorKind.FilmOutputSpace -> id.toIntOrNull() in FilmStocks.colorSpaces.indices
}

private fun selectorParent(kind: ChoiceSelectorKind): SettingsDestination = when (kind) {
    ChoiceSelectorKind.SaveLocation -> {
        SettingsDestination.Section(SettingsSection.Storage)
    }

    ChoiceSelectorKind.FalseColorPreset, ChoiceSelectorKind.PeakingSensitivity -> {
        SettingsDestination.Section(
            SettingsSection.Monitoring
        )
    }

    ChoiceSelectorKind.OutputColorSpace, ChoiceSelectorKind.TransferFunction, ChoiceSelectorKind.JpegChromaSubsampling -> {
        SettingsDestination
            .Section(
                SettingsSection.Jpeg
            )
    }

    ChoiceSelectorKind.DngCompression -> {
        SettingsDestination.Section(SettingsSection.Dng)
    }

        ChoiceSelectorKind.MaxPostGain, ChoiceSelectorKind.AutoMinFps -> {
            SettingsDestination.Section(
                SettingsSection.Exposure
            )
        }

        ChoiceSelectorKind.FilmOutputSpace -> {
            SettingsDestination.FilmSimSubPage(FilmSimSection.Output)
        }
}
