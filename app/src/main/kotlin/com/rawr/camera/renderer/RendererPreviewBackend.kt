package com.rawr.camera.renderer

import android.graphics.Bitmap
import android.view.Surface
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.Job
import kotlinx.coroutines.launch
import kotlinx.coroutines.sync.Mutex
import kotlinx.coroutines.sync.withLock
import kotlinx.coroutines.withContext

/** Android preview outputs. The image processing itself stays in the native renderer. */
internal class RendererPreviewBackend(
    private val native: RendererNativeSession,
    private val assets: RendererAssets,
    private val lane: Mutex,
    private val scope: CoroutineScope,
    private val exporting: () -> Boolean,
    private val visible: () -> Boolean
) {
    suspend fun preview(job: RenderJob, region: FloatArray? = null): Bitmap = withContext(Dispatchers.IO) {
        lane.withLock {
            check(!exporting()) { "Export in progress" }
            val crop = region?.let { r ->
                val width = (job.width * r[2]).toInt().coerceIn(1, job.width)
                val height = (job.height * r[3]).toInt().coerceIn(1, job.height)
                intArrayOf((job.width * r[0]).toInt().coerceIn(0, job.width - width),
                    (job.height * r[1]).toInt().coerceIn(0, job.height - height), width, height)
            }
            val cw = crop?.get(2) ?: job.width; val ch = crop?.get(3) ?: job.height
            val export = RenderResolution.dimensions(job.width, job.height, job.megapixels)
            val scale = minOf(export.first.toDouble() / job.width, 2048.0 / maxOf(cw, ch))
            val w = (cw * scale).toInt().coerceAtLeast(2); val h = (ch * scale).toInt().coerceAtLeast(2)
            val pixels = checkNotNull(native.render(job, assets.prepareRecipe(job, job.draft), w, h, -1, crop, export.first, export.second))
            // Pixels arrive in display order (native EXIF transpose); orientations
            // 5..8 swap dimensions. Single Bitmap, no rotation copy per preview.
            val (bw, bh) = if (job.orientation in 5..8) h to w else w to h
            Bitmap.createBitmap(pixels, bw, bh, Bitmap.Config.ARGB_8888)
        }
    }
    suspend fun attachPreviewSurface(job: RenderJob, surface: Surface?) = withContext(Dispatchers.IO) {
        lane.withLock {
            if (surface == null) {
                native.detachSurface()
            } else {
                native.setSurface(job, surface)
            }
        }
    }
    fun detachPreviewSurface(after: Job? = null): Job = scope.launch {
        after?.join()
        lane.withLock { native.detachSurface() }
    }
    suspend fun previewSurface(job: RenderJob) = withContext(Dispatchers.IO) {
        lane.withLock {
            check(!exporting()) { "Export in progress" }
            val crop: IntArray? = null
            val export = RenderResolution.dimensions(job.width, job.height, job.megapixels)
            val scale = minOf(export.first.toDouble() / job.width, 1280.0 / maxOf(job.width, job.height))
            val w = (job.width * scale).toInt().coerceAtLeast(2)
            val h = (job.height * scale).toInt().coerceAtLeast(2)
            val started = android.os.SystemClock.elapsedRealtime()
            native.render(job, assets.prepareRecipe(job, job.draft), w, h, -2, crop, export.first, export.second)
            android.util.Log.i("RawrRenderer", "Editor preview ${w}x$h in ${android.os.SystemClock.elapsedRealtime() - started}ms")
        }
    }
    /**
     * Full-frame HD preview (capped at ~6MP). The full-frame crop selects the
     * demosaic path rather than the Bayer proxy. Pass the selected export
     * dimensions as its working resolution so a 200MP -> 50MP selection uses
     * the same sharp Bayer bin as export before demosaicing. Only the final
     * image is reduced to HD size.
     */
    suspend fun previewHd(job: RenderJob): Bitmap = withContext(Dispatchers.IO) {
        lane.withLock {
            check(!exporting()) { "Export in progress" }
            val (w, h) = RenderResolution.hdDimensions(job.width, job.height)
            val (workingWidth, workingHeight) = RenderResolution.dimensions(job.width, job.height, job.megapixels)
            val crop = intArrayOf(0, 0, job.width, job.height)
            val pixels = checkNotNull(native.render(job, assets.prepareRecipe(job, job.draft), w, h, -1, crop,
                workingWidth, workingHeight))
            val (bw, bh) = if (job.orientation in 5..8) h to w else w to h
            Bitmap.createBitmap(pixels, bw, bh, Bitmap.Config.ARGB_8888)
        }
    }
    suspend fun closePreview() {
        check(!exporting())
        native.cancel()
        lane.withLock { native.close() }
    }
    fun releasePreview() { scope.launch { lane.withLock {
        if (!visible() && !exporting()) native.close()
    } } }
}
