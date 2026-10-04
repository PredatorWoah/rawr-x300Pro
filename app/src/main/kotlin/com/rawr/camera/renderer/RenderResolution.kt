package com.rawr.camera.renderer

import kotlin.math.roundToInt
import kotlin.math.sqrt

internal object RenderResolution {
    private const val NATIVE_DEFAULT_MAX_PIXELS = 50_500_000L

    fun defaultMegapixels(width: Int, height: Int, rawr: Boolean): Double =
        if (rawr || width.toLong() * height <= NATIVE_DEFAULT_MAX_PIXELS) 0.0 else 25.0
    fun choices(width: Int, height: Int, rawr: Boolean): List<Double> = buildList {
        val mp = width.toLong() * height / 1_000_000.0
        if (rawr || width.toLong() * height <= NATIVE_DEFAULT_MAX_PIXELS) add(0.0)
        listOf(50.0, 25.0, 12.5).filterTo(this) { it < mp * .99 }
    }
    fun dimensions(width: Int, height: Int, megapixels: Double): Pair<Int, Int> {
        require(width > 0 && height > 0 && megapixels.isFinite() && megapixels >= 0)
        if (megapixels == 0.0) return width to height
        // 200MP -> 50MP is a true 2x Bayer bin. Preserve its exact geometry
        // rather than scaling the demosaiced result to an arbitrary 50.0MP.
        if (megapixels == 50.0 && width % 2 == 0 && height % 2 == 0 &&
            width.toLong() * height in 180_000_000L..220_000_000L) return width / 2 to height / 2
        val scale = sqrt((megapixels * 1_000_000 / (width.toLong() * height)).coerceAtMost(1.0))
        return ((width * scale / 2).roundToInt() * 2).coerceIn(minOf(2, width), width) to
            ((height * scale / 2).roundToInt() * 2).coerceIn(minOf(2, height), height)
    }
    /**
     * Exact-preview dimensions for the editor HD button: fit inside 6MP,
     * preserve aspect, round to even, never upscale. Keeps the 6MP RGBA16F
     * intermediates (~48MB) bounded on 200MP sources.
     */
    fun hdDimensions(width: Int, height: Int): Pair<Int, Int> {
        require(width > 0 && height > 0)
        val scale = sqrt((6_000_000.0 / (width.toLong() * height)).coerceAtMost(1.0))
        var w = ((width * scale / 2).roundToInt() * 2).coerceIn(minOf(2, width), width)
        var h = ((height * scale / 2).roundToInt() * 2).coerceIn(minOf(2, height), height)
        // Even-rounding can nudge the product marginally over the cap (e.g.
        // 200MP lands ~1kpx over); trim the longer side until inside it.
        while (w.toLong() * h > 6_000_000L && (w > 2 || h > 2)) {
            if (w >= h && w > 2) w -= 2 else if (h > 2) h -= 2 else break
        }
        return w to h
    }
}
