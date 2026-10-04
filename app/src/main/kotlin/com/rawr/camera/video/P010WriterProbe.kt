package com.rawr.camera.video

import android.graphics.ImageFormat
import android.media.ImageWriter
import android.media.MediaCodec
import android.media.MediaCodecInfo
import android.media.MediaFormat
import org.json.JSONObject

/** Tests whether the encoder's input Surface can dequeue GPU-shareable P010 buffers. */
internal object P010WriterProbe {
    fun run(): JSONObject {
        val report = JSONObject()
        var codec: MediaCodec? = null
        var writer: ImageWriter? = null
        var surface: android.view.Surface? = null
        try {
            codec = MediaCodec.createByCodecName("c2.qti.hevc.encoder")
            val format = MediaFormat.createVideoFormat(MediaFormat.MIMETYPE_VIDEO_HEVC, 1920, 1080).apply {
                setInteger(MediaFormat.KEY_COLOR_FORMAT, MediaCodecInfo.CodecCapabilities.COLOR_FormatSurface)
                setInteger(MediaFormat.KEY_PROFILE, MediaCodecInfo.CodecProfileLevel.HEVCProfileMain10)
                setInteger(MediaFormat.KEY_BIT_RATE, 12_000_000)
                setInteger(MediaFormat.KEY_FRAME_RATE, 30)
                setInteger(MediaFormat.KEY_I_FRAME_INTERVAL, 1)
            }
            codec.configure(format, null, null, MediaCodec.CONFIGURE_FLAG_ENCODE)
            surface = codec.createInputSurface()
            writer = ImageWriter.Builder(surface)
                .setImageFormat(ImageFormat.YCBCR_P010)
                .setMaxImages(3)
                .build()
            codec.start()
            val image = writer.dequeueInputImage()
            report.put("imageFormat", image.format)
            report.put("p010", image.format == ImageFormat.YCBCR_P010)
            report.put("width", image.width)
            report.put("height", image.height)
            report.put("planes", image.planes.size)
            val hardwareBuffer = image.hardwareBuffer
            report.put("hardwareBuffer", hardwareBuffer != null)
            if (hardwareBuffer != null) {
                report.put("hardwareBufferFormat", hardwareBuffer.format)
                report.put("hardwareBufferUsage", hardwareBuffer.usage.toString())
                val gpu = VideoGpuProbe.inspectHardwareBuffer(hardwareBuffer)
                report.put("vulkanFormat", gpu[0])
                report.put("vulkanExternalFormat", gpu[1].toString())
                report.put("vulkanFormatFeatures", gpu[2].toString())
                report.put("vulkanTransferQuery", gpu[3])
                report.put("vulkanStorageQuery", gpu[4])
                report.put("vulkanTransferRequiredUsage", gpu[5].toString())
                report.put("vulkanStorageRequiredUsage", gpu[6].toString())
                report.put("vulkanTransferExternalFeatures", gpu[7])
                report.put("vulkanStorageExternalFeatures", gpu[8])
                hardwareBuffer.close()
            }
            image.close()
            report.put("success", true)
        } catch (error: Throwable) {
            report.put("success", false)
            report.put("error", error.toString())
        } finally {
            runCatching { codec?.stop() }
            writer?.close()
            surface?.release()
            codec?.release()
        }
        return report
    }
}
