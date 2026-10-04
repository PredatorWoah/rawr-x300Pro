package com.rawr.camera.integration

object InternalTraceNative {
    init {
        System.loadLibrary("rawrcam_native")
    }

    external fun clear()

    external fun setRetainedRows(rows: Int)

    external fun recordZslArtifactPublished(success: Boolean, bytes: Long)
}
