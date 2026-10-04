package com.rawr.camera.renderer

import android.content.res.AssetManager
import android.view.Surface

internal object RendererNative {
    init { System.loadLibrary("rawrcam_native") }
    external fun open(path: String, files: String, library: String, assets: AssetManager): Long
    external fun inspect(id: Long): String
    external fun render(id: Long, recipe: String, width: Int, height: Int, fd: Int, crop: IntArray? = null, fullWidth: Int = width, fullHeight: Int = height): IntArray?
    external fun setSurface(id: Long, surface: Surface?)
    external fun progress(id: Long): Int
    external fun cancel(id: Long)
    external fun close(id: Long)
    external fun materialize(path: String, fd: Int, files: String, library: String, assets: AssetManager)
}
