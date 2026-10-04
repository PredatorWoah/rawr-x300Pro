package com.rawr.camera.video

import android.media.MediaCodec
import android.media.MediaCodecInfo
import android.media.MediaFormat
import android.media.MediaMuxer
import android.view.Surface
import org.json.JSONObject
import java.io.File

/** Checks the actual Vulkan RGBA16F swapchain to Main10 encoder path. */
internal object VideoVulkanProbe {
    fun run(filesDir: File): JSONObject {
        val report = JSONObject()
        val output = File(filesDir, "video_vulkan16f_probe.mp4")
        var codec: MediaCodec? = null
        var surface: Surface? = null
        var muxer: MediaMuxer? = null
        var muxStarted = false
        try {
            val format = MediaFormat.createVideoFormat(MediaFormat.MIMETYPE_VIDEO_HEVC, 1920, 1080).apply {
                setInteger(MediaFormat.KEY_COLOR_FORMAT, MediaCodecInfo.CodecCapabilities.COLOR_FormatSurface)
                setInteger(MediaFormat.KEY_PROFILE, MediaCodecInfo.CodecProfileLevel.HEVCProfileMain10)
                setInteger(MediaFormat.KEY_BIT_RATE, 12_000_000)
                setInteger(MediaFormat.KEY_FRAME_RATE, 30)
                setInteger(MediaFormat.KEY_I_FRAME_INTERVAL, 1)
                setInteger(MediaFormat.KEY_COLOR_STANDARD, MediaFormat.COLOR_STANDARD_BT709)
                setInteger(MediaFormat.KEY_COLOR_TRANSFER, MediaFormat.COLOR_TRANSFER_SDR_VIDEO)
                setInteger(MediaFormat.KEY_COLOR_RANGE, MediaFormat.COLOR_RANGE_LIMITED)
            }
            codec = MediaCodec.createByCodecName("c2.qti.hevc.encoder")
            codec.configure(format, null, null, MediaCodec.CONFIGURE_FLAG_ENCODE)
            surface = codec.createInputSurface()
            codec.start()
            val rendered = VideoGpuProbe.renderHalfFloatFrames(surface)
            report.put("vulkanRenderedFrames", rendered)
            check(rendered > 0) { "Vulkan RGBA16F swapchain did not present a frame" }
            codec.signalEndOfInputStream()
            muxer = MediaMuxer(output.absolutePath, MediaMuxer.OutputFormat.MUXER_OUTPUT_MPEG_4)
            val bufferInfo = MediaCodec.BufferInfo()
            val bootBeforeDrainUs = android.os.SystemClock.elapsedRealtimeNanos() / 1_000L
            report.put("bootBeforeDrainUs", bootBeforeDrainUs)
            var track = -1
            var eos = false
            val begin = System.nanoTime()
            while (!eos && System.nanoTime() - begin < 8_000_000_000L) {
                when (val index = codec.dequeueOutputBuffer(bufferInfo, 10_000)) {
                    MediaCodec.INFO_OUTPUT_FORMAT_CHANGED -> {
                        report.put("outputFormat", codec.outputFormat.toString())
                        track = muxer.addTrack(codec.outputFormat)
                        muxer.start()
                        muxStarted = true
                    }
                    in 0..Int.MAX_VALUE -> {
                        if (!report.has("firstCodecPtsUs") && bufferInfo.size > 0)
                            report.put("firstCodecPtsUs", bufferInfo.presentationTimeUs)
                        if (bufferInfo.size > 0) report.put("lastCodecPtsUs", bufferInfo.presentationTimeUs)
                        if (bufferInfo.size > 0 && track >= 0) {
                            val data = requireNotNull(codec.getOutputBuffer(index))
                            data.position(bufferInfo.offset)
                            data.limit(bufferInfo.offset + bufferInfo.size)
                            muxer.writeSampleData(track, data, bufferInfo)
                        }
                        eos = bufferInfo.flags and MediaCodec.BUFFER_FLAG_END_OF_STREAM != 0
                        codec.releaseOutputBuffer(index, false)
                    }
                }
            }
            report.put("eos", eos)
            report.put("success", eos && output.length() > 0)
        } catch (error: Throwable) {
            report.put("success", false)
            report.put("error", error.toString())
        } finally {
            runCatching { codec?.stop() }
            codec?.release()
            surface?.release()
            if (muxStarted) runCatching { muxer?.stop() }
            muxer?.release()
            report.put("fileBytes", output.length())
        }
        return report
    }
}
