package com.rawr.camera.settings.architecture

import com.rawr.camera.settings.model.*

/** Edits owned by the denoise settings feature. */
internal fun reduceDenoiseSettings(
    state: SettingsUiState, action: SettingsApplicationAction
): SettingsUiState = when (action) {
    is SetPhotoDenoiseEnabled -> {
        // Master switch for photo denoisers. Transitions live on the
        // sealed type; native enforces the same precedence as backstop.
        state.withValues(state.values.copy(photoDenoise = state.values.photoDenoise.withMaster(action.enabled)))
    }

    is SetPhotoDenoiseStrength -> {
        state.withValues(state.values.copy(photoDenoise = state.values.photoDenoise.withWaveletStrength(action.value)))
    }

    is SetPhotoDenoiseDetail -> {
        state.withValues(state.values.copy(photoDenoise = state.values.photoDenoise.withWaveletDetail(action.value)))
    }

    is SetPhotoDenoiseLuma -> {
        state.withValues(state.values.copy(photoDenoise = state.values.photoDenoise.withWaveletLuma(action.value)))
    }

    is SetPhotoDenoiseScales -> {
        state.withValues(state.values.copy(photoDenoise = state.values.photoDenoise.withWaveletScales(action.value)))
    }

    is SetPhotoDenoiseMethod -> {
        // Explicit method switch (0 = wavelet, 1 = galosh).
        val next = if (action.method.coerceIn(0, 1) == 1) {
            state.values.photoDenoise.withGaloshMethod()
        } else {
            state.values.photoDenoise.withWaveletMethod()
        }
        state.withValues(state.values.copy(photoDenoise = next))
    }

    is SetVideoDenoiseEnabled -> {
        state.withValues(state.values.copy(videoDenoiseEnabled = action.enabled))
    }

    is SetVideoDenoiseStrength -> {
        state.withValues(state.values.copy(videoDenoiseStrength = action.value.coerceIn(0f, 8f)))
    }

    is SetVideoDenoiseDetail -> {
        state.withValues(state.values.copy(videoDenoiseDetail = action.value.coerceIn(0f, 1.8f)))
    }

    is SetVideoDenoiseLuma -> {
        state.withValues(state.values.copy(videoDenoiseLuma = action.value.coerceIn(0f, 1f)))
    }

    is SetVideoDenoiseScales -> {
        state.withValues(state.values.copy(videoDenoiseScales = action.value.coerceIn(1, 7)))
    }

    is SetGaloshRawMode -> {
        // Lane picks imply the galosh method + master switch; parking
        // the last lane falls back to wavelet. All inside withRawMode.
        state.withValues(
            state.values.copy(photoDenoise = state.values.photoDenoise.withRawMode(LaneMode.of(action.mode)))
        )
    }

    is SetGaloshStrength -> {
        state.withValues(state.values.copy(photoDenoise = state.values.photoDenoise.withRawStrengths(strength = action.value)))
    }

    is SetGaloshLuma -> {
        state.withValues(state.values.copy(photoDenoise = state.values.photoDenoise.withRawStrengths(luma = action.value)))
    }

    is SetGaloshChroma -> {
        state.withValues(state.values.copy(photoDenoise = state.values.photoDenoise.withRawStrengths(chroma = action.value)))
    }

    is SetGaloshYuvMode -> {
        // Same method/master semantics as the RAW lane.
        state.withValues(
            state.values.copy(photoDenoise = state.values.photoDenoise.withYuvMode(LaneMode.of(action.mode)))
        )
    }

    is SetGaloshYuvStrengthY -> {
        state.withValues(
            state.values.copy(photoDenoise = state.values.photoDenoise.withYuvStrengths(strengthY = action.value))
        )
    }

    is SetGaloshYuvStrengthC -> {
        state.withValues(
            state.values.copy(photoDenoise = state.values.photoDenoise.withYuvStrengths(strengthC = action.value))
        )
    }
    else -> error("Unsupported denoise settings action: $action")
}
