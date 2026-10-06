package com.rawr.camera.model

/**
 * Capture-screen control arrangement. Classic = viewfinder rails; Compact = V2 button strip; Simple = Compact's
 * controls in one rounded deck with the look (profile, film, params) tucked behind a LOOK chip. Pro = Gcam style: one
 * ruler for the selected value, a single row of live value tiles, and dedicated FILTERS and PARAMS buttons.
 */
enum class CaptureControlLayout {
    Classic,
    Compact,
    Simple,
    Pro;

    /** Every layout but Classic shares the button-strip geometry (no viewfinder rails). */
    val usesButtonStrip: Boolean get() = this != Classic
}
