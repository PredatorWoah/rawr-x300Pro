package com.rawr.camera.model

/** Primary export choice; a capture always has at least one requested format. */
enum class CaptureOutputFormat(val dng: Boolean, val jpeg: Boolean, val label: String) {
    DngAndJpeg(true, true, "DNG + JPEG"),
    Jpeg(false, true, "JPEG"),
    Dng(true, false, "DNG");

    fun next() = entries[(ordinal + 1) % entries.size]
    companion object {
        fun from(dng: Boolean, jpeg: Boolean) = when {
            !jpeg -> Dng
            dng -> DngAndJpeg
            else -> Jpeg
        }
    }
}
