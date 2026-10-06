package com.rawr.camera.model

/**
 * Capture-screen control arrangement. Classic = viewfinder rails; Compact = V2 button strip; Simple = Compact's
 * controls in one rounded deck with the look (profile, film, params) tucked behind a LOOK chip.
 */
enum class CaptureControlLayout {
    Classic,
    Compact,
    Simple;

    /** Compact and Simple share the button-strip geometry (no viewfinder rails). */
    val usesButtonStrip: Boolean get() = this != Classic
}
