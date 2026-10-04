package com.rawr.camera.ui

import androidx.compose.animation.core.animateFloatAsState
import androidx.compose.animation.core.snap
import androidx.compose.animation.core.spring
import androidx.compose.foundation.gestures.detectDragGestures
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.foundation.verticalScroll
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableFloatStateOf
import androidx.compose.runtime.mutableIntStateOf
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.rememberUpdatedState
import androidx.compose.runtime.setValue
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.input.pointer.pointerInput
import androidx.compose.ui.platform.LocalDensity
import androidx.compose.ui.platform.testTag
import androidx.compose.ui.semantics.contentDescription
import androidx.compose.ui.semantics.semantics
import androidx.compose.ui.unit.Dp
import androidx.compose.ui.unit.dp
import com.rawr.camera.architecture.CaptureDispatch
import com.rawr.camera.architecture.SetWhiteBalanceMode
import com.rawr.camera.architecture.SetWhiteBalanceTempTint
import com.rawr.camera.model.CaptureUiState
import com.rawr.camera.model.Orientation
import com.rawr.camera.model.WhiteBalanceMode

private val WbBodyHeight = 88.dp

/** Compact top-bar white-balance controls: HAL presets + manual temp/tint rails. */
@Composable
internal fun WhiteBalancePanel(state: CaptureUiState, dispatch: CaptureDispatch, modifier: Modifier) {
    val haptics = LocalCaptureHaptics.current
    val landscape = state.orientation == Orientation.Landscape
    val otherPresets =
        WhiteBalanceMode.entries.filter {
            it != WhiteBalanceMode.Auto &&
                it != WhiteBalanceMode.ManualTempTint &&
                it in state.capabilities.supportedWhiteBalanceModes
        }.sortedBy(WhiteBalanceMode::awbValue)
    // MANUAL sits beside AUTO in the first row, remaining presets follow.
    val orderedModes =
        listOf(WhiteBalanceMode.Auto, WhiteBalanceMode.ManualTempTint) + otherPresets

    val inManual = state.whiteBalanceMode == WhiteBalanceMode.ManualTempTint
    // Live neutral readout on the header line while in Auto/preset. Mirrors
    // the rails' display state (the single live source fed by the snapshot
    // follow), so header, rails, compact strip and monitor can never drift
    // apart. Shown whenever a backend seed exists.
    val hasSeed = state.autoWhiteBalanceTemperatureK != null && state.autoWhiteBalanceTint != null
    val headerReadout =
        if (!inManual && hasSeed) {
            "· AUTO ≈ ${state.whiteBalanceTemperatureK}K ${formatTint(state.whiteBalanceTint)}"
        } else {
            null
        }

    CompactTopBarPalette(
        title = if (headerReadout != null) "WB $headerReadout" else "WB",
        orientation = state.orientation,
        modifier = modifier.testTag(CaptureTestTags.WB_PANEL),
        leadingWeight = .52f,
        trailingWeight = .48f,
        leading = {
            Column(
                modifier =
                    Modifier
                        .fillMaxSize()
                        .verticalScroll(rememberScrollState()),
                verticalArrangement = Arrangement.spacedBy(CaptureDimens.TopBarPaletteInnerGap)
            ) {
                orderedModes.chunked(2).forEach { row ->
                    Row(
                        Modifier.fillMaxWidth(),
                        horizontalArrangement = Arrangement.spacedBy(CaptureDimens.TopBarPaletteInnerGap)
                    ) {
                        row.forEach { mode ->
                            val active = state.whiteBalanceMode == mode
                            val enabled =
                                (mode == WhiteBalanceMode.Auto) ||
                                    (mode == WhiteBalanceMode.ManualTempTint &&
                                        state.capabilities.manualWhiteBalanceSupported) ||
                                    (mode in state.capabilities.supportedWhiteBalanceModes)
                            // NOTE: no orientation passed on purpose. The palette
                            // shell already rotates 90° in landscape (like the
                            // MONITORING panel); rotating here again would turn
                            // the text upside down.
                            SmallChoice(
                                label =
                                    when (mode) {
                                        WhiteBalanceMode.ManualTempTint ->
                                            if (landscape) {
                                                "MAN"
                                            } else {
                                                "MANUAL"
                                            }
                                        else -> if (landscape) mode.shortLabel else mode.label.uppercase()
                                    },
                                active = active,
                                enabled = enabled,
                                testTag = CaptureTestTags.whiteBalance(mode.name),
                                modifier =
                                    Modifier
                                        .weight(1f)
                                        .height(CaptureDimens.TopBarPaletteMinorChoiceHeight)
                                        .semantics {
                                            contentDescription =
                                                "White balance ${mode.label}" +
                                                    if (active) ", active" else ""
                                        }
                            ) {
                                haptics.selection()
                                if (mode == WhiteBalanceMode.ManualTempTint) {
                                    dispatch(
                                        SetWhiteBalanceTempTint(
                                            state.whiteBalanceTemperatureK,
                                            state.whiteBalanceTint
                                        )
                                    )
                                } else {
                                    dispatch(SetWhiteBalanceMode(mode))
                                }
                            }
                        }
                        if (row.size == 1) {
                            Spacer(Modifier.weight(1f))
                        }
                    }
                }
            }
        },
        trailing = {
            Column(
                modifier = Modifier.fillMaxSize(),
                verticalArrangement = Arrangement.spacedBy(CaptureDimens.TopBarPaletteInnerGap)
            ) {
                val manualEnabled = state.capabilities.manualWhiteBalanceSupported
                WbRail(
                    title = "TEMP",
                    current = "${state.whiteBalanceTemperatureK}K",
                    previous = previousTempText(state.whiteBalanceTemperatureK),
                    next = nextTempText(state.whiteBalanceTemperatureK),
                    enabled = manualEnabled,
                    semanticsDescription =
                        "White balance temperature, ${state.whiteBalanceTemperatureK} kelvin. Drag to adjust.",
                    value = state.whiteBalanceTemperatureK,
                    min = WhiteBalanceMode.TEMP_MIN_K,
                    max = WhiteBalanceMode.TEMP_MAX_K,
                    step = 25,
                    stepPx = 5.dp,
                    detentEvery = 500,
                    onStep = { dispatch(SetWhiteBalanceTempTint(it, state.whiteBalanceTint)) },
                    modifier =
                        Modifier
                            .fillMaxWidth()
                            .height(CaptureDimens.ExposureControlHeight)
                )
                WbRail(
                    title = "TINT",
                    current = formatTint(state.whiteBalanceTint),
                    previous = previousTintText(state.whiteBalanceTint),
                    next = nextTintText(state.whiteBalanceTint),
                    enabled = manualEnabled,
                    semanticsDescription =
                        "White balance tint, ${state.whiteBalanceTint}. Drag to adjust.",
                    value = state.whiteBalanceTint,
                    min = WhiteBalanceMode.TINT_MIN,
                    max = WhiteBalanceMode.TINT_MAX,
                    step = 1,
                    stepPx = 8.dp,
                    detentEvery = 5,
                    onStep = { dispatch(SetWhiteBalanceTempTint(state.whiteBalanceTemperatureK, it)) },
                    modifier =
                        Modifier
                            .fillMaxWidth()
                            .height(CaptureDimens.ExposureControlHeight)
                )
            }
        },
        bodyHeight = WbBodyHeight
    )
}

private fun formatTint(tint: Int): String = if (tint >= 0) "+$tint" else "$tint"

private fun previousTempText(tempK: Int): String =
    (tempK - 500).takeIf { it >= WhiteBalanceMode.TEMP_MIN_K }?.let { "${it}K" } ?: ""

private fun nextTempText(tempK: Int): String =
    (tempK + 500).takeIf { it <= WhiteBalanceMode.TEMP_MAX_K }?.let { "${it}K" } ?: ""

private fun previousTintText(tint: Int): String =
    (tint - 5).takeIf { it >= WhiteBalanceMode.TINT_MIN }?.let { formatTint(it) } ?: ""

private fun nextTintText(tint: Int): String =
    (tint + 5).takeIf { it <= WhiteBalanceMode.TINT_MAX }?.let { formatTint(it) } ?: ""

/**
 * Temp/tint scrub sharing the exact reference-rail presentation as exposure
 * ([ReferenceExposureRail]): same height, ticks, center index and anchor
 * labels. Drag-anywhere relative gesture, quantized steps, detent haptics,
 * elastic end-stops — no jump-to-finger.
 */
@Composable
private fun WbRail(
    title: String,
    current: String,
    previous: String,
    next: String,
    enabled: Boolean,
    semanticsDescription: String,
    value: Int,
    min: Int,
    max: Int,
    step: Int,
    stepPx: Dp,
    detentEvery: Int,
    onStep: (Int) -> Unit,
    modifier: Modifier = Modifier
) {
    val haptics = LocalCaptureHaptics.current
    val latestValue by rememberUpdatedState(value)
    val latestOnStep by rememberUpdatedState(onStep)
    val stepPxFloat = with(LocalDensity.current) { stepPx.toPx() }

    var dragging by remember { mutableStateOf(false) }
    var gestureValue by remember { mutableIntStateOf(value) }
    var accumulatedPx by remember { mutableFloatStateOf(0f) }
    var rawTrackOffsetPx by remember { mutableFloatStateOf(0f) }
    var lastHapticBucket by remember { mutableIntStateOf(Math.floorDiv(value, detentEvery)) }
    val animatedTrackOffsetPx by animateFloatAsState(
        targetValue = if (dragging) rawTrackOffsetPx else 0f,
        animationSpec = if (dragging) snap() else spring(dampingRatio = .78f, stiffness = 720f),
        label = "wb-track-snap"
    )
    // Backend truth arrives via snapshot; resync the gesture base when not
    // dragging so reopen/entry always continues from the live value.
    LaunchedEffect(value, dragging) {
        if (!dragging) {
            gestureValue = value
            lastHapticBucket = Math.floorDiv(value, detentEvery)
        }
    }

    fun bucketOf(v: Int) = Math.floorDiv(v, detentEvery)

    Box(
        modifier
            .testTag(CaptureTestTags.toneScrub("wb_$title"))
            .clip(RoundedCornerShape(CaptureDimens.ExposureCornerRadius))
            .captureControlSurface(
                shape = RoundedCornerShape(CaptureDimens.ExposureCornerRadius),
                textureStrength = .9f
            )
            .then(
                if (enabled) {
                    Modifier.pointerInput(min, max, step, stepPxFloat) {
                        detectDragGestures(
                            onDragStart = {
                                dragging = true
                                gestureValue = latestValue
                                accumulatedPx = 0f
                                rawTrackOffsetPx = 0f
                                lastHapticBucket = bucketOf(gestureValue)
                            },
                            onDrag = { change, drag ->
                                change.consume()
                                accumulatedPx += drag.x
                                var residual = accumulatedPx
                                var steps = (residual / stepPxFloat).toInt()
                                while (steps != 0) {
                                    val direction = if (steps > 0) 1 else -1
                                    val nextValue = (gestureValue + direction * step).coerceIn(min, max)
                                    if (nextValue == gestureValue) break
                                    gestureValue = nextValue
                                    latestOnStep(nextValue)
                                    val bucket = bucketOf(nextValue)
                                    if (bucket != lastHapticBucket) {
                                        lastHapticBucket = bucket
                                        haptics.detent()
                                    }
                                    residual -= direction * stepPxFloat
                                    steps -= direction
                                }
                                // Firm stops with a hint of elastic travel.
                                val pushingPastMin = gestureValue == min && residual < 0f
                                val pushingPastMax = gestureValue == max && residual > 0f
                                rawTrackOffsetPx =
                                    if (pushingPastMin || pushingPastMax) residual * .18f else residual
                                accumulatedPx = residual
                            },
                            onDragEnd = {
                                dragging = false
                                rawTrackOffsetPx = 0f
                                accumulatedPx = 0f
                            },
                            onDragCancel = {
                                dragging = false
                                rawTrackOffsetPx = 0f
                                accumulatedPx = 0f
                            }
                        )
                    }
                } else {
                    Modifier
                }
            )
            .semantics { contentDescription = semanticsDescription }
    ) {
        ReferenceExposureRail(
            title = title,
            current = current,
            previous = previous,
            next = next,
            atMinimum = value <= min,
            atMaximum = value >= max,
            trackOffsetPx = -(if (dragging) rawTrackOffsetPx else animatedTrackOffsetPx),
            modifier = Modifier.matchParentSize()
        )
    }
}
