package com.rawr.camera.storage

import androidx.core.content.edit
import android.content.Context
import android.net.Uri
import android.provider.MediaStore

/** Read-only MediaStore access for the capture-screen "last image" affordance. */
class GalleryMediaStore(private val context: Context) {
    private val prefs = context.getSharedPreferences("gallery_thumbnail", Context.MODE_PRIVATE)

    /** Remembers the newest published capture, JPEG or DNG, so a DNG-only shooter still gets a thumbnail. */
    fun rememberPublished(uri: Uri) {
        prefs.edit { putString(LATEST_KEY, uri.toString()) }
    }

    fun findLatestRawrImage(): Uri? {
        val remembered = prefs.getString(LATEST_KEY, null) ?: prefs.getString(LEGACY_JPEG_KEY, null)
        remembered?.let(Uri::parse)?.let { uri ->
            val readable =
                runCatching {
                    context.contentResolver.openFileDescriptor(uri, "r")?.use { true } ?: false
                }.getOrDefault(false)
            if (readable) return uri
        }
        val collection = MediaStore.Images.Media.EXTERNAL_CONTENT_URI
        val projection = arrayOf(MediaStore.Images.Media._ID)
        val selection =
            buildString {
                append(MediaStore.Images.Media.DISPLAY_NAME)
                append(" LIKE ? AND ")
                append(MediaStore.Images.Media.IS_PENDING)
                append(" = 0")
            }
        val sortOrder = "${MediaStore.Images.Media.DATE_ADDED} DESC, ${MediaStore.Images.Media._ID} DESC"

        return context.contentResolver
            .query(collection, projection, selection, arrayOf("RAWR_%"), sortOrder)
            ?.use { cursor ->
                if (!cursor.moveToFirst()) return@use null
                val id = cursor.getLong(cursor.getColumnIndexOrThrow(MediaStore.Images.Media._ID))
                Uri.withAppendedPath(collection, id.toString())
            }
    }

    private companion object {
        const val LATEST_KEY = "latest_image_uri"
        const val LEGACY_JPEG_KEY = "latest_jpeg_uri"
    }
}
