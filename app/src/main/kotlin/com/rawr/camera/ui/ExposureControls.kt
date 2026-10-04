package com.rawr.camera.ui

import androidx.compose.animation.core.animateFloatAsState
import androidx.compose.animation.core.snap
import androidx.compose.animation.core.spring
import androidx.compose.foundation.Canvas
import androidx.compose.foundation.gestures.detectDragGestures
import androidx.compose.foundation.gestures.detectTapGestures
import androidx.compose.foundation.layout.*
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material3.Text
import androidx.compose.runtime.*
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.geometry.Offset
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.input.pointer.pointerInput
import androidx.compose.ui.platform.testTag
import androidx.compose.ui.semantics.contentDescription
import androidx.compose.ui.semantics.semantics
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import com.rawr.camera.architecture.CaptureDispatch
import com.rawr.camera.architecture.SetExposureCandidate
import com.rawr.camera.model.*

@Composable
internal fun ExposureArea(state: CaptureUiState, dispatch: CaptureDispatch) {
    val owners = state.exposureOwners()
    Row(Modifier.fillMaxWidth(), horizontalArrangement = Arrangement.spacedBy(CaptureDimens.ControlGap)) {
        owners.forEach { parameter ->
            ExposureScrub(parameter, state, dispatch, Modifier.weight(1f))
        }
    }
}

@Composable
private fun ExposureScrub(
    parameter: ExposureParameter,
    state: CaptureUiState,
    dispatch: CaptureDispatch,
    modifier: Modifier
) {
    val capability = state.capabilities.capabilityFor(parameter)
    val index = state.requestedCandidateIndexFor(parameter)
    val latestCommittedIndex by rememberUpdatedState(index)
    val latestRequestedId by rememberUpdatedState(state.requestedCandidateIdFor(parameter))
    val labels = capability.labelsAround(state.requestedCandidateIdFor(parameter))
    val title = parameter.displayLabel
    val haptics = LocalCaptureHaptics.current
    val isEv = parameter == ExposureParameter.Ev
    val zeroEvId = if (isEv) capability.zeroEvCandidateId() else null
    val zeroEvIndex = if (isEv && zeroEvId != null) capability.indexOf(zeroEvId).takeIf { it >= 0 } else null

    // Shutter/ISO use a dense backend grid so dragging feels continuous, while the
    // conventional 1/3-stop values act as magnetic detents. EV keeps the device's
    // native discrete compensation steps.
    var rawTrackOffsetPx by remember(parameter) { mutableFloatStateOf(0f) }
    var dragging by remember(parameter) { mutableStateOf(false) }
    var gestureIndex by remember(parameter) { mutableIntStateOf(index) }
    val animatedTrackOffsetPx by animateFloatAsState(
        targetValue = if (dragging) rawTrackOffsetPx else 0f,
        animationSpec = if (dragging) snap() else spring(dampingRatio = .78f, stiffness = 720f),
        label = "exposure-track-snap"
    )
    // Gesture direction and rail direction are intentionally opposite. The center index
    // is fixed: dragging right selects the next/brighter detent, so the numbered scale
    // must travel left underneath it (and vice versa). Keeping rawTrackOffsetPx in
    // gesture space makes detent accumulation/end-stop logic easy to reason about; only
    // the presentation translation is inverted here.
    val renderedTrackOffsetPx = -(if (dragging) rawTrackOffsetPx else animatedTrackOffsetPx)

    Box(
        modifier
            .height(CaptureDimens.ExposureControlHeight)
            .testTag(
                when (parameter) {
                    ExposureParameter.Shutter -> CaptureTestTags.EXPOSURE_SCRUB_SHUTTER
                    ExposureParameter.Iso -> CaptureTestTags.EXPOSURE_SCRUB_ISO
                    ExposureParameter.Ev -> CaptureTestTags.EXPOSURE_SCRUB_EV
                }
            )
            .clip(RoundedCornerShape(CaptureDimens.ExposureCornerRadius))
            .captureControlSurface(
                shape = RoundedCornerShape(CaptureDimens.ExposureCornerRadius),
                textureStrength = .9f
            ).then(
                if (isEv && zeroEvId != null && zeroEvIndex != null) {
                    Modifier.pointerInput(capability, zeroEvId) {
                        detectTapGestures(
                            onDoubleTap = {
                                // Double-tap resets EV to exactly +0.0. A separate tap
                                // detector coexists with the drag detector below: drag
                                // movement cancels the tap gesture, so a scrub never
                                // triggers a reset and a clean double-tap never starts
                                // a drag.
                                if (!dragging) {
                                    haptics.selection()
                                    gestureIndex = zeroEvIndex
                                    rawTrackOffsetPx = 0f
                                    if (latestRequestedId != zeroEvId) {
                                        dispatch(SetExposureCandidate(parameter, zeroEvId))
                                    }
                                }
                            }
                        )
                    }
                } else {
                    Modifier
                }
            ).pointerInput(state.orientation, parameter, capability.candidates.size) {
                val detentPx = CaptureDimens.ExposureDetentSpacing.toPx()
                // Shutter/ISO use a dense 1/24-EV backend grid. One conventional
                // 1/3-stop interval therefore spans eight fine candidates. EV remains
                // on its device-advertised discrete grid and keeps the original spacing.
                val magnetic = capability.magneticSnapAnchorIds.isNotEmpty()
                val fineStepPx = if (magnetic) detentPx / 8f else detentPx
                val magneticReleasePx = if (magnetic) fineStepPx * 2.25f else fineStepPx
                detectDragGestures(
                    onDragStart = {
                        dragging = true
                        // pointerInput intentionally does not restart for every committed detent.
                        // Read the latest committed value at the beginning of each new gesture
                        // so lift -> re-touch never jumps back to a stale index.
                        gestureIndex = latestCommittedIndex
                        rawTrackOffsetPx = 0f
                    },
                    onDrag = { change, drag ->
                        change.consume()
                        // The Activity remains portrait locked. In physical landscape the
                        // supported "up" gesture maps onto this local horizontal axis.
                        val delta = drag.x
                        var residual = rawTrackOffsetPx + delta

                        while (gestureIndex < capability.candidates.lastIndex) {
                            val currentIsMagnetic =
                                capability.candidates[gestureIndex].id in capability.magneticSnapAnchorIds
                            val threshold = if (currentIsMagnetic) magneticReleasePx else fineStepPx
                            if (residual < threshold) break
                            gestureIndex += 1
                            val candidate = capability.candidates[gestureIndex]
                            dispatch(SetExposureCandidate(parameter, candidate.id))
                            if (!magnetic || candidate.id in capability.magneticSnapAnchorIds) haptics.detent()
                            residual -= threshold
                        }
                        while (gestureIndex > 0) {
                            val currentIsMagnetic =
                                capability.candidates[gestureIndex].id in capability.magneticSnapAnchorIds
                            val threshold = if (currentIsMagnetic) magneticReleasePx else fineStepPx
                            if (residual > -threshold) break
                            gestureIndex -= 1
                            val candidate = capability.candidates[gestureIndex]
                            dispatch(SetExposureCandidate(parameter, candidate.id))
                            if (!magnetic || candidate.id in capability.magneticSnapAnchorIds) haptics.detent()
                            residual += threshold
                        }

                        // A real mechanical control gets firm at its stop. Keep just enough
                        // elastic travel to communicate the boundary, then resist strongly.
                        val pushingPastMin = gestureIndex == 0 && residual < 0f
                        val pushingPastMax = gestureIndex == capability.candidates.lastIndex && residual > 0f
                        rawTrackOffsetPx = if (pushingPastMin || pushingPastMax) residual * .18f else residual
                    },
                    onDragEnd = {
                        dragging = false
                        rawTrackOffsetPx = 0f
                    },
                    onDragCancel = {
                        dragging = false
                        rawTrackOffsetPx = 0f
                    }
                )
            }.semantics {
                contentDescription =
                    "$title scrub. ${labels.current}. Previous anchor ${labels.previousAnchor}. Next anchor ${labels.nextAnchor}. ${if (state.orientation == Orientation.Portrait) "Right" else "Up"} is brighter." +
                        if (isEv) " Double tap to reset." else ""
            }
    ) {
        ReferenceExposureRail(
            title = title,
            current = labels.current,
            previous = labels.previousAnchor,
            next = labels.nextAnchor,
            atMinimum = index == 0,
            atMaximum = index == capability.candidates.lastIndex,
            trackOffsetPx = renderedTrackOffsetPx,
            modifier = Modifier.matchParentSize()
        )
    }
}

@Composable
internal fun ReferenceExposureRail(
    title: String,
    current: String,
    previous: String,
    next: String,
    atMinimum: Boolean,
    atMaximum: Boolean,
    trackOffsetPx: Float,
    modifier: Modifier = Modifier
) {
    Box(modifier) {
        ExposureRailMarks(
            atMinimum = atMinimum,
            atMaximum = atMaximum,
            trackOffsetPx = trackOffsetPx,
            modifier = Modifier.matchParentSize()
        )

        // The reference leaves a genuine visual void in the middle of the tick rail.
        // The parameter and selected value sit in that void rather than being layered
        // on top of a continuous slider line.
        Column(
            modifier =
                Modifier
                    .align(Alignment.Center)
                    .width(CaptureDimens.ExposureCenterGap),
            horizontalAlignment = Alignment.CenterHorizontally,
            verticalArrangement = Arrangement.Center
        ) {
            Text(
                title,
                color = Color.White.copy(alpha = .66f),
                fontFamily = CaptureMono,
                fontWeight = FontWeight.Medium,
                fontSize = 7.sp,
                lineHeight = 7.sp,
                letterSpacing = .35.sp
            )
            Text(
                current,
                color = Color.White.copy(alpha = .98f),
                fontFamily = CaptureMono,
                fontWeight = FontWeight.SemiBold,
                fontSize = 13.sp,
                lineHeight = 13.sp,
                maxLines = 1
            )
        }

        Text(
            previous,
            color = Color.White.copy(alpha = .52f),
            fontFamily = CaptureMono,
            fontSize = 8.sp,
            lineHeight = 9.sp,
            modifier =
                Modifier
                    .align(Alignment.BottomStart)
                    .padding(start = CaptureDimens.ExposureHorizontalInset, bottom = 2.dp)
        )
        Text(
            next,
            color = Color.White.copy(alpha = .52f),
            fontFamily = CaptureMono,
            fontSize = 8.sp,
            lineHeight = 9.sp,
            modifier =
                Modifier
                    .align(Alignment.BottomEnd)
                    .padding(end = CaptureDimens.ExposureHorizontalInset, bottom = 2.dp)
        )
    }
}

@Composable
internal fun ExposureRailMarks(atMinimum: Boolean, atMaximum: Boolean, trackOffsetPx: Float, modifier: Modifier) {
    Canvas(modifier) {
        val inset = CaptureDimens.ExposureHorizontalInset.toPx()
        val left = inset
        val right = size.width - inset
        val center = size.width / 2f
        val halfGap = CaptureDimens.ExposureCenterGap.toPx() / 2f
        val gapLeft = center - halfGap
        val gapRight = center + halfGap
        val railCenterY = CaptureDimens.ExposureRailCenterY.toPx()
        val detent = CaptureDimens.ExposureDetentSpacing.toPx()
        val minor = detent / 3f

        fun drawSegment(from: Float, to: Float, skip: Boolean) {
            if (skip || to <= from) return
            // Draw a denser mechanical scale that translates continuously with the drag.
            // Major ticks land at each discrete capability detent; minor ticks interpolate.
            var x = center + trackOffsetPx
            var n = 0
            while (x > from - detent) {
                x -= minor
                n--
            }
            while (x < to + detent) {
                if (x in from..to) {
                    val major = n % 3 == 0
                    val h =
                        (if (major) CaptureDimens.ExposureMajorTickHeight else CaptureDimens.ExposureMinorTickHeight)
                            .toPx()
                    drawRoundRect(
                        color = Color.White.copy(alpha = if (major) .72f else .38f),
                        topLeft = Offset(x - .45.dp.toPx(), railCenterY - h / 2f),
                        size =
                            androidx.compose.ui.geometry
                                .Size(.9.dp.toPx(), h),
                        cornerRadius =
                            androidx.compose.ui.geometry
                                .CornerRadius(.45.dp.toPx())
                    )
                }
                x += minor
                n++
            }
        }

        // Capability endpoints are properties of the committed value, not of the
        // transient drag offset. Keep the impossible side completely absent for the
        // whole gesture while the committed index is at min/max. Only after the user
        // crosses a real detent and commits the adjacent capability value may that side
        // of the rail appear. This also prevents touch-slop/jitter on pointer-down from
        // flashing phantom ticks at an endpoint.
        drawSegment(left, gapLeft, skip = atMinimum)
        drawSegment(gapRight, right, skip = atMaximum)

        val markerWidth = 1.4.dp.toPx()
        val markerColor = CaptureColors.Accent.copy(alpha = .96f)
        val topStart = CaptureDimens.ExposureIndexTop.toPx()
        val topEnd = CaptureDimens.ExposureIndexTopEnd.toPx()
        val bottomStart = CaptureDimens.ExposureIndexBottomStart.toPx()
        val bottomEnd = CaptureDimens.ExposureIndexBottom.toPx()

        // Split center index from the reference: the selected label/value owns the empty
        // center gap, with a short warm index entering from above and below.
        drawRoundRect(
            color = markerColor,
            topLeft = Offset(center - markerWidth / 2f, topStart),
            size =
                androidx.compose.ui.geometry
                    .Size(markerWidth, topEnd - topStart),
            cornerRadius =
                androidx.compose.ui.geometry
                    .CornerRadius(markerWidth / 2f)
        )
        drawRoundRect(
            color = markerColor,
            topLeft = Offset(center - markerWidth / 2f, bottomStart),
            size =
                androidx.compose.ui.geometry
                    .Size(markerWidth, bottomEnd - bottomStart),
            cornerRadius =
                androidx.compose.ui.geometry
                    .CornerRadius(markerWidth / 2f)
        )
    }
}
