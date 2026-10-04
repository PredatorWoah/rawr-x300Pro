package com.rawr.camera.settings.ui

import androidx.compose.runtime.Composable
import androidx.compose.ui.tooling.preview.Preview
import com.rawr.camera.settings.architecture.SettingsAction
import com.rawr.camera.settings.architecture.SettingsDispatch
import com.rawr.camera.settings.fixtures.SettingsFixtures
import com.rawr.camera.settings.model.*
import com.rawr.camera.ui.CaptureTheme

private val noOpDispatch = SettingsDispatch { _: SettingsAction -> }

@Composable
private fun PreviewSettings(state: SettingsUiState) {
    CaptureTheme { SettingsScreen(state = state, dispatch = noOpDispatch) }
}

private fun base() = SettingsFixtures.initialState()

private fun at(destination: SettingsDestination, tab: ImageToneTab = ImageToneTab.ExposureTonality): SettingsUiState {
    val state = base()
    return state.copy(presentation = state.presentation.copy(destination = destination, imageToneTab = tab))
}

@Preview(name = "Settings Home", widthDp = 360, heightDp = 800)
@Composable
private fun SettingsHomePreview() = PreviewSettings(base())

@Preview(name = "Storage", widthDp = 360, heightDp = 800)
@Composable
private fun StoragePreview() = PreviewSettings(at(SettingsDestination.Section(SettingsSection.Storage)))

@Preview(name = "Monitoring", widthDp = 360, heightDp = 800)
@Composable
private fun MonitoringPreview() = PreviewSettings(at(SettingsDestination.Section(SettingsSection.Monitoring)))

@Preview(name = "Image Tone - Tonality", widthDp = 360, heightDp = 800)
@Composable
private fun ImageToneTonalityPreview() = PreviewSettings(at(SettingsDestination.Section(SettingsSection.ImageTone)))

@Preview(name = "Image Tone - Color", widthDp = 360, heightDp = 800)
@Composable
private fun ImageToneColorPreview() =
    PreviewSettings(at(SettingsDestination.Section(SettingsSection.ImageTone), ImageToneTab.Color))

@Preview(name = "Image Tone - Output", widthDp = 360, heightDp = 800)
@Composable
private fun ImageToneOutputPreview() =
    PreviewSettings(at(SettingsDestination.Section(SettingsSection.ImageTone), ImageToneTab.Output))

@Preview(name = "Capture", widthDp = 360, heightDp = 800)
@Composable
private fun CaptureSettingsPreview() = PreviewSettings(at(SettingsDestination.Section(SettingsSection.Capture)))

@Preview(name = "Camera Device Supported", widthDp = 360, heightDp = 800)
@Composable
private fun CameraSupportedPreview() = PreviewSettings(at(SettingsDestination.Section(SettingsSection.CameraDevice)))

@Preview(name = "Camera Device Unsupported", widthDp = 360, heightDp = 800)
@Composable
private fun CameraUnsupportedPreview() {
    val s = SettingsFixtures.initialState(false)
    PreviewSettings(
        s.copy(
            presentation = s.presentation.copy(destination = SettingsDestination.Section(SettingsSection.CameraDevice))
        )
    )
}

@Preview(name = "Exposure", widthDp = 360, heightDp = 800)
@Composable
private fun ExposurePreview() = PreviewSettings(at(SettingsDestination.Section(SettingsSection.Exposure)))

@Preview(name = "Maximum Post Gain Selector", widthDp = 360, heightDp = 800)
@Composable
private fun MaxPostGainPreview() = PreviewSettings(at(SettingsDestination.ChoiceSelector(ChoiceSelectorKind.MaxPostGain)))

@Preview(name = "Auto Min FPS Selector", widthDp = 360, heightDp = 800)
@Composable
private fun AutoMinFpsPreview() =
    PreviewSettings(at(SettingsDestination.ChoiceSelector(ChoiceSelectorKind.AutoMinFps)))

@Preview(name = "Image Tone Reset Confirmation", widthDp = 360, heightDp = 800)
@Composable
private fun ResetPreview() {
    val s = at(SettingsDestination.Section(SettingsSection.ImageTone))
    PreviewSettings(s.copy(presentation = s.presentation.copy(pendingReset = ResetTarget.AllImageTone)))
}

@Preview(name = "Settings Landscape", widthDp = 800, heightDp = 360)
@Composable
private fun SettingsLandscapePreview() = PreviewSettings(base())

@Preview(name = "Image Tone Landscape", widthDp = 800, heightDp = 360)
@Composable
private fun ImageToneLandscapePreview() =
    PreviewSettings(at(SettingsDestination.Section(SettingsSection.ImageTone), ImageToneTab.Color))
