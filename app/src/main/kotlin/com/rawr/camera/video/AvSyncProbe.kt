package com.rawr.camera.video

import android.media.MediaCodec
import android.media.MediaCodecInfo
import android.media.MediaFormat
import android.opengl.EGL14
import android.opengl.EGLExt
import android.opengl.GLES20
import android.os.SystemClock
import android.view.Surface
import org.json.JSONObject
import java.io.File

/** Debug exercise of live mic, Main10 video and recoverable fragmented muxing. */
internal object AvSyncProbe {
    fun run(filesDir: File): JSONObject {
        val report = JSONObject()
        val file = File(filesDir, "av_sync_probe.mp4")
        var codec: MediaCodec? = null
        var surface: Surface? = null
        var audio: AudioCaptureEncoder? = null
        var muxer: FragmentedAvMuxer? = null
        var display = EGL14.EGL_NO_DISPLAY
        var context = EGL14.EGL_NO_CONTEXT
        var eglSurface = EGL14.EGL_NO_SURFACE
        try {
            val originNs = SystemClock.elapsedRealtimeNanos()
            muxer = FragmentedAvMuxer(file, originNs)
            audio = AudioCaptureEncoder(muxer, onFailure = { error ->
                android.util.Log.e("RawrVideoProbe", "Audio failed", error)
            })
            val videoFormat = MediaFormat.createVideoFormat(MediaFormat.MIMETYPE_VIDEO_HEVC, 1920, 1080).apply {
                setInteger(MediaFormat.KEY_COLOR_FORMAT, MediaCodecInfo.CodecCapabilities.COLOR_FormatSurface)
                setInteger(MediaFormat.KEY_PROFILE, MediaCodecInfo.CodecProfileLevel.HEVCProfileMain10)
                setInteger(MediaFormat.KEY_BIT_RATE, 12_000_000)
                setInteger(MediaFormat.KEY_FRAME_RATE, 30)
                setInteger(MediaFormat.KEY_I_FRAME_INTERVAL, 1)
                setInteger(MediaFormat.KEY_MAX_B_FRAMES, 0)
                setInteger(MediaFormat.KEY_COLOR_STANDARD, MediaFormat.COLOR_STANDARD_BT709)
                setInteger(MediaFormat.KEY_COLOR_TRANSFER, MediaFormat.COLOR_TRANSFER_SDR_VIDEO)
                setInteger(MediaFormat.KEY_COLOR_RANGE, MediaFormat.COLOR_RANGE_LIMITED)
            }
            codec = MediaCodec.createByCodecName("c2.qti.hevc.encoder")
            codec.configure(videoFormat, null, null, MediaCodec.CONFIGURE_FLAG_ENCODE)
            surface = codec.createInputSurface()
            display = EGL14.eglGetDisplay(EGL14.EGL_DEFAULT_DISPLAY)
            check(EGL14.eglInitialize(display, null, 0, null, 0)) { "EGL initialization failed" }
            val attrs = intArrayOf(EGL14.EGL_RED_SIZE, 10, EGL14.EGL_GREEN_SIZE, 10,
                EGL14.EGL_BLUE_SIZE, 10, EGL14.EGL_ALPHA_SIZE, 2,
                EGL14.EGL_RENDERABLE_TYPE, EGL14.EGL_OPENGL_ES2_BIT,
                0x3142, 1, EGL14.EGL_NONE)
            val configs = arrayOfNulls<android.opengl.EGLConfig>(1)
            val count = IntArray(1)
            check(EGL14.eglChooseConfig(display, attrs, 0, configs, 0, 1, count, 0) && count[0] == 1) {
                "RGB10 EGL config unavailable"
            }
            val config = requireNotNull(configs[0])
            context = EGL14.eglCreateContext(display, config, EGL14.EGL_NO_CONTEXT,
                intArrayOf(EGL14.EGL_CONTEXT_CLIENT_VERSION, 2, EGL14.EGL_NONE), 0)
            eglSurface = EGL14.eglCreateWindowSurface(display, config, surface,
                intArrayOf(EGL14.EGL_NONE), 0)
            check(EGL14.eglMakeCurrent(display, eglSurface, eglSurface, context)) { "EGL current failed" }
            codec.start()
            audio.start()
            val info = MediaCodec.BufferInfo()
            val frameCount = 90
            val firstVideoNs = SystemClock.elapsedRealtimeNanos()
            for (frame in 0 until frameCount) {
                val ptsNs = firstVideoNs + frame * 33_333_333L
                while (SystemClock.elapsedRealtimeNanos() < ptsNs) {
                    Thread.sleep(1)
                }
                val value = (512 + frame % 16) / 1023f
                GLES20.glViewport(0, 0, 1920, 1080)
                GLES20.glClearColor(value, value, value, 1f)
                GLES20.glClear(GLES20.GL_COLOR_BUFFER_BIT)
                EGLExt.eglPresentationTimeANDROID(display, eglSurface, ptsNs)
                check(EGL14.eglSwapBuffers(display, eglSurface)) { "Encoder EGL swap failed" }
                drainVideo(codec, muxer, info, false)
            }
            codec.signalEndOfInputStream()
            drainVideo(codec, muxer, info, true)
            audio.close()
            audio = null
            report.put("videoFrames", frameCount)
            report.put("encodedSamples", muxer.sampleCount())
            muxer.close()
            muxer = null
            report.put("success", file.length() > 0)
        } catch (error: SecurityException) {
            report.put("success", false)
            report.put("error", "Microphone permission denied or revoked: $error")
        } catch (error: Throwable) {
            report.put("success", false)
            report.put("error", error.toString())
        } finally {
            runCatching { audio?.close() }
            runCatching { muxer?.close() }
            if (display != EGL14.EGL_NO_DISPLAY) {
                EGL14.eglMakeCurrent(display, EGL14.EGL_NO_SURFACE, EGL14.EGL_NO_SURFACE,
                    EGL14.EGL_NO_CONTEXT)
                if (eglSurface != EGL14.EGL_NO_SURFACE) EGL14.eglDestroySurface(display, eglSurface)
                if (context != EGL14.EGL_NO_CONTEXT) EGL14.eglDestroyContext(display, context)
                EGL14.eglTerminate(display)
            }
            runCatching { codec?.stop() }
            codec?.release()
            surface?.release()
            report.put("fileBytes", file.length())
        }
        return report
    }

    private fun drainVideo(codec: MediaCodec, muxer: FragmentedAvMuxer,
        info: MediaCodec.BufferInfo, untilEos: Boolean) {
        val deadline = SystemClock.elapsedRealtimeNanos() + if (untilEos) 5_000_000_000L else 0L
        do {
            when (val index = codec.dequeueOutputBuffer(info, if (untilEos) 10_000 else 0)) {
                MediaCodec.INFO_OUTPUT_FORMAT_CHANGED ->
                    muxer.setFormat(FragmentedAvMuxer.Track.Video, codec.outputFormat)
                in 0..Int.MAX_VALUE -> {
                    val eos = info.flags and MediaCodec.BUFFER_FLAG_END_OF_STREAM != 0
                    codec.getOutputBuffer(index)?.let { muxer.write(FragmentedAvMuxer.Track.Video, it, info) }
                    codec.releaseOutputBuffer(index, false)
                    if (eos) return
                }
                else -> if (!untilEos) return
            }
        } while (!untilEos || SystemClock.elapsedRealtimeNanos() < deadline)
        if (untilEos) error("HEVC encoder did not emit EOS in five seconds")
    }
}
