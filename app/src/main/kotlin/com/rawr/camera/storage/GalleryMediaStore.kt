package com.rawr.camera.storage

import androidx.core.content.edit
import android.content.Context
import android.net.Uri
import android.provider.MediaStore

/** Read-only MediaStore access for the capture-screen "last image" affordance. */
class GalleryMediaStore(private val context: Context) {
    private val prefs = context.getSharedPreferences("gallery_thumbnail", Context.MODE_PRIVATE)

    fun rememberPublishedJpeg(uri: Uri) {
        prefs.edit { putString("latest_jpeg_uri", uri.toString()) }
    }

    fun findLatestRawrJpeg(): Uri? {
        prefs.getString("latest_jpeg_uri", null)?.let(Uri::parse)?.let { remembered ->
            val readable =
                runCatching {
                    context.contentResolver.openFileDescriptor(remembered, "r")?.use { true } ?: false
                }.getOrDefault(false)
            if (readable) return remembered
        }
        val collection = MediaStore.Images.Media.EXTERNAL_CONTENT_URI
        val projection = arrayOf(MediaStore.Images.Media._ID)
        val selection =
            buildString {
                append(MediaStore.Images.Media.MIME_TYPE)
                append(" = ? AND ")
                append(MediaStore.Images.Media.DISPLAY_NAME)
                append(" LIKE ? AND ")
                append(MediaStore.Images.Media.IS_PENDING)
                append(" = 0")
            }
        val selectionArgs = arrayOf("image/jpeg", "RAWR_%.jpg")
        val sortOrder = "${MediaStore.Images.Media.DATE_ADDED} DESC, ${MediaStore.Images.Media._ID} DESC"

        return context.contentResolver
            .query(
                collection,
                projection,
                selection,
                selectionArgs,
                sortOrder
            )?.use { cursor ->
                if (!cursor.moveToFirst()) return@use null
                val id = cursor.getLong(cursor.getColumnIndexOrThrow(MediaStore.Images.Media._ID))
                Uri.withAppendedPath(collection, id.toString())
            }
    }
}
