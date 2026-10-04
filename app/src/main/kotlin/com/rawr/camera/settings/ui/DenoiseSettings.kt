package com.rawr.camera.settings.ui

import androidx.compose.foundation.layout.*
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import com.rawr.camera.settings.architecture.*
import com.rawr.camera.settings.model.*

@Composable
internal fun DenoiseSettings(state: SettingsUiState, dispatch: SettingsDispatch) {
    SettingsPageContainer {
        DenoiseTargetSelector(state, dispatch)
        PhotoDenoiseGroup(state, dispatch)
        if (state.values.photoDenoise is DenoiseConfig.Wavelet) {
            PhotoWaveletGroup(state, dispatch)
        }
        if (state.values.photoDenoise is DenoiseConfig.Galosh) {
            GaloshGroup(state, dispatch)
        }
        VideoDenoiseGroup(state, dispatch)
    }
}

@Composable
internal fun DenoiseGroup(state: SettingsUiState, dispatch: SettingsDispatch) {
    DenoiseTargetSelector(state, dispatch)
    PhotoDenoiseGroup(state, dispatch)
    if (state.values.photoDenoise is DenoiseConfig.Wavelet) {
        PhotoWaveletGroup(state, dispatch)
    }
    if (state.values.photoDenoise is DenoiseConfig.Galosh) {
        GaloshGroup(state, dispatch)
    }
    VideoDenoiseGroup(state, dispatch)
}

@Composable
private fun DenoiseTargetSelector(state: SettingsUiState, dispatch: SettingsDispatch) {
    val v = state.values
    SettingsGroup(
        description = "Photo denoise runs on single-frame stills only (wavelets or galosh); multiframe merges denoise themselves. Video denoise is wavelets only, with its own strength."
    ) {
        PhotoVideoTargetSelector(
            photoOn = v.photoDenoise != DenoiseConfig.Off,
            videoOn = v.videoDenoiseEnabled,
            onPhotoChange = { dispatch.invoke(SetPhotoDenoiseEnabled(it)) },
            onVideoChange = { dispatch.invoke(SetVideoDenoiseEnabled(it)) }
        )
    }
}

@Composable
private fun PhotoDenoiseGroup(state: SettingsUiState, dispatch: SettingsDispatch) {
    val v = state.values
    // Galosh always carries at least one lane by construction, so the old
    // "galosh (no lane)" state is gone.
    SettingsGroup(title = "Photo") {
        val photoOn = v.photoDenoise != DenoiseConfig.Off
        // Off has no method; enabling lands on Wavelet, so Off renders as
        // Wavelet-selected-but-disabled rather than a phantom selection.
        SegmentedOptionRow(
            options = listOf("Wavelet", "Galosh"),
            selected = if (v.photoDenoise is DenoiseConfig.Galosh) 1 else 0,
            onSelect = { dispatch.invoke(SetPhotoDenoiseMethod(it)) },
            enabled = photoOn
        )
    }
}

@Composable
private fun PhotoWaveletGroup(state: SettingsUiState, dispatch: SettingsDispatch) {
    val wavelet = (state.values.photoDenoise as? DenoiseConfig.Wavelet)?.config ?: return
    SettingsGroup(title = "Photo · Wavelet") {
        StandaloneNumericSliderRow(
            identity = "denoise_strength_photo",
            label = "Strength",
            minimum = 0f,
            maximum = 8f,
            step = 0.1f,
            decimals = 1,
            value = wavelet.strength,
            enabled = true
        ) { dispatch.invoke(SetPhotoDenoiseStrength(it)) }
        SettingDivider()
        StandaloneNumericSliderRow(
            identity = "denoise_detail_photo",
            label = "Detail",
            minimum = 0f,
            maximum = 1.8f,
            step = 0.05f,
            decimals = 2,
            value = wavelet.detail,
            enabled = true
        ) { dispatch.invoke(SetPhotoDenoiseDetail(it)) }
        SettingDivider()
        StandaloneNumericSliderRow(
            identity = "denoise_luma_photo",
            label = "Luma",
            minimum = 0f,
            maximum = 1f,
            step = 0.05f,
            decimals = 2,
            value = wavelet.luma,
            enabled = true
        ) { dispatch.invoke(SetPhotoDenoiseLuma(it)) }
        SettingDivider()
        StandaloneNumericSliderRow(
            identity = "denoise_scales_photo",
            label = "Scales",
            minimum = 1f,
            maximum = 7f,
            step = 1f,
            decimals = 0,
            value = wavelet.scales.toFloat(),
            enabled = true
        ) { dispatch.invoke(SetPhotoDenoiseScales(it.toInt())) }
    }
}

@Composable
private fun VideoDenoiseGroup(state: SettingsUiState, dispatch: SettingsDispatch) {
    val v = state.values
    SettingsGroup(title = "Video · Wavelet") {
        StandaloneNumericSliderRow(
            identity = "denoise_strength_video",
            label = "Strength",
            minimum = 0f,
            maximum = 8f,
            step = 0.1f,
            decimals = 1,
            value = v.videoDenoiseStrength,
            enabled = v.videoDenoiseEnabled
        ) { dispatch.invoke(SetVideoDenoiseStrength(it)) }
        SettingDivider()
        StandaloneNumericSliderRow(
            identity = "denoise_detail_video",
            label = "Detail",
            minimum = 0f,
            maximum = 1.8f,
            step = 0.05f,
            decimals = 2,
            value = v.videoDenoiseDetail,
            enabled = v.videoDenoiseEnabled
        ) { dispatch.invoke(SetVideoDenoiseDetail(it)) }
        SettingDivider()
        StandaloneNumericSliderRow(
            identity = "denoise_luma_video",
            label = "Luma",
            minimum = 0f,
            maximum = 1f,
            step = 0.05f,
            decimals = 2,
            value = v.videoDenoiseLuma,
            enabled = v.videoDenoiseEnabled
        ) { dispatch.invoke(SetVideoDenoiseLuma(it)) }
        SettingDivider()
        StandaloneNumericSliderRow(
            identity = "denoise_scales_video",
            label = "Scales",
            minimum = 1f,
            maximum = 7f,
            step = 1f,
            decimals = 0,
            value = v.videoDenoiseScales.toFloat(),
            enabled = v.videoDenoiseEnabled
        ) { dispatch.invoke(SetVideoDenoiseScales(it.toInt())) }
    }
}

@Composable
internal fun GaloshGroup(state: SettingsUiState, dispatch: SettingsDispatch) {
    if (state.values.photoDenoise !is DenoiseConfig.Galosh) return
    GaloshRawGroup(state, dispatch)
    GaloshYuvGroup(state, dispatch)
}

@Composable
private fun GaloshRawGroup(state: SettingsUiState, dispatch: SettingsDispatch) {
    val galosh = state.values.photoDenoise as? DenoiseConfig.Galosh ?: return
    SettingsGroup(title = "Photo · Galosh RAW") {
        SegmentedOptionRow(
            options = listOf("Off", "Full", "Chroma"),
            selected = galosh.raw.mode.ordinal,
            onSelect = { dispatch.invoke(SetGaloshRawMode(it)) }
        )
        if (galosh.raw.mode != LaneMode.OFF) {
            SettingDivider()
            StandaloneNumericSliderRow(
                identity = "galosh_strength",
                label = "Strength",
                minimum = 0f,
                maximum = 8f,
                step = 0.1f,
                decimals = 1,
                value = galosh.raw.strength,
                enabled = true
            ) { dispatch.invoke(SetGaloshStrength(it)) }
            // Luma lane is bypassed (copied through) in Chroma mode, so the
            // slider is hidden there rather than shown dead.
            if (galosh.raw.mode == LaneMode.FULL) {
                SettingDivider()
                StandaloneNumericSliderRow(
                    identity = "galosh_luma",
                    label = "Luma",
                    minimum = 0f,
                    maximum = 8f,
                    step = 0.1f,
                    decimals = 1,
                    value = galosh.raw.luma,
                    enabled = true
                ) { dispatch.invoke(SetGaloshLuma(it)) }
            }
            SettingDivider()
            StandaloneNumericSliderRow(
                identity = "galosh_chroma",
                label = "Chroma",
                minimum = 0f,
                maximum = 8f,
                step = 0.1f,
                decimals = 1,
                value = galosh.raw.chroma,
                enabled = true
            ) { dispatch.invoke(SetGaloshChroma(it)) }
        }
    }
}

@Composable
private fun GaloshYuvGroup(state: SettingsUiState, dispatch: SettingsDispatch) {
    val galosh = state.values.photoDenoise as? DenoiseConfig.Galosh ?: return
    SettingsGroup(title = "Photo · Galosh YUV") {
        SegmentedOptionRow(
            options = listOf("Off", "Full", "Chroma"),
            selected = galosh.yuv.mode.ordinal,
            onSelect = { dispatch.invoke(SetGaloshYuvMode(it)) }
        )
        if (galosh.yuv.mode != LaneMode.OFF) {
            // Luma lane is copied through in Chroma mode (native bypass),
            // so the slider is hidden there rather than shown dead.
            if (galosh.yuv.mode == LaneMode.FULL) {
                SettingDivider()
                StandaloneNumericSliderRow(
                    identity = "galosh_yuv_strength_y",
                    label = "Luma",
                    minimum = 0f,
                    maximum = 8f,
                    step = 0.1f,
                    decimals = 1,
                    value = galosh.yuv.strengthY,
                    enabled = true
                ) { dispatch.invoke(SetGaloshYuvStrengthY(it)) }
            }
            SettingDivider()
            StandaloneNumericSliderRow(
                identity = "galosh_yuv_strength_c",
                label = "Chroma",
                minimum = 0f,
                maximum = 8f,
                step = 0.1f,
                decimals = 1,
                value = galosh.yuv.strengthC,
                enabled = true
            ) { dispatch.invoke(SetGaloshYuvStrengthC(it)) }
        }
    }
}
