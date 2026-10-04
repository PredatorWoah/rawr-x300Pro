package com.rawr.camera.video

import android.media.MediaCodec
import android.media.MediaCodecInfo
import android.media.MediaFormat
import android.media.MediaMuxer
import android.opengl.EGL14
import android.opengl.EGLExt
import android.opengl.GLES20
import android.view.Surface
import org.json.JSONObject
import java.io.File

/** Debug-only feasibility probe for a true ten-bit GPU input surface. */
internal object VideoEncoderProbe {
    private const val EGL_RECORDABLE_ANDROID = 0x3142

    fun run(filesDir: File, encoderName: String = "c2.qti.hevc.encoder"): JSONObject {
        val report = JSONObject()
        val output = File(filesDir, "video_main10_surface_probe.mp4")
        report.put("encoder", encoderName)
        report.put("output", output.absolutePath)
        val width = 1920
        val height = 1080
        var codec: MediaCodec? = null
        var input: Surface? = null
        var muxer: MediaMuxer? = null
        var display = EGL14.EGL_NO_DISPLAY
        var context = EGL14.EGL_NO_CONTEXT
        var eglSurface = EGL14.EGL_NO_SURFACE
        var muxStarted = false
        try {
            val format = MediaFormat.createVideoFormat(MediaFormat.MIMETYPE_VIDEO_HEVC, width, height).apply {
                setInteger(MediaFormat.KEY_COLOR_FORMAT, MediaCodecInfo.CodecCapabilities.COLOR_FormatSurface)
                setInteger(MediaFormat.KEY_PROFILE, MediaCodecInfo.CodecProfileLevel.HEVCProfileMain10)
                setInteger(MediaFormat.KEY_BIT_RATE, 12_000_000)
                setInteger(MediaFormat.KEY_FRAME_RATE, 30)
                setInteger(MediaFormat.KEY_I_FRAME_INTERVAL, 1)
                setInteger(MediaFormat.KEY_COLOR_STANDARD, MediaFormat.COLOR_STANDARD_BT709)
                setInteger(MediaFormat.KEY_COLOR_TRANSFER, MediaFormat.COLOR_TRANSFER_SDR_VIDEO)
                setInteger(MediaFormat.KEY_COLOR_RANGE, MediaFormat.COLOR_RANGE_LIMITED)
            }
            codec = MediaCodec.createByCodecName(encoderName)
            codec.configure(format, null, null, MediaCodec.CONFIGURE_FLAG_ENCODE)
            input = codec.createInputSurface()
            report.put("vulkanSurface", VideoGpuProbe.inspectEncoderSurface(input).joinToString(","))

            display = EGL14.eglGetDisplay(EGL14.EGL_DEFAULT_DISPLAY)
            check(display != EGL14.EGL_NO_DISPLAY && EGL14.eglInitialize(display, null, 0, null, 0)) {
                "EGL display initialization failed"
            }
            val configAttrs = intArrayOf(
                EGL14.EGL_RED_SIZE, 10, EGL14.EGL_GREEN_SIZE, 10,
                EGL14.EGL_BLUE_SIZE, 10, EGL14.EGL_ALPHA_SIZE, 2,
                EGL14.EGL_RENDERABLE_TYPE, EGL14.EGL_OPENGL_ES2_BIT,
                EGL_RECORDABLE_ANDROID, 1, EGL14.EGL_NONE
            )
            val configs = arrayOfNulls<android.opengl.EGLConfig>(1)
            val configCount = IntArray(1)
            val found = EGL14.eglChooseConfig(display, configAttrs, 0, configs, 0, 1, configCount, 0)
            report.put("rgb10EglConfig", found && configCount[0] > 0)
            check(found && configCount[0] > 0) { "No recordable RGB10A2 EGL config" }
            val config = requireNotNull(configs[0])
            context = EGL14.eglCreateContext(display, config, EGL14.EGL_NO_CONTEXT,
                intArrayOf(EGL14.EGL_CONTEXT_CLIENT_VERSION, 2, EGL14.EGL_NONE), 0)
            check(context != EGL14.EGL_NO_CONTEXT) { "EGL 2 context creation failed" }
            eglSurface = EGL14.eglCreateWindowSurface(display, config, input,
                intArrayOf(EGL14.EGL_NONE), 0)
            check(eglSurface != EGL14.EGL_NO_SURFACE) { "RGB10 encoder surface creation failed: ${EGL14.eglGetError()}" }
            check(EGL14.eglMakeCurrent(display, eglSurface, eglSurface, context)) { "eglMakeCurrent failed" }
            var redBits = IntArray(1)
            EGL14.eglGetConfigAttrib(display, config, EGL14.EGL_RED_SIZE, redBits, 0)
            report.put("eglRedBits", redBits[0])
            codec.start()
            muxer = MediaMuxer(output.absolutePath, MediaMuxer.OutputFormat.MUXER_OUTPUT_MPEG_4)
            val bufferInfo = MediaCodec.BufferInfo()
            var track = -1
            var frames = 0
            var eos = false
            val beginNs = System.nanoTime()
            while (!eos && System.nanoTime() - beginNs < 8_000_000_000L) {
                if (frames < 16) {
                    // Different 10-bit code points near mid-gray are useful
                    // for later decoded precision inspection.
                    val level = (512 + frames) / 1023f
                    GLES20.glViewport(0, 0, width, height)
                    GLES20.glClearColor(level, level, level, 1f)
                    GLES20.glClear(GLES20.GL_COLOR_BUFFER_BIT)
                    EGLExt.eglPresentationTimeANDROID(display, eglSurface, frames * 33_333_333L)
                    check(EGL14.eglSwapBuffers(display, eglSurface)) { "eglSwapBuffers failed at frame $frames" }
                    frames++
                    if (frames == 16) codec.signalEndOfInputStream()
                }
                when (val index = codec.dequeueOutputBuffer(bufferInfo, 10_000)) {
                    MediaCodec.INFO_OUTPUT_FORMAT_CHANGED -> {
                        report.put("outputFormat", codec.outputFormat.toString())
                        track = muxer.addTrack(codec.outputFormat)
                        muxer.start()
                        muxStarted = true
                    }
                    in 0..Int.MAX_VALUE -> {
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
            report.put("submittedFrames", frames)
            report.put("eos", eos)
            report.put("success", eos && output.length() > 0)
        } catch (error: Throwable) {
            report.put("success", false)
            report.put("error", error.toString())
        } finally {
            if (display != EGL14.EGL_NO_DISPLAY) {
                EGL14.eglMakeCurrent(display, EGL14.EGL_NO_SURFACE, EGL14.EGL_NO_SURFACE, EGL14.EGL_NO_CONTEXT)
                if (eglSurface != EGL14.EGL_NO_SURFACE) EGL14.eglDestroySurface(display, eglSurface)
                if (context != EGL14.EGL_NO_CONTEXT) EGL14.eglDestroyContext(display, context)
                EGL14.eglTerminate(display)
            }
            runCatching { codec?.stop() }
            codec?.release()
            input?.release()
            if (muxStarted) runCatching { muxer?.stop() }
            muxer?.release()
            report.put("fileBytes", output.length())
        }
        return report
    }
}
