package com.rawr.camera.ui

import androidx.compose.foundation.gestures.detectDragGestures
import androidx.compose.foundation.gestures.detectTapGestures
import com.rawr.camera.ui.icons.Icons
import com.rawr.camera.ui.icons.outlined.Thermostat
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableFloatStateOf
import androidx.compose.runtime.mutableIntStateOf
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.rememberUpdatedState
import androidx.compose.runtime.setValue
import androidx.compose.ui.Modifier
import androidx.compose.ui.input.pointer.pointerInput
import androidx.compose.ui.platform.LocalDensity
import androidx.compose.ui.platform.LocalViewConfiguration
import androidx.compose.ui.platform.testTag
import androidx.compose.ui.unit.dp
import com.rawr.camera.architecture.CaptureDispatch
import com.rawr.camera.architecture.CaptureTransitions
import com.rawr.camera.architecture.LockWhiteBalance
import com.rawr.camera.architecture.SetWhiteBalanceMode
import com.rawr.camera.architecture.SetWhiteBalanceTempTint
import com.rawr.camera.model.CaptureUiState
import com.rawr.camera.model.WhiteBalanceMode

@Composable
internal fun CompactWbButton(state: CaptureUiState, dispatch: CaptureDispatch, modifier: Modifier = Modifier) {
    val haptics = LocalCaptureHaptics.current
    val manualSupported = state.capabilities.manualWhiteBalanceSupported
    val locked = CaptureTransitions.isWhiteBalanceLocked(state)
    // Tap callbacks below run inside pointerInput blocks keyed only on
    // capability flags, so plain vals would go stale across mode changes.
    // Always read the fresh value through these holders.
    val latestLocked by rememberUpdatedState(locked)
    val latestTemp by rememberUpdatedState(state.whiteBalanceTemperatureK)
    val latestTint by rememberUpdatedState(state.whiteBalanceTint)
    val tempText = "${state.whiteBalanceTemperatureK}K"
    val tintText = if (state.whiteBalanceTint >= 0) "+${state.whiteBalanceTint}" else "${state.whiteBalanceTint}"
    val valueText = "$tempText $tintText"
    val stepPxTemp = with(LocalDensity.current) { 5.dp.toPx() }
    val stepPxTint = with(LocalDensity.current) { 8.dp.toPx() }
    val touchSlopPx = LocalViewConfiguration.current.touchSlop
    var dragging by remember { mutableStateOf(false) }
    var gestureTemp by remember { mutableIntStateOf(state.whiteBalanceTemperatureK) }
    var gestureTint by remember { mutableIntStateOf(state.whiteBalanceTint) }
    var accumulatedTempPx by remember { mutableFloatStateOf(0f) }
    var accumulatedTintPx by remember { mutableFloatStateOf(0f) }
    // Axis lock: -1 = undecided, 0 = temperature, 1 = tint. The first axis to
    // clearly win owns the rest of the gesture, so diagonal finger wobble can
    // never bleed temperature into tint or vice versa.
    var lockedAxis by remember { mutableIntStateOf(-1) }
    var lastTempBucket by remember { mutableIntStateOf(latestTemp / 500) }
    var lastTintBucket by remember { mutableIntStateOf(latestTint / 5) }

    fun enterManualIfNeeded() {
        if (!manualSupported || latestLocked) return
        haptics.selection()
        dispatch(LockWhiteBalance(latestTemp, latestTint))
    }

    CompactParamShell(
        icon = Icons.Outlined.Thermostat,
        title = "WB",
        valueText = valueText,
        locked = locked,
        testTag = CaptureTestTags.COMPACT_WB,
        semanticsDescription =
            "White balance, $tempText tint $tintText, ${if (locked) "locked" else "auto"}. " +
                "Scrub horizontally for temperature, vertically for tint; the first direction wins. " +
                "Long press to lock, double tap for auto.",
        modifier = modifier
            .pointerInput(manualSupported) {
                detectTapGestures(
                    onDoubleTap = {
                        if (!dragging && latestLocked) {
                            haptics.selection()
                            dispatch(SetWhiteBalanceMode(WhiteBalanceMode.Auto))
                        }
                    },
                    onLongPress = {
                        if (!dragging && manualSupported && !latestLocked) {
                            haptics.longPress()
                            dispatch(LockWhiteBalance(latestTemp, latestTint))
                        } else if (!dragging) {
                            haptics.longPress()
                        }
                    }
                )
            }
            .then(
                if (manualSupported) {
                    Modifier.pointerInput(stepPxTemp, stepPxTint, touchSlopPx) {
                        detectDragGestures(
                            onDragStart = {
                                dragging = true
                                gestureTemp = latestTemp
                                gestureTint = latestTint
                                accumulatedTempPx = 0f
                                accumulatedTintPx = 0f
                                lockedAxis = -1
                                lastTempBucket = Math.floorDiv(gestureTemp, 500)
                                lastTintBucket = Math.floorDiv(gestureTint, 5)
                                enterManualIfNeeded()
                            },
                            onDrag = { change, drag ->
                                change.consume()
                                accumulatedTempPx += drag.x
                                accumulatedTintPx += -drag.y
                                if (lockedAxis == -1 &&
                                    maxOf(
                                        kotlin.math.abs(accumulatedTempPx),
                                        kotlin.math.abs(accumulatedTintPx)
                                    ) >= touchSlopPx
                                ) {
                                    // Dominant axis wins; drop the loser's
                                    // pending travel so it can't leak in later.
                                    if (kotlin.math.abs(accumulatedTempPx) >= kotlin.math.abs(accumulatedTintPx)) {
                                        lockedAxis = 0
                                        accumulatedTintPx = 0f
                                    } else {
                                        lockedAxis = 1
                                        accumulatedTempPx = 0f
                                    }
                                }
                                if (lockedAxis == 0) {
                                    var tempSteps = (accumulatedTempPx / stepPxTemp).toInt()
                                    while (tempSteps != 0) {
                                        val direction = if (tempSteps > 0) 1 else -1
                                        val next = (gestureTemp + direction * 25)
                                            .coerceIn(WhiteBalanceMode.TEMP_MIN_K, WhiteBalanceMode.TEMP_MAX_K)
                                        if (next == gestureTemp) break
                                        gestureTemp = next
                                        dispatch(SetWhiteBalanceTempTint(next, gestureTint))
                                        val bucket = Math.floorDiv(next, 500)
                                        if (bucket != lastTempBucket) {
                                            lastTempBucket = bucket
                                            haptics.detent()
                                        }
                                        accumulatedTempPx -= direction * stepPxTemp
                                        tempSteps -= direction
                                    }
                                }
                                if (lockedAxis == 1) {
                                    var tintSteps = (accumulatedTintPx / stepPxTint).toInt()
                                    while (tintSteps != 0) {
                                        val direction = if (tintSteps > 0) 1 else -1
                                        val next = (gestureTint + direction)
                                            .coerceIn(WhiteBalanceMode.TINT_MIN, WhiteBalanceMode.TINT_MAX)
                                        if (next == gestureTint) break
                                        gestureTint = next
                                        dispatch(SetWhiteBalanceTempTint(gestureTemp, next))
                                        val bucket = Math.floorDiv(next, 5)
                                        if (bucket != lastTintBucket) {
                                            lastTintBucket = bucket
                                            haptics.detent()
                                        }
                                        accumulatedTintPx -= direction * stepPxTint
                                        tintSteps -= direction
                                    }
                                }
                            },
                            onDragEnd = {
                                dragging = false
                                lockedAxis = -1
                                accumulatedTempPx = 0f
                                accumulatedTintPx = 0f
                            },
                            onDragCancel = {
                                dragging = false
                                lockedAxis = -1
                                accumulatedTempPx = 0f
                                accumulatedTintPx = 0f
                            }
                        )
                    }
                } else {
                    Modifier
                }
            )
    )
}
