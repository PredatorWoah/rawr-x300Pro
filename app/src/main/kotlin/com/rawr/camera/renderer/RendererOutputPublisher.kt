package com.rawr.camera.renderer

import android.content.ContentValues
import android.content.Context
import android.net.Uri
import android.provider.MediaStore

/** MediaStore adapter; native renderers receive an output descriptor only. */
internal class RendererOutputPublisher(private val context: Context) {
    fun isPublished(uri: Uri): Boolean = context.contentResolver.query(uri,
        arrayOf(MediaStore.Images.Media.IS_PENDING), null, null, null)?.use {
        it.moveToFirst() && it.getInt(0) == 0
    } ?: false
    fun createPending(displayName: String): Uri = checkNotNull(context.contentResolver.insert(
        MediaStore.Images.Media.EXTERNAL_CONTENT_URI, ContentValues().apply {
            put(MediaStore.Images.Media.DISPLAY_NAME, displayName)
            put(MediaStore.Images.Media.MIME_TYPE, "image/jpeg")
            put(MediaStore.Images.Media.RELATIVE_PATH, "DCIM/Camera")
            put(MediaStore.Images.Media.IS_PENDING, 1)
        }))
    fun write(uri: Uri, render: (Int) -> Unit) {
        checkNotNull(context.contentResolver.openFileDescriptor(uri, "rwt")).use { render(it.fd) }
    }
    fun publish(uri: Uri) {
        check(context.contentResolver.update(uri, ContentValues().apply {
            put(MediaStore.Images.Media.IS_PENDING, 0)
        }, null, null) > 0) { "JPEG publication failed" }
    }
    fun delete(uri: Uri) { context.contentResolver.delete(uri, null, null) }
}
