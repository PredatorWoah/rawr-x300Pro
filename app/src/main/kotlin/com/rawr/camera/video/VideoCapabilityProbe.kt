package com.rawr.camera.video

import android.media.MediaCodecInfo
import android.media.MediaCodecList
import android.media.MediaFormat
import com.rawr.camera.integration.NativePreviewEngine
import org.json.JSONArray
import org.json.JSONObject

/** Combines native camera facts with Android codec capability diagnostics. */
internal object VideoCapabilityProbe {
    fun report(): JSONObject {
        val root = JSONObject(NativePreviewEngine().cameraVideoCapabilitiesSnapshot())
        val outputModes = root.getJSONArray("encoderProbeModes")

        val encoders = JSONArray()
        for (info in MediaCodecList(MediaCodecList.REGULAR_CODECS).codecInfos) {
            if (!info.isEncoder || info.isSoftwareOnly || !info.supportedTypes.contains(MediaFormat.MIMETYPE_VIDEO_HEVC)) {
                continue
            }
            val encoder = JSONObject().put("name", info.name)
            try {
                val caps = info.getCapabilitiesForType(MediaFormat.MIMETYPE_VIDEO_HEVC)
                encoder.put(
                    "main10",
                    caps.profileLevels.any { it.profile == MediaCodecInfo.CodecProfileLevel.HEVCProfileMain10 }
                )
                encoder.put("surface", caps.colorFormats.contains(MediaCodecInfo.CodecCapabilities.COLOR_FormatSurface))
                encoder.put("p010", caps.colorFormats.contains(MediaCodecInfo.CodecCapabilities.COLOR_FormatYUVP010))
                val modes = JSONArray()
                for (index in 0 until outputModes.length()) {
                    val size = outputModes.getJSONObject(index)
                    val width = size.getInt("width")
                    val height = size.getInt("height")
                    for (fps in listOf(24, 30)) {
                        modes.put(
                            JSONObject()
                                .put("width", width)
                                .put("height", height)
                                .put("fps", fps)
                                .put("advertised", caps.videoCapabilities?.areSizeAndRateSupported(
                                    width, height, fps.toDouble()
                                ) == true)
                        )
                    }
                }
                encoder.put("modes", modes)
            } catch (error: Exception) {
                encoder.put("error", error.toString())
            }
            encoders.put(encoder)
        }
        root.put("hardwareHevcEncoders", encoders)
        return root
    }
}
