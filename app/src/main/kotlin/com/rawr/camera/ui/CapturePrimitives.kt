package com.rawr.camera.ui

import androidx.compose.foundation.BorderStroke
import androidx.compose.foundation.background
import androidx.compose.foundation.layout.*
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material3.Surface
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.rememberUpdatedState
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.graphicsLayer
import androidx.compose.ui.platform.testTag
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.style.TextAlign
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.unit.Dp
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import com.rawr.camera.model.Orientation

/**
 * Shared capture-screen UI primitives.
 *
 * Small stateless atoms (choice chip, top-bar panel, orientation modifier)
 * reused across control bars, monitor panels and settings surfaces.
 * Feature compositions (CaptureControls, QuickControls, MonitorPanel, ...)
 * live in their own files.
 */

@Composable
internal fun SmallChoice(
    label: String,
    active: Boolean,
    modifier: Modifier = Modifier,
    enabled: Boolean = true,
    orientation: Orientation = Orientation.Portrait,
    testTag: String? = null,
    onLongClick: (() -> Unit)? = null,
    onClick: () -> Unit
) {
    // Route through rememberUpdatedState so the clickable modifier keeps stable
    // keys: per-frame snapshot recompositions must not restart gesture
    // detection mid-tap and swallow the tap.
    val latestOnClick by rememberUpdatedState(onClick)
    val latestOnLongClick by rememberUpdatedState(onLongClick)
    val shape = RoundedCornerShape(10.dp)
    Surface(
        modifier =
            modifier
                .then(if (testTag != null) Modifier.testTag(testTag) else Modifier)
                .captureControlSurface(shape = shape, active = active, textureStrength = .65f)
                .captureCombinedClickable(
                    shape = shape,
                    enabled = enabled,
                    onClick = { latestOnClick() },
                    onLongClick = latestOnLongClick?.let { cb -> { cb() } }
                ),
        shape = shape,
        color = Color.Transparent,
        border = null
    ) {
        Box(Modifier.fillMaxSize().padding(horizontal = 3.dp), contentAlignment = Alignment.Center) {
            Text(
                text = label,
                color = if (active) CaptureColors.Accent else Color.White.copy(alpha = .46f),
                fontFamily = CaptureMono,
                fontWeight = FontWeight.Bold,
                fontSize = 10.sp,
                maxLines = 1,
                overflow = TextOverflow.Ellipsis,
                textAlign = TextAlign.Center,
                modifier =
                    Modifier
                        .fillMaxWidth()
                        .uprightInLandscape(orientation)
            )
        }
    }
}

@Composable
internal fun CompactTopBarPalette(
    title: String,
    orientation: Orientation,
    modifier: Modifier = Modifier,
    leadingWeight: Float,
    trailingWeight: Float,
    leading: @Composable BoxScope.() -> Unit,
    trailing: @Composable BoxScope.() -> Unit,
    bodyHeight: Dp = CaptureDimens.TopBarPaletteBodyHeight
) {
    val width =
        if (orientation == Orientation.Landscape) {
            CaptureDimens.TopBarPaletteLandscapeWidth
        } else {
            CaptureDimens.TopBarPalettePortraitWidth
        }
    CaptureTopPanel(
        title = title,
        orientation = orientation,
        modifier = modifier.width(width)
    ) {
        Row(
            Modifier.fillMaxWidth().height(bodyHeight),
            horizontalArrangement = Arrangement.spacedBy(CaptureDimens.TopBarPaletteSplitGap),
            verticalAlignment = Alignment.Top
        ) {
            Box(Modifier.weight(leadingWeight).fillMaxHeight(), content = leading)
            Box(
                Modifier
                    .width(1.dp)
                    .fillMaxHeight()
                    .background(CaptureColors.Line)
            )
            Box(Modifier.weight(trailingWeight).fillMaxHeight(), content = trailing)
        }
    }
}

internal fun Modifier.uprightInLandscape(orientation: Orientation): Modifier =
    if (orientation == Orientation.Landscape) graphicsLayer(rotationZ = 90f) else this

@Composable
internal fun CaptureTopPanel(
    title: String,
    orientation: Orientation,
    modifier: Modifier = Modifier,
    content: @Composable ColumnScope.() -> Unit
) {
    val shape = RoundedCornerShape(15.dp)
    Surface(
        modifier =
            modifier
                .padding(10.dp)
                .widthIn(max = 360.dp)
                .uprightInLandscape(orientation),
        shape = shape,
        color = CaptureColors.ViewfinderPanel,
        border = BorderStroke(1.dp, CaptureColors.Line),
        shadowElevation = 0.dp
    ) {
        Column(Modifier.padding(12.dp), verticalArrangement = Arrangement.spacedBy(7.dp)) {
            Text(
                title,
                fontFamily = CaptureMono,
                fontWeight = FontWeight.ExtraBold,
                fontSize = 10.sp,
                letterSpacing = 1.2.sp
            )
            content()
        }
    }
}
