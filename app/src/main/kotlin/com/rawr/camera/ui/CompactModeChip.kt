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
    val engaged = mode != ExposureMode.Auto
    val label = mode.chipLabel
    fun step() {
        val next = nextExposureMode(latestMode, latestSupported)
        if (next == latestMode) return
        haptics.selection()
        dispatch(SetExposureMode(next))
    }
    Box(
        modifier
            .testTag(CaptureTestTags.COMPACT_MODE)
            .systemGestureExclusion()
            .pointerInput(Unit) { detectTapGestures(onTap = { step() }) }
            .semantics {
                role = Role.Button
                contentDescription = "Exposure mode $label. Tap to change."
                onClick {
                    step()
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
                label,
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
                "MODE",
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
    val engaged = mode != FocusMode.Af
    fun toggle() {
        haptics.selection()
        dispatch(if (latestSelectorOpen) CloseFocusSelector else OpenFocusSelector)
    }
    Box(
        modifier
            .testTag(CaptureTestTags.COMPACT_FOCUS)
            .systemGestureExclusion()
            .pointerInput(Unit) { detectTapGestures(onTap = { toggle() }) }
            .semantics {
                role = Role.Button
                contentDescription = "Focus mode $label. Tap to open focus controls."
                onClick {
                    toggle()
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
                label,
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
                "FOCUS",
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
