package com.rawr.camera.ui

import androidx.compose.foundation.background
import androidx.compose.foundation.border
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.shape.CircleShape
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.platform.testTag
import androidx.compose.ui.semantics.Role
import androidx.compose.ui.semantics.contentDescription
import androidx.compose.ui.semantics.role
import androidx.compose.ui.semantics.semantics
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import com.rawr.camera.model.Orientation

typealias VideoControlState = com.rawr.camera.model.VideoControlState
typealias VideoTimingState = com.rawr.camera.model.VideoTimingState

/**
 * Record shutter at exactly the photo shutter size ([CaptureDimens.ShutterOuter]):
 * red ring + red dot idle, red ring + white STOP square while recording. The
 * running timer lives in the slot-top overlay ([VideoRecordTimer]), never in
 * this row, so photo/video rows measure identically.
 */
@Composable
internal fun VideoRecordButton(
    state: VideoControlState,
    orientation: Orientation,
    onClick: () -> Unit,
    modifier: Modifier = Modifier,
    countingDown: Boolean = false
) {
    val haptics = LocalCaptureHaptics.current
    val ring = if (state.busy) CaptureColors.Muted else CaptureColors.Danger
    Box(
        modifier
            .size(CaptureDimens.ShutterOuter)
            .testTag(CaptureTestTags.VIDEO_RECORD)
            .captureClickable(shape = CircleShape, enabled = !state.busy, onClick = {
                haptics.capture()
                onClick()
            })
            .semantics {
                role = Role.Button
                contentDescription = when {
                    countingDown -> "Cancel self timer"
                    state.recording -> "Stop video recording"
                    else -> "Start video recording"
                }
            },
        contentAlignment = Alignment.Center
    ) {
        Box(
            Modifier.size(CaptureDimens.ShutterOuter).border(2.dp, ring, CircleShape),
            contentAlignment = Alignment.Center
        ) {
            if (state.recording) {
                Box(
                    Modifier.size(CaptureDimens.VideoStopInner)
                        .background(Color.White, RoundedCornerShape(5.dp))
                )
            } else {
                Box(
                    Modifier.size(CaptureDimens.ShutterInner).background(ring, CircleShape)
                )
            }
        }
    }
}

/**
 * Floating `REC mm:ss` readout pinned to the viewfinder slot top while
 * recording. Kept out of the shutter row so photo/video rows stay the same
 * height; drop-shadowed like other viewfinder glyphs.
 */
@Composable
internal fun VideoRecordTimer(
    status: String,
    dropped: Long?,
    orientation: Orientation,
    modifier: Modifier = Modifier
) {
    Row(
        modifier
            .testTag(CaptureTestTags.VIDEO_RECORD_TIMER)
            .semantics { contentDescription = "Video recording $status" }
            .padding(horizontal = 12.dp, vertical = 4.dp),
        horizontalArrangement = Arrangement.spacedBy(6.dp, Alignment.CenterHorizontally),
        verticalAlignment = Alignment.CenterVertically
    ) {
        Text(
            dropped?.let { "DROP $it" } ?: "DROP —",
            color = CaptureColors.Danger,
            fontFamily = CaptureMono,
            fontWeight = FontWeight.Bold,
            fontSize = 11.sp,
            maxLines = 1,
            style = com.rawr.camera.ui.ViewfinderTextStyle,
            modifier = Modifier.testTag(CaptureTestTags.VIDEO_RECORD_DROPS)
        )
        Box(Modifier.size(8.dp).background(CaptureColors.Danger, CircleShape))
        Text(
            status,
            color = Color.White,
            fontFamily = CaptureMono,
            fontWeight = FontWeight.Bold,
            fontSize = 11.sp,
            maxLines = 1,
            style = com.rawr.camera.ui.ViewfinderTextStyle
        )
    }
}
