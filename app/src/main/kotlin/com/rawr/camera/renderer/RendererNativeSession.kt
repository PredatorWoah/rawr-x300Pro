package com.rawr.camera.renderer

import android.content.Context
import android.view.Surface
import com.rawr.camera.storage.StillOutputStore
import java.io.File

/** Thin JNI adapter. Call handle-mutating operations under the store's shared operation lane. */
internal class RendererNativeSession(private val context: Context, private val source: (RenderJob) -> File) {
    @Volatile private var handle = 0L
    private var opened = ""
    private fun open(job: RenderJob): Long {
        check(!StillOutputStore.hasActiveCaptures(context.filesDir)) { "Wait for capture processing to finish" }
        if (opened == job.id && handle != 0L) return handle
        if (handle != 0L) RendererNative.close(handle)
        handle = 0; opened = ""
        handle = RendererNative.open(source(job).absolutePath, context.filesDir.absolutePath, context.applicationInfo.nativeLibraryDir, context.assets)
        opened = job.id
        return handle
    }
    fun inspect(job: RenderJob) = RendererNative.inspect(open(job))
    fun render(job: RenderJob, recipe: String, width: Int, height: Int, fd: Int,
               crop: IntArray? = null, fullWidth: Int = width, fullHeight: Int = height): IntArray? =
        RendererNative.render(open(job), recipe, width, height, fd, crop, fullWidth, fullHeight)
    fun setSurface(job: RenderJob, surface: Surface?) {
        if (surface == null) detachSurface() else RendererNative.setSurface(open(job), surface)
    }
    fun detachSurface() { if (handle != 0L) RendererNative.setSurface(handle, null) }
    fun materialize(capture: String, fd: Int) = RendererNative.materialize(
        File(context.filesDir, "still_jobs/$capture.job").absolutePath, fd,
        context.filesDir.absolutePath, context.applicationInfo.nativeLibraryDir, context.assets)
    fun closeFor(id: String) { if (opened == id) close() }
    fun close() {
        if (handle != 0L) RendererNative.close(handle)
        handle = 0; opened = ""
    }
    fun cancel() { if (handle != 0L) RendererNative.cancel(handle) }
    fun progress() = if (handle != 0L) RendererNative.progress(handle) else 0
}
