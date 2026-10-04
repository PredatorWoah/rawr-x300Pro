package com.rawr.camera.renderer

import android.graphics.Bitmap
import android.view.Surface
import android.view.SurfaceHolder
import android.view.SurfaceView
import androidx.compose.foundation.Image
import androidx.compose.foundation.gestures.detectTransformGestures
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.widthIn
import androidx.compose.foundation.shape.RoundedCornerShape
import com.rawr.camera.ui.icons.Icons
import com.rawr.camera.ui.icons.rounded.BrokenImage
import com.rawr.camera.ui.icons.rounded.Image
import androidx.compose.material3.Card
import androidx.compose.material3.CardDefaults
import androidx.compose.material3.CircularProgressIndicator
import androidx.compose.material3.Icon
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.SuggestionChip
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.DisposableEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.rememberUpdatedState
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.geometry.Offset
import androidx.compose.ui.graphics.asImageBitmap
import androidx.compose.ui.graphics.graphicsLayer
import androidx.compose.ui.input.pointer.pointerInput
import androidx.compose.ui.layout.ContentScale
import androidx.compose.ui.layout.onSizeChanged
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.platform.testTag
import androidx.compose.ui.unit.IntSize
import androidx.compose.ui.unit.dp
import androidx.compose.ui.viewinterop.AndroidView

@Composable
internal fun RendererPreviewCard(
    job: RenderJob,
    bitmap: Bitmap?,
    surfaceHasFrame: Boolean,
    previewFailed: Boolean,
    detail: Bitmap?,
    previewBusy: Boolean,
    hdShowing: Boolean = false,
    approxShowing: Boolean = false,
    zoom: Float,
    offset: Offset,
    onZoomChange: (Float, Offset) -> Unit,
    onViewportChange: (IntSize) -> Unit,
    onSurfaceAvailable: (Surface) -> Unit,
    onSurfaceDestroyed: () -> Unit
) {
    val context = LocalContext.current
    // The pointerInput block outlives recompositions, so plain parameters go
    // stale inside gestures. Updated-state holders always read fresh values.
    val latestZoom by rememberUpdatedState(zoom)
    val latestOffset by rememberUpdatedState(offset)
    var viewportPx by remember(job.id) { mutableStateOf(IntSize(1, 1)) }
    val latestViewport by rememberUpdatedState(viewportPx)
    Card(
        modifier = Modifier.fillMaxWidth().padding(horizontal = 16.dp, vertical = 4.dp).testTag("renderer_preview"),
        colors = CardDefaults.cardColors(containerColor = MaterialTheme.colorScheme.surfaceContainerLow)
    ) {
        androidx.compose.foundation.layout.BoxWithConstraints(
            Modifier.fillMaxWidth().padding(8.dp).widthIn(max = 560.dp)
        ) {
            val rotated = job.orientation in 5..8
            val aspect = (if (rotated) job.height.toFloat() / job.width else job.width.toFloat() / job.height)
                .takeIf { it.isFinite() && it > 0f } ?: 1f
            // Size the viewer to the image aspect so there is no letterbox gap
            // and no stretch. The bitmap from the store is already EXIF-rotated,
            // so `aspect` is the displayed aspect. Clamping height alone would
            // change the container aspect (portrait ideal height often exceeds
            // the 380dp cap) and FillBounds would then squeeze the image, so
            // narrow the width instead to preserve the aspect, centered.
            val idealHeight = maxWidth / aspect
            val viewerHeight = idealHeight.coerceAtMost(380.dp)
            val viewerWidth = if (idealHeight > 380.dp) 380.dp * aspect else maxWidth
            Box(
                Modifier.fillMaxWidth(),
                contentAlignment = Alignment.Center
            ) {
            Box(
                Modifier
                    .size(viewerWidth, viewerHeight)
                    .clip(RoundedCornerShape(12.dp))
                    .onSizeChanged { viewportPx = it; onViewportChange(it) }
                    .pointerInput(job.id) {
                        detectTransformGestures { centroid, pan, gestureZoom, _ ->
                            val currentZoom = latestZoom
                            val currentOffset = latestOffset
                            val viewport = latestViewport
                            val next = (currentZoom * gestureZoom).coerceIn(1f, 32f)
                            // Zoom about the gesture centroid so the image point under
                            // the fingers stays put; plain center-zoom makes pinch
                            // drift away from the fingers and the detail crop then
                            // loads a center-based region instead of the gestured one.
                            // graphicsLayer uses transformOrigin=center, so with
                            // O = size/2, screen(p) = O + (p-O)*s + t, keeping p
                            // under the centroid fixed gives:
                            // t' = t*(s'/s) + pan + (c-O)*(1-s'/s).
                            val ratio = if (currentZoom == 0f) 1f else next / currentZoom
                            val center = Offset(viewport.width / 2f, viewport.height / 2f)
                            val translated = currentOffset * ratio + pan + (centroid - center) * (1f - ratio)
                            val limitX = viewport.width * (next - 1) / 2
                            val limitY = viewport.height * (next - 1) / 2
                            val clamped = Offset(
                                translated.x.coerceIn(-limitX, limitX),
                                translated.y.coerceIn(-limitY, limitY)
                            )
                            onZoomChange(next, clamped)
                        }
                    },
                contentAlignment = Alignment.Center
            ) {
                        // Detail is an export-resolution crop of the visible region: show it
                        // full-bleed as a replacement (not over the zoomed base) to avoid
                        // double-image misalignment.
                        val showingDetail = detail != null && zoom > 1.05f
                        val surfaceView = remember(job.id) { SurfaceView(context) }
                        DisposableEffect(surfaceView) {
                            var bound: Surface? = null
                            var disposed = false
                            fun bind(surface: Surface) {
                                if (!disposed && surface.isValid && bound !== surface) {
                                    bound = surface
                                    onSurfaceAvailable(surface)
                                }
                            }
                            val callback = object : SurfaceHolder.Callback {
                                override fun surfaceCreated(holder: SurfaceHolder) = bind(holder.surface)
                                override fun surfaceChanged(holder: SurfaceHolder, format: Int, width: Int, height: Int) {
                                    bind(holder.surface)
                                }
                                override fun surfaceDestroyed(holder: SurfaceHolder) {
                                    bound = null
                                    onSurfaceDestroyed()
                                }
                            }
                            surfaceView.holder.addCallback(callback)
                            bind(surfaceView.holder.surface)
                            surfaceView.post { bind(surfaceView.holder.surface) }
                            onDispose {
                                disposed = true
                                surfaceView.holder.removeCallback(callback)
                                onSurfaceDestroyed()
                            }
                        }
                        AndroidView(factory = { surfaceView }, modifier = Modifier.fillMaxSize().graphicsLayer {
                            scaleX = zoom; scaleY = zoom
                            translationX = offset.x; translationY = offset.y
                        })
                        if (showingDetail) {
                            detail?.let {
                                Image(it.asImageBitmap(), contentDescription = "High quality detail",
                                    contentScale = ContentScale.Fit, modifier = Modifier.fillMaxSize())
                            }
                        } else if (bitmap != null) {
                            Image(bitmap.asImageBitmap(), contentDescription = "High quality preview",
                                contentScale = ContentScale.Fit, modifier = Modifier.fillMaxSize().graphicsLayer {
                                    scaleX = zoom; scaleY = zoom
                                    translationX = offset.x; translationY = offset.y
                                })
                        }
                        if (!surfaceHasFrame && bitmap == null && detail == null && !previewBusy && previewFailed) {
                            Column(horizontalAlignment = Alignment.CenterHorizontally) {
                                Icon(Icons.Rounded.BrokenImage, contentDescription = null, tint = MaterialTheme.colorScheme.onSurfaceVariant)
                                Spacer(Modifier.height(8.dp))
                                Text("Preview unavailable", style = MaterialTheme.typography.bodyMedium, color = MaterialTheme.colorScheme.onSurfaceVariant)
                            }
                        }
                        if (!surfaceHasFrame && bitmap == null && !previewFailed) {
                            CircularProgressIndicator(modifier = Modifier.testTag("renderer_preview_loading"))
                        }
                        if (previewBusy || (zoom > 1.05f && detail == null && surfaceHasFrame)) {
                            CircularProgressIndicator(Modifier.align(Alignment.TopEnd).padding(8.dp).size(20.dp))
                        }
                        if (hdShowing && zoom <= 1.05f) {
                            SuggestionChip(
                                onClick = {},
                                label = { Text("HD") },
                                modifier = Modifier.align(Alignment.BottomStart).padding(8.dp).testTag("renderer_hd_badge")
                            )
                        }
                        if (approxShowing) {
                            SuggestionChip(
                                onClick = {},
                                label = { Text("Preview approx — Apply for exact") },
                                modifier = Modifier.align(Alignment.BottomEnd).padding(8.dp).testTag("renderer_approx_badge")
                            )
                        }
                    }
            }
            }
    }
}
