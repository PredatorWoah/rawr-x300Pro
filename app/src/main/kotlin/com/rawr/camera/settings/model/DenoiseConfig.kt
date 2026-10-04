package com.rawr.camera.settings.model

/**
 * Still-pipeline denoise configuration, single source of truth.
 *
 * Replaces eleven flat settings fields (master bool + method int + wavelet
 * strengths + two galosh lanes) whose arbitration used to live in three
 * places at once (controller transitions, prefs-restore inference, JNI
 * gating). Invalid states — "galosh with no lane", "master off with lanes
 * set", "method/lane disagreement" — are unrepresentable: the factory
 * repairs them instead of every reader re-validating.
 *
 * Wire values stay identical to the legacy ints (OFF=0, FULL=1,
 * CHROMA_ONLY=2), so recipe JSON, prefs keys, JNI params and native enums
 * are untouched. Only the settings-layer ownership changes.
 */
enum class LaneMode {
    OFF,
    FULL,
    CHROMA_ONLY;

    companion object {
        fun of(value: Int): LaneMode = entries.getOrElse(value.coerceIn(0, 2)) { OFF }
    }
}

/** Profiled wavelet denoise (pre-WB linear, needs a sensor noise profile). */
data class WaveletDenoise(
    val strength: Float = 1f,
    val detail: Float = 1f,
    // Luma-band force (Y0 threshold scale, shipped tuning 0.25; higher
    // smooths luma grain harder, chroma force stays fixed at 0.75).
    val luma: Float = 0.25f,
    // Wavelet band count (coarsest bands dropped first: faster, but
    // large-scale chroma-blotch cleanup goes first).
    val scales: Int = 7
)

/**
 * GALOSH-RAW lane (pre-demosaic Bayer, single-frame stills, RGGB only).
 * Luma is the WHT-shrinkage strength (<=0 bypasses the luma lane);
 * chroma is the LOESS strength (1.0 = calibrated).
 */
data class RawLane(
    val mode: LaneMode = LaneMode.OFF,
    val strength: Float = 1f,
    val luma: Float = 1f,
    val chroma: Float = 1f
)

/**
 * GALOSH-YUV lane (scene-linear SDR, all still paths incl. multiframe).
 * strengthY scales the luma threshold; strengthC is the chroma dial
 * (0 = true bypass, 0..1 = dry/wet mix, >1 = stiffer MAP-ridge).
 */
data class YuvLane(
    val mode: LaneMode = LaneMode.OFF,
    val strengthY: Float = 1f,
    val strengthC: Float = 1f
)

sealed interface DenoiseConfig {
    data object Off : DenoiseConfig

    data class Wavelet(val config: WaveletDenoise = WaveletDenoise()) : DenoiseConfig

    data class Galosh(
        val raw: RawLane = RawLane(),
        val yuv: YuvLane = YuvLane()
    ) : DenoiseConfig {
        init {
            require(raw.mode != LaneMode.OFF || yuv.mode != LaneMode.OFF) {
                "Galosh requires at least one lane"
            }
        }
    }

    companion object {
        /**
         * Repairs legacy field soup into a valid config. Mirrors the old
         * controller invariants: master off parks everything; galosh method
         * with no valid lane falls back to wavelet; anything else is wavelet.
         * Out-of-range ints and non-finite floats are clamped, never thrown.
         */
        fun fromLegacy(
            enabled: Boolean,
            method: Int,
            waveletStrength: Float,
            waveletDetail: Float,
            waveletLuma: Float = 0.25f,
            waveletScales: Int = 7,
            rawMode: Int,
            rawStrength: Float,
            rawLuma: Float,
            rawChroma: Float,
            yuvMode: Int,
            yuvStrengthY: Float,
            yuvStrengthC: Float
        ): DenoiseConfig {
            if (!enabled) return Off
            if (method == 1) {
                val raw = RawLane(
                    mode = LaneMode.of(rawMode),
                    strength = rawStrength.coerceIn(0f, 8f),
                    luma = rawLuma.coerceIn(0f, 8f),
                    chroma = rawChroma.coerceIn(0f, 8f)
                )
                val yuv = YuvLane(
                    mode = LaneMode.of(yuvMode),
                    strengthY = yuvStrengthY.coerceIn(0f, 8f),
                    strengthC = yuvStrengthC.coerceIn(0f, 8f)
                )
                if (raw.mode != LaneMode.OFF || yuv.mode != LaneMode.OFF) {
                    return Galosh(raw, yuv)
                }
            }
            return Wavelet(
                WaveletDenoise(
                    strength = waveletStrength.coerceIn(0f, 8f),
                    detail = waveletDetail.coerceIn(0f, 1.8f),
                    luma = waveletLuma.coerceIn(0f, 1f),
                    scales = waveletScales.coerceIn(1, 7)
                )
            )
        }
    }
}

/** Master-switch + method transitions. Each returns a valid config. */
fun DenoiseConfig.withMaster(enabled: Boolean): DenoiseConfig =
    if (enabled) {
        // ON keeps the selected method; nothing selected yet means wavelet.
        if (this is DenoiseConfig.Off) DenoiseConfig.Wavelet() else this
    } else {
        DenoiseConfig.Off
    }

/** Explicit method switch. Picking galosh with no lane armed defaults to the
 * YUV chroma-only lane (runs on all paths incl. multiframe/renderer). */
fun DenoiseConfig.withWaveletMethod(): DenoiseConfig =
    (this as? DenoiseConfig.Wavelet) ?: DenoiseConfig.Wavelet()

fun DenoiseConfig.withGaloshMethod(): DenoiseConfig =
    // Wavelet params have nowhere to live on a Galosh value; fresh lane
    // defaults apply (matches the old method-switch behavior).
    (this as? DenoiseConfig.Galosh)
        ?: DenoiseConfig.Galosh(raw = RawLane(), yuv = YuvLane(mode = LaneMode.CHROMA_ONLY))

/** Lane picks imply the galosh method + master switch. Parking the last lane
 * falls back to wavelet (keeps "params follow the choice" true). */
fun DenoiseConfig.withRawMode(mode: LaneMode): DenoiseConfig {
    val current = (this as? DenoiseConfig.Galosh)
    return if (mode != LaneMode.OFF) {
        DenoiseConfig.Galosh(
            raw = (current?.raw ?: RawLane()).copy(mode = mode),
            yuv = current?.yuv ?: YuvLane()
        )
    } else if (current != null && current.yuv.mode != LaneMode.OFF) {
        current.copy(raw = current.raw.copy(mode = LaneMode.OFF))
    } else {
        DenoiseConfig.Wavelet()
    }
}

fun DenoiseConfig.withYuvMode(mode: LaneMode): DenoiseConfig {
    val current = (this as? DenoiseConfig.Galosh)
    return if (mode != LaneMode.OFF) {
        DenoiseConfig.Galosh(
            raw = current?.raw ?: RawLane(),
            yuv = (current?.yuv ?: YuvLane()).copy(mode = mode)
        )
    } else if (current != null && current.raw.mode != LaneMode.OFF) {
        current.copy(yuv = current.yuv.copy(mode = LaneMode.OFF))
    } else {
        DenoiseConfig.Wavelet()
    }
}

/** Strength sliders only exist on an armed lane/method; otherwise no-op
 * (the UI disables them, so this is unreachable in practice). */
fun DenoiseConfig.withWaveletStrength(value: Float): DenoiseConfig =
    if (this is DenoiseConfig.Wavelet) {
        copy(config = config.copy(strength = value.coerceIn(0f, 8f)))
    } else {
        this
    }

fun DenoiseConfig.withWaveletDetail(value: Float): DenoiseConfig =
    if (this is DenoiseConfig.Wavelet) {
        copy(config = config.copy(detail = value.coerceIn(0f, 1.8f)))
    } else {
        this
    }

fun DenoiseConfig.withWaveletLuma(value: Float): DenoiseConfig =
    if (this is DenoiseConfig.Wavelet) {
        copy(config = config.copy(luma = value.coerceIn(0f, 1f)))
    } else {
        this
    }

fun DenoiseConfig.withWaveletScales(value: Int): DenoiseConfig =
    if (this is DenoiseConfig.Wavelet) {
        copy(config = config.copy(scales = value.coerceIn(1, 7)))
    } else {
        this
    }

fun DenoiseConfig.withRawStrengths(strength: Float? = null, luma: Float? = null, chroma: Float? = null): DenoiseConfig =
    if (this is DenoiseConfig.Galosh && raw.mode != LaneMode.OFF) {
        copy(
            raw = raw.copy(
                strength = strength?.coerceIn(0f, 8f) ?: raw.strength,
                luma = luma?.coerceIn(0f, 8f) ?: raw.luma,
                chroma = chroma?.coerceIn(0f, 8f) ?: raw.chroma
            )
        )
    } else {
        this
    }

fun DenoiseConfig.withYuvStrengths(strengthY: Float? = null, strengthC: Float? = null): DenoiseConfig =
    if (this is DenoiseConfig.Galosh && yuv.mode != LaneMode.OFF) {
        copy(
            yuv = yuv.copy(
                strengthY = strengthY?.coerceIn(0f, 8f) ?: yuv.strengthY,
                strengthC = strengthC?.coerceIn(0f, 8f) ?: yuv.strengthC
            )
        )
    } else {
        this
    }

/** Capture path capabilities the resolver decides on. Device gates (float16
 * compute, geometry size) stay native with SKIP emits; this covers the
 * path-level decisions that used to be re-derived at every tap site. */
enum class StillPath {
    SINGLE,
    MULTIFRAME
}

/**
 * Exactly what the still pipeline may run, after path arbitration.
 * Wire ints mirror LaneMode ordinals (0/1/2). Every method is off on the
 * merged path: the multiframe merge does its own denoising.
 */
data class ResolvedDenoise(
    val waveletStrength: Float = 0f,
    val waveletDetail: Float = 1f,
    val waveletLuma: Float = 0.25f,
    val waveletScales: Int = 7,
    val rawMode: Int = 0,
    val rawStrength: Float = 1f,
    val rawLuma: Float = 1f,
    val rawChroma: Float = 1f,
    val yuvMode: Int = 0,
    val yuvStrengthY: Float = 1f,
    val yuvStrengthC: Float = 1f,
    val skipped: List<String> = emptyList()
)

/**
 * Single path arbitration point. Native keeps its device gates as backstop.
 * The dedicated denoisers are single-frame only: a multiframe merge already
 * denoises (burst-fitted noise model plus fallback cleanup of motion areas),
 * so every method resolves to off on the merged path.
 */
fun DenoiseConfig.resolve(path: StillPath): ResolvedDenoise {
    val skipped = mutableListOf<String>()
    if (path == StillPath.MULTIFRAME && this !is DenoiseConfig.Off) {
        skipped += "single_frame_only_multiframe_denoises"
        return ResolvedDenoise(skipped = skipped)
    }
    return when (this) {
        is DenoiseConfig.Off -> ResolvedDenoise()
        is DenoiseConfig.Wavelet ->
            ResolvedDenoise(
                waveletStrength = config.strength,
                waveletDetail = config.detail,
                waveletLuma = config.luma,
                waveletScales = config.scales
            )
        is DenoiseConfig.Galosh -> {
            ResolvedDenoise(
                rawMode = raw.mode.ordinal,
                rawStrength = raw.strength,
                rawLuma = raw.luma,
                rawChroma = raw.chroma,
                yuvMode = yuv.mode.ordinal,
                yuvStrengthY = yuv.strengthY,
                yuvStrengthC = yuv.strengthC,
                skipped = skipped
            )
        }
    }
}

/**
 * JNI spec arrays: the collapsed form of the resolved intent at the
 * Kotlin->native boundary (one IntArray + one FloatArray instead of eleven
 * positional params). Layout is fixed; native validates lengths:
 *
 * modes[5]     = [master, method, rawMode, yuvMode, waveletScales] (lane ints mirror LaneMode ordinals)
 * strengths[8] = [waveletStrength, waveletDetail, rawStrength, rawLuma, rawChroma,
 *                 yuvStrengthY, yuvStrengthC, waveletLuma]
 */
fun ResolvedDenoise.toModesArray(): IntArray {
    val master = waveletStrength > 0f || rawMode != 0 || yuvMode != 0
    val method = if (rawMode != 0 || yuvMode != 0) 1 else 0
    return intArrayOf(if (master) 1 else 0, method, rawMode, yuvMode, waveletScales.coerceIn(1, 7))
}

fun ResolvedDenoise.toStrengthsArray(): FloatArray = floatArrayOf(
    waveletStrength, waveletDetail, rawStrength, rawLuma, rawChroma, yuvStrengthY, yuvStrengthC,
    waveletLuma.coerceIn(0f, 1f)
)

/**
 * Legacy 11-field decomposition for the pass-through plumbing (prefs keys,
 * recipe JSON). Keys and wire values are unchanged; only the ownership
 * moved here.
 */
data class LegacyDenoise(
    val enabled: Boolean,
    val method: Int,
    val waveletStrength: Float,
    val waveletDetail: Float,
    val waveletLuma: Float,
    val waveletScales: Int,
    val rawMode: Int,
    val rawStrength: Float,
    val rawLuma: Float,
    val rawChroma: Float,
    val yuvMode: Int,
    val yuvStrengthY: Float,
    val yuvStrengthC: Float
)

fun DenoiseConfig.toLegacy(): LegacyDenoise = when (this) {
    is DenoiseConfig.Off -> LegacyDenoise(
        enabled = false, method = 0,
        waveletStrength = 1f, waveletDetail = 1f, waveletLuma = 0.25f, waveletScales = 7,
        rawMode = 0, rawStrength = 1f, rawLuma = 1f, rawChroma = 1f,
        yuvMode = 0, yuvStrengthY = 1f, yuvStrengthC = 1f
    )
    is DenoiseConfig.Wavelet -> LegacyDenoise(
        enabled = true, method = 0,
        waveletStrength = config.strength, waveletDetail = config.detail,
        waveletLuma = config.luma, waveletScales = config.scales,
        rawMode = 0, rawStrength = 1f, rawLuma = 1f, rawChroma = 1f,
        yuvMode = 0, yuvStrengthY = 1f, yuvStrengthC = 1f
    )
    is DenoiseConfig.Galosh -> LegacyDenoise(
        enabled = true, method = 1,
        waveletStrength = 1f, waveletDetail = 1f, waveletLuma = 0.25f, waveletScales = 7,
        rawMode = raw.mode.ordinal, rawStrength = raw.strength,
        rawLuma = raw.luma, rawChroma = raw.chroma,
        yuvMode = yuv.mode.ordinal, yuvStrengthY = yuv.strengthY,
        yuvStrengthC = yuv.strengthC
    )
}
