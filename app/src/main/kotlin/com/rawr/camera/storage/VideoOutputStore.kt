package com.rawr.camera.storage

import android.content.ContentValues
import android.content.Context
import android.net.Uri
import android.provider.DocumentsContract
import android.provider.MediaStore
import androidx.core.net.toUri

/** Provider operations for pending MediaStore videos and user-selected document trees. */
internal class VideoOutputStore(private val context: Context) {
    fun create(saveLocationId: String, displayName: String, capturedAt: Long): Uri {
        val destination = CaptureSaveLocation.fromId(saveLocationId)
        val resolver = context.contentResolver
        val tree = destination.treeUri?.toUri()
        return if (tree != null) {
            val parent = DocumentsContract.buildDocumentUriUsingTree(tree, DocumentsContract.getTreeDocumentId(tree))
            checkNotNull(DocumentsContract.createDocument(resolver, parent, "video/mp4", displayName)) {
                "Could not create video in the selected folder"
            }
        } else {
            val values = ContentValues().apply {
                put(MediaStore.Video.Media.DISPLAY_NAME, displayName)
                put(MediaStore.Video.Media.MIME_TYPE, "video/mp4")
                put(MediaStore.Video.Media.RELATIVE_PATH, destination.relativePath)
                put(MediaStore.Video.Media.IS_PENDING, 1)
                put(MediaStore.Video.Media.DATE_TAKEN, capturedAt)
            }
            checkNotNull(resolver.insert(MediaStore.Video.Media.EXTERNAL_CONTENT_URI, values)) {
                "Could not create video in ${destination.relativePath}"
            }
        }
    }

    fun publish(uri: Uri) {
        // Document providers have no MediaStore pending flag. Closing the
        // native descriptor finalizes their file at the selected destination.
        if (uri.authority == MediaStore.AUTHORITY) {
            check(context.contentResolver.update(uri,
                ContentValues().apply { put(MediaStore.Video.Media.IS_PENDING, 0) }, null, null) > 0) {
                "Video output disappeared before publication"
            }
        }
    }

    fun delete(uri: Uri) {
        if (uri.authority == MediaStore.AUTHORITY) context.contentResolver.delete(uri, null, null)
        else check(DocumentsContract.deleteDocument(context.contentResolver, uri)) {
            "Could not remove unfinished video"
        }
    }
}
