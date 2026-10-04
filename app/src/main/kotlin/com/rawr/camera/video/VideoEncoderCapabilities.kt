package com.rawr.camera.video

import android.media.MediaCodecInfo
import android.media.MediaCodecList
import android.media.MediaFormat

/** Bitrate limits shared by settings and the recorder (the native recorder accepts the same range). */
object VideoEncoderLimits {
    const val MIN_BITRATE_MBPS = 1
    const val MAX_BITRATE_MBPS = 400
}

/** What the hardware HEVC encoder reports, for the Video Encoder settings page. */
object VideoEncoderCapabilities {
    /** KEY_BITRATE_MODE wire values the first hardware HEVC encoder accepts. */
    val supportedBitrateModes: Set<Int> by lazy {
        runCatching {
            val info = MediaCodecList(MediaCodecList.REGULAR_CODECS).codecInfos.firstOrNull {
                it.isEncoder && !it.isSoftwareOnly && MediaFormat.MIMETYPE_VIDEO_HEVC in it.supportedTypes
            } ?: return@runCatching setOf(CBR)
            val encoder = info.getCapabilitiesForType(MediaFormat.MIMETYPE_VIDEO_HEVC).encoderCapabilities
                ?: return@runCatching setOf(CBR)
            listOf(CQ, VBR, CBR).filter { encoder.isBitrateModeSupported(it) }.toSet()
        }.getOrDefault(setOf(CBR))
    }

    /** Highest bitrate (Mbps) the hardware HEVC encoder advertises; 100 if unknown. */
    val maxBitrateMbps: Int by lazy {
        runCatching {
            val info = MediaCodecList(MediaCodecList.REGULAR_CODECS).codecInfos.firstOrNull {
                it.isEncoder && !it.isSoftwareOnly && MediaFormat.MIMETYPE_VIDEO_HEVC in it.supportedTypes
            } ?: return@runCatching FALLBACK_MAX_MBPS
            val upper = info.getCapabilitiesForType(MediaFormat.MIMETYPE_VIDEO_HEVC).videoCapabilities
                ?.bitrateRange?.upper ?: return@runCatching FALLBACK_MAX_MBPS
            (upper / 1_000_000).coerceIn(FALLBACK_MAX_MBPS, VideoEncoderLimits.MAX_BITRATE_MBPS)
        }.getOrDefault(FALLBACK_MAX_MBPS)
    }

    /** Checks the same first hardware encoder used by the native path; configure is the final gate. */
    fun supportsMain10(width: Int, height: Int, fps: Int): Boolean = runCatching {
        val info = MediaCodecList(MediaCodecList.REGULAR_CODECS).codecInfos.firstOrNull {
            it.isEncoder && !it.isSoftwareOnly && MediaFormat.MIMETYPE_VIDEO_HEVC in it.supportedTypes
        } ?: return@runCatching false
        val caps = info.getCapabilitiesForType(MediaFormat.MIMETYPE_VIDEO_HEVC)
        caps.profileLevels.any { it.profile == MediaCodecInfo.CodecProfileLevel.HEVCProfileMain10 } &&
            caps.videoCapabilities?.areSizeAndRateSupported(width, height, fps.toDouble()) == true
    }.getOrDefault(false)

    private const val FALLBACK_MAX_MBPS = 100
    private const val CQ = MediaCodecInfo.EncoderCapabilities.BITRATE_MODE_CQ
    private const val VBR = MediaCodecInfo.EncoderCapabilities.BITRATE_MODE_VBR
    private const val CBR = MediaCodecInfo.EncoderCapabilities.BITRATE_MODE_CBR
}
