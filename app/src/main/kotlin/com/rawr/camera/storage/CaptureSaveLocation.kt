package com.rawr.camera.storage

/** Shared destination selection for photo and video captures. */
internal data class CaptureSaveLocation(val relativePath: String, val treeUri: String? = null) {
    companion object {
        fun fromId(id: String): CaptureSaveLocation = when {
            id.startsWith("storage.tree:") -> CaptureSaveLocation(
                relativePath = "",
                treeUri = id.removePrefix("storage.tree:")
            )
            id == "storage.pictures_raw" -> CaptureSaveLocation("Pictures/RAW Camera")
            else -> CaptureSaveLocation("DCIM/Camera")
        }
    }
}
