package com.rawr.camera.ui

import androidx.compose.animation.core.animateFloatAsState
import androidx.compose.animation.core.tween
import androidx.compose.foundation.ExperimentalFoundationApi
import androidx.compose.foundation.combinedClickable
import androidx.compose.foundation.interaction.MutableInteractionSource
import androidx.compose.foundation.interaction.collectIsPressedAsState
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material3.ripple
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.remember
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.graphics.Shape
import androidx.compose.ui.graphics.graphicsLayer
import androidx.compose.ui.semantics.Role
import androidx.compose.ui.unit.dp

/**
 * Shared press feedback for capture-screen tappables.
 *
 * Bare `Modifier.clickable` renders a square bounded ripple sized to the
 * layout Box, which reads as an ugly square flash on circular buttons and
 * bare text cells. Every helper here clips the ripple to the visible shape
 * and adds a subtle press-dim, so feedback follows the geometry:
 * circle stays circular, pill stays rounded.
 *
 * Order inside: `clip(shape)` first so the ripple is bounded by the shape,
 * then press-dim, then the clickable with an explicit bounded M3 ripple.
 */

/** Clipped-ripple shape for bare viewfinder text cells (lens/mode/strip items). */
internal val CaptureTextRippleShape: Shape = RoundedCornerShape(8.dp)

/** Clipped-ripple shape for the MERGED/FILM side pills. */
internal val CapturePillRippleShape: Shape = RoundedCornerShape(CaptureDimens.CompactParamRadius)

@Composable
internal fun Modifier.capturePressDim(
    interactionSource: MutableInteractionSource,
    pressedAlpha: Float = .6f
): Modifier {
    val pressed by interactionSource.collectIsPressedAsState()
    val alpha by animateFloatAsState(
        targetValue = if (pressed) pressedAlpha else 1f,
        animationSpec = tween(durationMillis = 90),
        label = "capturePressDim"
    )
    return graphicsLayer { this.alpha = alpha }
}

@OptIn(ExperimentalFoundationApi::class)
@Composable
internal fun Modifier.captureClickable(
    shape: Shape,
    enabled: Boolean = true,
    onClickLabel: String? = null,
    role: Role? = null,
    onClick: () -> Unit
): Modifier {
    val interactionSource = remember { MutableInteractionSource() }
    return this
        .clip(shape)
        .capturePressDim(interactionSource)
        .combinedClickable(
            interactionSource = interactionSource,
            indication = ripple(bounded = true),
            enabled = enabled,
            onClickLabel = onClickLabel,
            role = role,
            onClick = onClick
        )
}

@OptIn(ExperimentalFoundationApi::class)
@Composable
internal fun Modifier.captureCombinedClickable(
    shape: Shape,
    enabled: Boolean = true,
    onClickLabel: String? = null,
    role: Role? = null,
    onLongClickLabel: String? = null,
    onLongClick: (() -> Unit)? = null,
    onDoubleClick: (() -> Unit)? = null,
    onClick: () -> Unit
): Modifier {
    val interactionSource = remember { MutableInteractionSource() }
    return this
        .clip(shape)
        .capturePressDim(interactionSource)
        .combinedClickable(
            interactionSource = interactionSource,
            indication = ripple(bounded = true),
            enabled = enabled,
            onClickLabel = onClickLabel,
            role = role,
            onLongClickLabel = onLongClickLabel,
            onLongClick = onLongClick,
            onDoubleClick = onDoubleClick,
            onClick = onClick
        )
}
