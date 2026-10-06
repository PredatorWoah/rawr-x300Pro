package com.rawr.camera.storage

import androidx.core.content.edit
import android.content.Context
import android.net.Uri
import android.provider.MediaStore

/**
 * One capture for the in-app gallery. A DNG and JPEG of the same shot are one item: [uri] is what to show (the JPEG when
 * there is one), [dngUri] / [jpegUri] say which files exist.
 */
data class GalleryItem(
    val uri: Uri,
    val name: String,
    val dngUri: Uri?,
    val jpegUri: Uri?,
    val dateAddedSeconds: Long
) {
    val kind: String
        get() = when {
            dngUri != null && jpegUri != null -> "DNG + JPG"
            dngUri != null -> "DNG"
            else -> "JPG"
        }
}

/** Read-only MediaStore access for the capture-screen "last image" affordance and the in-app gallery. */
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

    /** Newest first, at most [limit] captures, DNG and JPEG of one shot merged. */
    fun listRawrImages(limit: Int = 300): List<GalleryItem> {
        val collection = MediaStore.Images.Media.EXTERNAL_CONTENT_URI
        val projection =
            arrayOf(
                MediaStore.Images.Media._ID,
                MediaStore.Images.Media.DISPLAY_NAME,
                MediaStore.Images.Media.DATE_ADDED
            )
        val selection = "${MediaStore.Images.Media.DISPLAY_NAME} LIKE ? AND ${MediaStore.Images.Media.IS_PENDING} = 0"
        val sortOrder = "${MediaStore.Images.Media.DATE_ADDED} DESC, ${MediaStore.Images.Media._ID} DESC"
        val byBase = LinkedHashMap<String, GalleryItem>()
        context.contentResolver.query(collection, projection, selection, arrayOf("RAWR_%"), sortOrder)?.use { cursor ->
            val idColumn = cursor.getColumnIndexOrThrow(MediaStore.Images.Media._ID)
            val nameColumn = cursor.getColumnIndexOrThrow(MediaStore.Images.Media.DISPLAY_NAME)
            val dateColumn = cursor.getColumnIndexOrThrow(MediaStore.Images.Media.DATE_ADDED)
            while (cursor.moveToNext() && byBase.size < limit) {
                val name = cursor.getString(nameColumn) ?: continue
                val uri = Uri.withAppendedPath(collection, cursor.getLong(idColumn).toString())
                val base = name.substringBeforeLast('.')
                val isDng = name.endsWith(".dng", ignoreCase = true)
                val existing = byBase[base]
                byBase[base] =
                    if (existing == null) {
                        GalleryItem(
                            uri = uri,
                            name = base,
                            dngUri = if (isDng) uri else null,
                            jpegUri = if (isDng) null else uri,
                            dateAddedSeconds = cursor.getLong(dateColumn)
                        )
                    } else {
                        val dng = if (isDng) uri else existing.dngUri
                        val jpeg = if (isDng) existing.jpegUri else uri
                        existing.copy(uri = jpeg ?: dng ?: existing.uri, dngUri = dng, jpegUri = jpeg)
                    }
            }
        }
        return byBase.values.toList()
    }

    private companion object {
        const val LATEST_KEY = "latest_image_uri"
        const val LEGACY_JPEG_KEY = "latest_jpeg_uri"
    }
}
