package com.rawr.camera.ui

import androidx.compose.foundation.background
import androidx.compose.foundation.gestures.detectTapGestures
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxHeight
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.input.pointer.pointerInput
import androidx.compose.ui.platform.testTag
import androidx.compose.ui.semantics.Role
import androidx.compose.ui.semantics.contentDescription
import androidx.compose.ui.semantics.disabled
import androidx.compose.ui.semantics.onClick
import androidx.compose.ui.semantics.role
import androidx.compose.ui.semantics.selected
import androidx.compose.ui.semantics.semantics
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import com.rawr.camera.architecture.CaptureDispatch
import com.rawr.camera.architecture.SetExposureMode
import com.rawr.camera.model.CaptureUiState
import com.rawr.camera.model.ExposureMode

/**
 * Classic (V1) viewfinder-bottom controls.
 *
 * The regular tonemap sliders belong to the capture strip / Settings Tone
 * group, so this row only carries the exposure-mode selector; the freed half
 * keeps the AMSI group the same size as the compact layout.
 */
@Composable
internal fun QuickControlsRow(state: CaptureUiState, dispatch: CaptureDispatch) {
    Row(
        Modifier.fillMaxWidth().height(CaptureDimens.QuickRowHeight),
        horizontalArrangement = Arrangement.spacedBy(CaptureDimens.ControlGap)
    ) {
        ExposureModeSelector(
            selected = state.exposureControl.mode,
            supported = state.capabilities.supportedExposureModes,
            onSelect = { dispatch(SetExposureMode(it)) },
            modifier = Modifier.weight(1f).fillMaxHeight()
        )
        Spacer(Modifier.weight(1f))
    }
}

@Composable
private fun ExposureModeSelector(
    selected: ExposureMode,
    supported: Set<ExposureMode>,
    onSelect: (ExposureMode) -> Unit,
    modifier: Modifier = Modifier
) {
    val haptics = LocalCaptureHaptics.current
    val shape = RoundedCornerShape(CaptureDimens.GlassControlRadius)
    Row(
        modifier
            .testTag(CaptureTestTags.EXPOSURE_MODE_BAR)
            .clip(shape)
            .captureControlSurface(shape = shape, textureStrength = .75f),
        verticalAlignment = Alignment.CenterVertically
    ) {
        ExposureMode.entries.forEachIndexed { i, mode ->
            val enabled = mode in supported
            Box(
                Modifier
                    .weight(1f)
                    .fillMaxHeight()
                    .testTag(CaptureTestTags.exposureMode(mode.name))
                    .then(
                        if (mode == selected) {
                            Modifier.background(
                                CaptureColors.Accent.copy(alpha = .075f)
                            )
                        } else {
                            Modifier
                        }
                    ).then(
                        if (enabled) {
                            Modifier.pointerInput(mode) {
                                detectTapGestures {
                                    haptics.selection()
                                    onSelect(mode)
                                }
                            }
                        } else {
                            Modifier
                        }
                    ).semantics {
                        contentDescription = "Exposure mode ${mode.name}"
                        role = Role.RadioButton
                        this.selected = mode == selected
                        if (!enabled) disabled()
                        onClick {
                            if (!enabled) return@onClick false
                            haptics.selection()
                            onSelect(mode)
                            true
                        }
                    },
                contentAlignment = Alignment.Center
            ) {
                Text(
                    mode.shortLabel,
                    color =
                        when {
                            mode == selected -> CaptureColors.Accent
                            enabled -> Color.White.copy(alpha = .48f)
                            else -> Color.White.copy(alpha = .16f)
                        },
                    fontFamily = CaptureMono,
                    fontWeight = FontWeight.SemiBold,
                    fontSize = 10.sp
                )
            }
            if (i != ExposureMode.entries.lastIndex) {
                Box(
                    Modifier
                        .width(.5.dp)
                        .fillMaxHeight(.48f)
                        .background(Color.White.copy(alpha = .055f))
                )
            }
        }
    }
}
