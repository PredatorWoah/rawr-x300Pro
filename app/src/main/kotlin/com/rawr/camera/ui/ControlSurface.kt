package com.rawr.camera.ui

import androidx.compose.foundation.background
import androidx.compose.foundation.border
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.runtime.Composable
import androidx.compose.runtime.staticCompositionLocalOf
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.drawBehind
import androidx.compose.ui.geometry.Offset
import androidx.compose.ui.graphics.Brush
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.unit.dp
import com.rawr.camera.model.ControlSurfaceStyle

internal val LocalControlSurfaceStyle = staticCompositionLocalOf { ControlSurfaceStyle.Basic }

private fun smokedGlassBrush(active: Boolean): Brush = Brush.verticalGradient(
    listOf(
        if (active) CaptureColors.Accent.copy(alpha = .070f) else CaptureColors.GlassTop,
        if (active) CaptureColors.Accent.copy(alpha = .032f) else CaptureColors.GlassMiddle,
        CaptureColors.GlassBottom
    )
)

/** Deterministic blur-free micro-grain so the live preview is never sampled by the UI layer. */
private fun Modifier.smokedGlassTexture(strength: Float): Modifier = drawBehind {
    val step = 5.dp.toPx().coerceAtLeast(1f)
    val radius = .45.dp.toPx()
    var row = 0
    var y = step * .55f
    while (y < size.height) {
        var col = 0
        var x = step * .55f
        while (x < size.width) {
            var hash = (row * 73856093) xor (col * 19349663) xor 0x45d9f3b
            hash = hash xor (hash ushr 13)
            when (hash and 7) {
                0 -> drawCircle(Color.White.copy(alpha = .030f * strength), radius, Offset(x, y))
                3 -> drawCircle(Color.White.copy(alpha = .017f * strength), radius, Offset(x, y))
                6 -> drawCircle(Color.Black.copy(alpha = .035f * strength), radius, Offset(x, y))
            }
            x += step
            col++
        }
        y += step
        row++
    }
}

/** Shared material skin. Geometry and behavior must remain independent from the selected style. */
@Composable
internal fun Modifier.captureControlSurface(
    shape: RoundedCornerShape = RoundedCornerShape(CaptureDimens.GlassControlRadius),
    active: Boolean = false,
    textureStrength: Float = .75f
): Modifier = when (LocalControlSurfaceStyle.current) {
    ControlSurfaceStyle.Frosted -> {
        this
            .background(smokedGlassBrush(active), shape)
            .smokedGlassTexture(textureStrength)
            .border(
                width = if (active) .6.dp else .35.dp,
                color = if (active) CaptureColors.GlassBorderActive else Color.White.copy(alpha = .035f),
                shape = shape
            )
    }

    ControlSurfaceStyle.Basic -> {
        this
            .background(
                color = if (active) Color.Black.copy(alpha = .39f) else Color.Black.copy(alpha = .31f),
                shape = shape
            ).border(
                width = if (active) .55.dp else .35.dp,
                color = if (active) CaptureColors.Accent.copy(alpha = .24f) else Color.White.copy(alpha = .045f),
                shape = shape
            )
    }
}
