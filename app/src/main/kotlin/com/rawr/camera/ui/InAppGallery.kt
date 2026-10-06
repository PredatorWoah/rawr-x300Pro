package com.rawr.camera.ui

import android.graphics.Bitmap
import android.net.Uri
import android.util.Size
import androidx.activity.compose.BackHandler
import androidx.compose.foundation.Image
import androidx.compose.foundation.background
import androidx.compose.foundation.clickable
import androidx.compose.foundation.gestures.awaitEachGesture
import androidx.compose.foundation.gestures.awaitFirstDown
import androidx.compose.foundation.gestures.calculatePan
import androidx.compose.foundation.gestures.calculateZoom
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.navigationBarsPadding
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.statusBarsPadding
import androidx.compose.foundation.pager.HorizontalPager
import androidx.compose.foundation.pager.rememberPagerState
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableFloatStateOf
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.asImageBitmap
import androidx.compose.ui.graphics.graphicsLayer
import androidx.compose.ui.input.pointer.pointerInput
import androidx.compose.ui.input.pointer.positionChanged
import androidx.compose.ui.layout.ContentScale
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import com.rawr.camera.storage.GalleryItem
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.withContext

/**
 * A small full-screen gallery for the app's own captures: swipe between them, pinch to zoom, share or open one in
 * another app. Opening a DNG-only shot in an external viewer often fails silently; this always shows something.
 */
@Composable
internal fun InAppGallery(
    items: List<GalleryItem>,
    startUri: Uri?,
    onClose: () -> Unit,
    onOpenExternal: (Uri) -> Unit,
    onShare: (Uri) -> Unit
) {
    BackHandler(onBack = onClose)
    val startPage = items.indexOfFirst { it.uri == startUri || it.dngUri == startUri || it.jpegUri == startUri }
        .coerceAtLeast(0)
    Box(Modifier.fillMaxSize().background(Color.Black)) {
        if (items.isEmpty()) {
            Text(
                "No captures yet",
                color = Color.White.copy(alpha = .7f),
                fontFamily = CaptureMono,
                modifier = Modifier.align(Alignment.Center)
            )
        } else {
            val pagerState = rememberPagerState(initialPage = startPage) { items.size }
            var zoomed by remember { mutableStateOf(false) }
            LaunchedEffect(pagerState.currentPage) { zoomed = false }
            HorizontalPager(
                state = pagerState,
                modifier = Modifier.fillMaxSize(),
                userScrollEnabled = !zoomed
            ) { page ->
                GalleryPage(
                    item = items[page],
                    active = pagerState.currentPage == page,
                    onZoomChanged = { if (pagerState.currentPage == page) zoomed = it }
                )
            }
            val current = items[pagerState.currentPage.coerceIn(0, items.lastIndex)]
            Row(
                Modifier.fillMaxWidth().statusBarsPadding().padding(horizontal = 16.dp, vertical = 10.dp),
                verticalAlignment = Alignment.CenterVertically
            ) {
                GalleryPill("CLOSE", onClose)
                Text(
                    "${pagerState.currentPage + 1} / ${items.size}",
                    color = Color.White,
                    fontFamily = CaptureMono,
                    fontSize = 12.sp,
                    modifier = Modifier.weight(1f),
                    textAlign = androidx.compose.ui.text.style.TextAlign.Center
                )
                Text(current.kind, color = CaptureColors.Accent, fontFamily = CaptureMono, fontWeight = FontWeight.Bold, fontSize = 12.sp)
            }
            Column(
                Modifier.align(Alignment.BottomCenter).fillMaxWidth().navigationBarsPadding().padding(16.dp),
                horizontalAlignment = Alignment.CenterHorizontally,
                verticalArrangement = Arrangement.spacedBy(10.dp)
            ) {
                Text(current.name, color = Color.White.copy(alpha = .8f), fontFamily = CaptureMono, fontSize = 11.sp)
                Row(horizontalArrangement = Arrangement.spacedBy(12.dp)) {
                    GalleryPill("OPEN IN APP") { onOpenExternal(current.uri) }
                    GalleryPill("SHARE") { onShare(current.uri) }
                    current.dngUri?.takeIf { current.jpegUri != null }?.let { dng ->
                        GalleryPill("SHARE DNG") { onShare(dng) }
                    }
                }
            }
        }
    }
}

@Composable
private fun GalleryPill(text: String, onClick: () -> Unit) {
    Box(
        Modifier
            .clip(RoundedCornerShape(16.dp))
            .background(Color.White.copy(alpha = .14f))
            .clickable(onClick = onClick)
            .padding(horizontal = 14.dp, vertical = 8.dp),
        contentAlignment = Alignment.Center
    ) {
        Text(text, color = Color.White, fontFamily = CaptureMono, fontWeight = FontWeight.Bold, fontSize = 11.sp)
    }
}

@Composable
private fun GalleryPage(item: GalleryItem, active: Boolean, onZoomChanged: (Boolean) -> Unit) {
    val context = LocalContext.current
    var bitmap by remember(item.uri) { mutableStateOf<Bitmap?>(null) }
    var failed by remember(item.uri) { mutableStateOf(false) }
    LaunchedEffect(item.uri) {
        val loaded =
            withContext(Dispatchers.IO) {
                // The system cache hands back a downscaled copy quickly, and extracts the embedded preview of a DNG.
                runCatching { context.contentResolver.loadThumbnail(item.uri, Size(2048, 2048), null) }.getOrNull()
            }
        bitmap = loaded
        failed = loaded == null
    }
    var scale by remember(item.uri) { mutableFloatStateOf(1f) }
    var offsetX by remember(item.uri) { mutableFloatStateOf(0f) }
    var offsetY by remember(item.uri) { mutableFloatStateOf(0f) }
    LaunchedEffect(active) {
        if (!active) {
            scale = 1f
            offsetX = 0f
            offsetY = 0f
        }
    }
    Box(Modifier.fillMaxSize(), contentAlignment = Alignment.Center) {
        val shown = bitmap
        if (shown != null) {
            Image(
                bitmap = shown.asImageBitmap(),
                contentDescription = item.name,
                contentScale = ContentScale.Fit,
                modifier = Modifier
                    .fillMaxSize()
                    .pointerInput(item.uri) {
                        // Only claim two-finger pinches and drags while zoomed in; a one-finger swipe at 1x goes to the pager.
                        awaitEachGesture {
                            awaitFirstDown(requireUnconsumed = false)
                            do {
                                val event = awaitPointerEvent()
                                val multiTouch = event.changes.count { it.pressed } > 1
                                if (multiTouch || scale > 1f) {
                                    scale = (scale * event.calculateZoom()).coerceIn(1f, 6f)
                                    if (scale > 1f) {
                                        val pan = event.calculatePan()
                                        offsetX += pan.x
                                        offsetY += pan.y
                                    } else {
                                        offsetX = 0f
                                        offsetY = 0f
                                    }
                                    onZoomChanged(scale > 1.02f)
                                    event.changes.forEach { if (it.positionChanged()) it.consume() }
                                }
                            } while (event.changes.any { it.pressed })
                        }
                    }
                    .graphicsLayer(
                        scaleX = scale,
                        scaleY = scale,
                        translationX = offsetX,
                        translationY = offsetY
                    )
            )
        } else {
            Text(
                if (failed) "Preview unavailable. Use OPEN IN APP." else "Loading",
                color = Color.White.copy(alpha = .6f),
                fontFamily = CaptureMono,
                fontSize = 12.sp
            )
        }
    }
}
