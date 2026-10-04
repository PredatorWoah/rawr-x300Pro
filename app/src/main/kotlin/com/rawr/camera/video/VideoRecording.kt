package com.rawr.camera.video

import android.net.Uri
import org.json.JSONObject

/** UI and MediaStore handoff; capture, audio, encoding, and muxing may live in native code. */
interface VideoRecording : AutoCloseable {
    val isRecording: Boolean
    val outputUri: Uri?
    fun stats(): JSONObject
    fun journalSnapshot()
}
