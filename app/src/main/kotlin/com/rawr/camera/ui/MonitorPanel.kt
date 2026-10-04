package com.rawr.camera.ui

import androidx.compose.foundation.layout.*
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.platform.testTag
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.sp
import com.rawr.camera.architecture.CaptureDispatch
import com.rawr.camera.architecture.ToggleFalseColor
import com.rawr.camera.architecture.ToggleOverlayArmed
import com.rawr.camera.architecture.ToggleScope
import com.rawr.camera.architecture.ToggleWaveformMode
import com.rawr.camera.model.CaptureUiState
import com.rawr.camera.model.Orientation
import com.rawr.camera.model.OverlayMode
import com.rawr.camera.model.ScopeType

/** Compact top-bar controls for overlays and scope enablement. */
@Composable
internal fun MonitorPanel(state: CaptureUiState, dispatch: CaptureDispatch, modifier: Modifier) {
    val haptics = LocalCaptureHaptics.current
    val landscape = state.orientation == Orientation.Landscape

    CompactTopBarPalette(
        title = "MONITORING",
        orientation = state.orientation,
        modifier = modifier.testTag(CaptureTestTags.MONITOR_PANEL),
        leadingWeight = .62f,
        trailingWeight = .38f,
        leading = {
            Column(
                modifier = Modifier.fillMaxSize(),
                verticalArrangement = Arrangement.spacedBy(CaptureDimens.TopBarPaletteInnerGap)
            ) {
                Text(
                    "OVERLAY · ARM AUTO",
                    color = Color.White.copy(alpha = .35f),
                    fontFamily = CaptureMono,
                    fontWeight = FontWeight.Bold,
                    fontSize = 8.sp,
                    maxLines = 1
                )
                Row(
                    Modifier.fillMaxWidth(),
                    horizontalArrangement = Arrangement.spacedBy(CaptureDimens.TopBarPaletteInnerGap)
                ) {
                    ArmChoice(
                        label = if (landscape) "PEAK" else "PEAKING",
                        armed = OverlayMode.Peaking in state.armedOverlays,
                        modifier = Modifier.weight(1f),
                        testTag = CaptureTestTags.monitorArm("peaking"),
                        onToggle = { dispatch(ToggleOverlayArmed(OverlayMode.Peaking)) }
                    )
                    ArmChoice(
                        label = if (landscape) "RAW HL" else "RAW HIGHLIGHTS",
                        armed = OverlayMode.RawHighlights in state.armedOverlays,
                        modifier = Modifier.weight(1f),
                        testTag = CaptureTestTags.monitorArm("raw_highlights"),
                        onToggle = { dispatch(ToggleOverlayArmed(OverlayMode.RawHighlights)) }
                    )
                }
                Row(
                    Modifier.fillMaxWidth(),
                    horizontalArrangement = Arrangement.spacedBy(CaptureDimens.TopBarPaletteInnerGap)
                ) {
                    ArmChoice(
                        label = if (landscape) "SHADOW" else "TONEMAP SHADOWS",
                        armed = OverlayMode.TonemapShadows in state.armedOverlays,
                        modifier = Modifier.weight(1f),
                        testTag = CaptureTestTags.monitorArm("tonemap_shadows"),
                        onToggle = { dispatch(ToggleOverlayArmed(OverlayMode.TonemapShadows)) }
                    )
                    ArmChoice(
                        label = if (landscape) "FALSE" else "FALSE COLOR",
                        armed = state.falseColorManual,
                        modifier = Modifier.weight(1f),
                        testTag = CaptureTestTags.monitorArm("false_color"),
                        onToggle = { dispatch(ToggleFalseColor) }
                    )
                }
            }
        },
        trailing = {
            Column(
                modifier = Modifier.fillMaxSize(),
                verticalArrangement = Arrangement.spacedBy(CaptureDimens.TopBarPaletteInnerGap)
            ) {
                Text(
                    "SCOPES",
                    color = Color.White.copy(alpha = .35f),
                    fontFamily = CaptureMono,
                    fontWeight = FontWeight.Bold,
                    fontSize = 8.sp
                )
                ScopeType.entries.forEach { scope ->
                    val label =
                        when (scope) {
                            ScopeType.Waveform -> "WAVE"
                            ScopeType.Vectorscope -> if (landscape) "VECT" else "VECTOR"
                        }
                    val longPress: (() -> Unit)? =
                        when (scope) {
                            ScopeType.Waveform -> (
                                {
                                    haptics.selection()
                                    dispatch(ToggleWaveformMode)
                                }
                                )

                            ScopeType.Vectorscope -> null
                        }
                    SmallChoice(
                        label = label,
                        active = scope in state.activeScopes,
                        modifier = Modifier.fillMaxWidth().height(CaptureDimens.TopBarPaletteMinorChoiceHeight),
                        testTag = CaptureTestTags.scope(scope.name),
                        onLongClick = longPress
                    ) {
                        haptics.selection()
                        dispatch(ToggleScope(scope))
                    }
                }
            }
        }
    )
}

@Composable
private fun ArmChoice(
    label: String,
    armed: Boolean,
    modifier: Modifier,
    testTag: String? = null,
    onToggle: () -> Unit
) {
    val haptics = LocalCaptureHaptics.current
    SmallChoice(
        label = label,
        active = armed,
        modifier = modifier.height(CaptureDimens.TopBarPaletteMajorChoiceHeight),
        testTag = testTag
    ) {
        haptics.selection()
        onToggle()
    }
}
