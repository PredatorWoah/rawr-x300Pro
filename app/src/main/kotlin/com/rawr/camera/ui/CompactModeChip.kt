package com.rawr.camera.ui

import androidx.compose.foundation.gestures.detectTapGestures
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.systemGestureExclusion
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.rememberUpdatedState
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.input.pointer.pointerInput
import androidx.compose.ui.platform.testTag
import androidx.compose.ui.semantics.Role
import androidx.compose.ui.semantics.contentDescription
import androidx.compose.ui.semantics.onClick
import androidx.compose.ui.semantics.role
import androidx.compose.ui.semantics.semantics
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import com.rawr.camera.architecture.CaptureDispatch
import com.rawr.camera.architecture.CloseFocusSelector
import com.rawr.camera.architecture.OpenFocusSelector
import com.rawr.camera.architecture.SetExposureMode
import com.rawr.camera.model.CaptureUiState
import com.rawr.camera.model.ExposureMode
import com.rawr.camera.model.FocusMode
import com.rawr.camera.model.chipLabel
import com.rawr.camera.model.nextExposureMode

/**
 * A tappable two-line cell in the compact param row: the current value above, a small title below. Same visual
 * language as the scrubbable SS / ISO / EV buttons, and accent-coloured while [engaged].
 */
@Composable
internal fun CompactTextChip(
    value: String,
    title: String,
    engaged: Boolean,
    testTag: String,
    description: String,
    onClick: () -> Unit,
    modifier: Modifier = Modifier
) {
    val latestOnClick by rememberUpdatedState(onClick)
    Box(
        modifier
            .testTag(testTag)
            .systemGestureExclusion()
            .pointerInput(Unit) { detectTapGestures(onTap = { latestOnClick() }) }
            .semantics {
                role = Role.Button
                contentDescription = description
                onClick {
                    latestOnClick()
                    true
                }
            },
        contentAlignment = Alignment.Center
    ) {
        Column(
            horizontalAlignment = Alignment.CenterHorizontally,
            verticalArrangement = Arrangement.Center,
            modifier = Modifier.padding(horizontal = 4.dp)
        ) {
            Text(
                value,
                color = if (engaged) CaptureColors.AccentSoft else Color.White.copy(alpha = .95f),
                fontFamily = CaptureMono,
                fontWeight = FontWeight.SemiBold,
                fontSize = 10.sp,
                lineHeight = 12.sp,
                maxLines = 1,
                overflow = TextOverflow.Ellipsis,
                style = ViewfinderTextStyle
            )
            Text(
                title,
                color = if (engaged) CaptureColors.Accent else Color.White.copy(alpha = .62f),
                fontFamily = CaptureMono,
                fontWeight = FontWeight.Bold,
                fontSize = 8.sp,
                lineHeight = 10.sp,
                letterSpacing = .4.sp,
                maxLines = 1,
                style = ViewfinderTextStyle
            )
        }
    }
}

/**
 * Shows the exposure mode in the compact row and steps through it on tap: Auto, shutter priority, ISO priority,
 * Manual. Compact otherwise has no visible mode at all, only long-press and double-tap gestures on SS and ISO, which
 * are invisible until discovered and silently do nothing on a camera that lacks a mode.
 */
@Composable
internal fun CompactModeChip(state: CaptureUiState, dispatch: CaptureDispatch, modifier: Modifier = Modifier) {
    val haptics = LocalCaptureHaptics.current
    val mode = state.exposureControl.mode
    val latestMode by rememberUpdatedState(mode)
    val latestSupported by rememberUpdatedState(state.capabilities.supportedExposureModes)
    CompactTextChip(
        value = mode.chipLabel,
        title = "MODE",
        engaged = mode != ExposureMode.Auto,
        testTag = CaptureTestTags.COMPACT_MODE,
        description = "Exposure mode ${mode.chipLabel}. Tap to change.",
        onClick = {
            val next = nextExposureMode(latestMode, latestSupported)
            if (next != latestMode) {
                haptics.selection()
                dispatch(SetExposureMode(next))
            }
        },
        modifier = modifier
    )
}

/**
 * Shows the focus mode (AF, AF LOCK, MF) and opens the focus controls on tap. Those controls, the mode buttons and the
 * manual focus rail, were reachable only by long-pressing the preview, so most people never found manual focus.
 */
@Composable
internal fun CompactFocusChip(state: CaptureUiState, dispatch: CaptureDispatch, modifier: Modifier = Modifier) {
    val haptics = LocalCaptureHaptics.current
    val mode = state.focus.mode
    val latestSelectorOpen by rememberUpdatedState(state.focus.selectorOpen)
    val label =
        when (mode) {
            FocusMode.Af -> "AF"
            FocusMode.AfLock -> "AF LOCK"
            FocusMode.Mf -> "MF"
        }
    CompactTextChip(
        value = label,
        title = "FOCUS",
        engaged = mode != FocusMode.Af,
        testTag = CaptureTestTags.COMPACT_FOCUS,
        description = "Focus mode $label. Tap to open focus controls.",
        onClick = {
            haptics.selection()
            dispatch(if (latestSelectorOpen) CloseFocusSelector else OpenFocusSelector)
        },
        modifier = modifier
    )
}
