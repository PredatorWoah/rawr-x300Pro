package com.rawr.camera.ui

import androidx.compose.foundation.Canvas
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.shape.CircleShape
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.geometry.Offset
import androidx.compose.ui.geometry.Size
import androidx.compose.ui.graphics.Brush
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.platform.testTag
import androidx.compose.ui.semantics.contentDescription
import androidx.compose.ui.semantics.semantics
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import com.rawr.camera.model.GridMode
import com.rawr.camera.model.Orientation
import com.rawr.camera.model.OverlayMode

/**
 * Pure viewfinder overlay renderables extracted from CaptureViewfinder.
 *
 * Placeholder fixture and grid/monitoring overlays. Gesture handling, panel
 * placement and state mapping stay in CaptureViewfinder.
 */

@Composable
internal fun ViewfinderFixture(modifier: Modifier) {
    Canvas(modifier) {
        drawRect(
            Brush.linearGradient(
                colors =
                    listOf(
                        Color(0xFF111820),
                        Color(0xFF24313B),
                        Color(0xFF56616A),
                        Color(0xFF20282E),
                        Color(0xFF101317)
                    ),
                start = Offset.Zero,
                end = Offset(size.width, size.height)
            )
        )
        drawCircle(
            color = Color(0xFF59636C),
            radius = size.minDimension * .10f,
            center = Offset(size.width * .68f, size.height * .28f)
        )
        drawOval(
            brush =
                Brush.radialGradient(
                    colors = listOf(Color.Transparent, Color.Black.copy(alpha = .42f)),
                    center = Offset(size.width / 2f, size.height / 2f),
                    radius = size.maxDimension * .62f
                ),
            topLeft = Offset.Zero,
            size = Size(size.width, size.height)
        )
    }
}

@Composable
internal fun GridOverlay(mode: GridMode, modifier: Modifier) {
    if (mode == GridMode.Off) return
    Canvas(modifier) {
        val color = Color.White.copy(alpha = .20f)
        when (mode) {
            GridMode.Thirds -> {
                listOf(1f / 3f, 2f / 3f).forEach { fraction ->
                    drawLine(color, Offset(size.width * fraction, 0f), Offset(size.width * fraction, size.height), 1f)
                    drawLine(color, Offset(0f, size.height * fraction), Offset(size.width, size.height * fraction), 1f)
                }
            }

            GridMode.FourByFour -> {
                (1..3).forEach { index ->
                    val fraction = index / 4f
                    drawLine(
                        color.copy(alpha = .15f),
                        Offset(size.width * fraction, 0f),
                        Offset(size.width * fraction, size.height),
                        1f
                    )
                    drawLine(
                        color.copy(alpha = .15f),
                        Offset(0f, size.height * fraction),
                        Offset(
                            size.width,
                            size.height * fraction
                        ),
                        1f
                    )
                }
            }

            GridMode.Cross -> {
                drawLine(
                    color,
                    Offset(size.width / 2f, size.height * .2f),
                    Offset(size.width / 2f, size.height * .8f),
                    1f
                )
                drawLine(
                    color,
                    Offset(size.width * .2f, size.height / 2f),
                    Offset(size.width * .8f, size.height / 2f),
                    1f
                )
            }

            GridMode.Off -> {
                Unit
            }
        }
    }
}

@Composable
internal fun MonitoringOverlay(armed: Set<OverlayMode>, falseColor: Boolean, modifier: Modifier) {
    if (armed.isEmpty() && !falseColor) return
    Canvas(modifier) {
        if (OverlayMode.Peaking in armed) {
            for (i in -size.height.toInt()..size.width.toInt() step 42) {
                drawLine(
                    CaptureColors.Accent.copy(alpha = .38f),
                    Offset(i.toFloat(), 0f),
                    Offset(i + size.height, size.height),
                    2f
                )
            }
        }

        if (OverlayMode.RawHighlights in armed) {
            drawCircle(
                CaptureColors.Danger.copy(alpha = .52f),
                size.minDimension * .07f,
                Offset(size.width * .7f, size.height * .23f)
            )
        }

        if (OverlayMode.TonemapShadows in armed) {
            // B/W diagonal zebra placeholder for the tonemap-shadow overlay.
            for (i in -size.height.toInt()..size.width.toInt() step 24) {
                drawLine(
                    Color.White.copy(alpha = .30f),
                    Offset(i.toFloat(), 0f),
                    Offset(i + size.height, size.height),
                    6f
                )
            }
            drawRect(Color.Black.copy(alpha = .18f))
        }

        if (falseColor) {
            drawRect(Color.Blue.copy(alpha = .16f))
            drawCircle(
                Color.Green.copy(alpha = .18f),
                size.minDimension * .35f,
                Offset(
                    size.width * .4f,
                    size.height * .55f
                )
            )
            drawCircle(
                Color.Red.copy(alpha = .18f),
                size.minDimension * .28f,
                Offset(
                    size.width * .76f,
                    size.height * .35f
                )
            )
        }
    }
}

/**
 * Centered self-timer countdown readout. Covers the viewfinder so taps
 * cancel the run instead of moving focus; palettes and the bottom slot stay
 * above/below via z-order at the call site.
 */
@Composable
internal fun SelfTimerCountdownOverlay(
    remainingMs: Long,
    orientation: Orientation,
    onCancel: () -> Unit,
    modifier: Modifier = Modifier
) {
    val seconds = ((remainingMs + 999) / 1000).toInt().coerceAtLeast(1)
    Box(
        modifier
            .fillMaxSize()
            .testTag(CaptureTestTags.SELF_TIMER_COUNTDOWN)
            .captureClickable(shape = CircleShape, onClick = onCancel)
            .semantics {
                contentDescription = "Self timer $seconds seconds remaining. Tap to cancel."
            },
        contentAlignment = Alignment.Center
    ) {
        Text(
            "$seconds",
            color = Color.White,
            fontFamily = CaptureMono,
            fontWeight = FontWeight.Bold,
            fontSize = 72.sp,
            maxLines = 1,
            style = ViewfinderTextStyle,
            modifier = Modifier.uprightInLandscape(orientation)
        )
    }
}
