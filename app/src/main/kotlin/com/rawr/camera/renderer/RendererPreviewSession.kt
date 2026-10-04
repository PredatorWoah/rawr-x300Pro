package com.rawr.camera.renderer

import android.graphics.Bitmap
import android.view.Surface
import kotlinx.coroutines.CancellationException
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.Job
import kotlinx.coroutines.SupervisorJob
import kotlinx.coroutines.cancel
import kotlinx.coroutines.delay
import kotlinx.coroutines.flow.*
import kotlinx.coroutines.launch

internal data class RendererViewport(
    val zoom: Float = 1f,
    val offsetX: Float = 0f,
    val offsetY: Float = 0f,
    val width: Int = 1,
    val height: Int = 1
)

internal data class RendererPreviewState(
    val surfaceHasFrame: Boolean = false,
    val failed: Boolean = false,
    val busy: Boolean = false,
    val detail: Bitmap? = null,
    val hdBitmap: Bitmap? = null,
    val hdKey: RendererPreviewKey? = null,
    val hdBusy: Boolean = false
)

/** Owns preview scheduling and surface operations; the UI only supplies viewport measurements. */
internal class RendererPreviewSession(
    initialJob: RenderJob,
    private val store: RendererStore,
    private val onError: (String?) -> Unit
) : AutoCloseable {
    private val scope = CoroutineScope(SupervisorJob() + Dispatchers.Main.immediate)
    private val job = MutableStateFlow(initialJob)
    private val viewport = MutableStateFlow(RendererViewport())
    private val mutableState = MutableStateFlow(RendererPreviewState())
    val state = mutableState.asStateFlow()
    private val visible = MutableStateFlow(true)
    private var surfaceReady = false
    private var hdJob: Job? = null
    private var surfaceGeneration = 0L
    private var surfaceJob: Job? = null
    private var detachJob: Job? = null

    init {
        RendererProxyScheduler(
            scope, job,
            ready = { visible.value && surfaceReady },
            busy = { store.busy.value },
            generation = { surfaceGeneration },
            render = { current ->
                val generation = surfaceGeneration
                mutableState.update { it.copy(busy = true) }
                try {
                    store.previewSurface(current)
                    if (visible.value && surfaceReady && generation == surfaceGeneration) {
                        mutableState.update { it.copy(surfaceHasFrame = true, failed = false) }
                    }
                } finally { mutableState.update { it.copy(busy = false) } }
            },
            onFailure = {
                mutableState.update { state -> state.copy(failed = true) }
                report(it)
            }
        )
        scope.launch {
            combine(job, viewport, store.busy, visible) { current, view, busy, shown -> Triple(current, view, busy || !shown) }
                .collectLatest { (current, view, busy) ->
                    mutableState.update { it.copy(detail = null) }
                    if (view.zoom > 1.05f && !busy && visible.value) {
                        delay(250)
                        try {
                            val bitmap = store.preview(current, detailRegion(current.orientation, view))
                            mutableState.update { it.copy(detail = bitmap) }
                        } catch (e: CancellationException) { throw e }
                        catch (e: Exception) { report(e) }
                    }
                }
        }
    }

    fun updateJob(current: RenderJob) { job.value = current }
    fun updateViewport(current: RendererViewport) { viewport.value = current }

    fun setVisible(value: Boolean) {
        visible.value = value
        if (!value) { hdJob?.cancel(); surfaceDestroyed() }
    }

    fun surfaceAvailable(surface: Surface) {
        val generation = ++surfaceGeneration
        surfaceJob?.cancel()
        val detach = detachJob
        surfaceJob = scope.launch {
            try {
                detach?.join()
                store.attachPreviewSurface(job.value, surface)
                if (generation == surfaceGeneration && visible.value) {
                    surfaceReady = true
                    mutableState.update { it.copy(surfaceHasFrame = false, failed = false) }
                }
            } catch (e: CancellationException) { throw e }
            catch (e: Exception) { report(e) }
        }
    }

    fun surfaceDestroyed() {
        surfaceGeneration++
        surfaceJob?.cancel()
        surfaceReady = false
        mutableState.update { it.copy(surfaceHasFrame = false) }
        if (!store.busy.value) store.cancel()
        detachJob = store.detachPreviewSurface(surfaceJob)
    }

    fun requestHd() {
        if (store.busy.value || !visible.value) return
        hdJob?.cancel()
        hdJob = scope.launch {
            delay(300)
            if (store.busy.value || !visible.value) return@launch
            store.cancel()
            mutableState.update { it.copy(hdBusy = true) }
            val current = job.value
            try {
                val bitmap = store.previewHd(current)
                mutableState.update { it.copy(hdBitmap = bitmap, hdKey = current.previewKey()) }
            } catch (e: CancellationException) { throw e }
            catch (e: Exception) { report(e) }
            finally { mutableState.update { it.copy(hdBusy = false) } }
        }
    }

    private fun report(e: Exception) {
        if (e !is IllegalStateException || e.message !in setOf("Cancelled", "Export in progress")) onError(e.message)
    }

    override fun close() {
        surfaceDestroyed()
        scope.cancel()
    }
}

/** EXIF-aware mapping from a display viewport into a normalized sensor crop. */
internal fun detailRegion(orientation: Int, view: RendererViewport): FloatArray {
    val fraction = 1f / view.zoom
    val left = (.5f - view.offsetX / (view.width.coerceAtLeast(1) * view.zoom) - fraction / 2).coerceIn(0f, 1f - fraction)
    val top = (.5f - view.offsetY / (view.height.coerceAtLeast(1) * view.zoom) - fraction / 2).coerceIn(0f, 1f - fraction)
    return when (orientation) {
        2 -> floatArrayOf(1 - left - fraction, top, fraction, fraction)
        4 -> floatArrayOf(left, 1 - top - fraction, fraction, fraction)
        5 -> floatArrayOf(top, left, fraction, fraction)
        7 -> floatArrayOf(1 - top - fraction, 1 - left - fraction, fraction, fraction)
        6 -> floatArrayOf(top, 1 - left - fraction, fraction, fraction)
        8 -> floatArrayOf(1 - top - fraction, left, fraction, fraction)
        3 -> floatArrayOf(1 - left - fraction, 1 - top - fraction, fraction, fraction)
        else -> floatArrayOf(left, top, fraction, fraction)
    }
}
