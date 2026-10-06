package com.rawr.camera.storage

import android.annotation.SuppressLint
import android.content.Context
import android.graphics.ImageFormat
import android.hardware.camera2.CameraCaptureSession
import android.hardware.camera2.CameraDevice
import android.hardware.camera2.CameraManager
import android.hardware.camera2.CameraMetadata
import android.hardware.camera2.CaptureFailure
import android.hardware.camera2.CaptureRequest
import android.hardware.camera2.CaptureResult
import android.hardware.camera2.TotalCaptureResult
import android.hardware.camera2.params.OutputConfiguration
import android.hardware.camera2.params.SessionConfiguration
import android.media.ImageReader
import android.os.Handler
import android.os.HandlerThread
import android.util.Log
import android.util.Size
import java.io.File
import java.util.concurrent.CountDownLatch
import java.util.concurrent.Executor
import java.util.concurrent.TimeUnit
import java.util.concurrent.atomic.AtomicReference

/**
 * Debug experiment, Java Camera2 flavour of the native full-resolution test. vivo's own camera configures a RAW
 * stream at 8192x6144 on camera 2 although nothing advertises it; the NDK refuses the same request with a bare
 * status code. The Java API reports why. Needs the camera free (open this from Settings). The report is kept in
 * the app's files directory and goes into the diagnostics bundle.
 */
internal object FullResJavaProbe {
    const val REPORT_FILE = "rawrcam_fullres_java_probe.txt"
    private const val TAG = "RawrFullRes"
    private const val STEP_SECONDS = 8L

    @SuppressLint("MissingPermission")
    fun run(context: Context): String {
        val report = StringBuilder()
        val log: (String) -> Unit = { line ->
            report.appendLine(line)
            Log.i(TAG, line)
        }
        val manager = context.getSystemService(CameraManager::class.java)
        val thread = HandlerThread("fullres-probe").also { it.start() }
        val handler = Handler(thread.looper)
        val executor = Executor { task -> handler.post(task) }
        log("FULLRES_JAVA_BEGIN cameraIdList=${manager.cameraIdList.joinToString()}")
        for (id in listOf("2", "3", "4")) {
            try {
                probeCamera(manager, id, handler, executor, log)
            } catch (t: Throwable) {
                log("FULLRES_JAVA camera=$id fatal ${t.javaClass.simpleName}: ${t.message}")
            }
        }
        log("FULLRES_JAVA_END")
        thread.quitSafely()
        runCatching { File(context.filesDir, REPORT_FILE).writeText(report.toString()) }
        return report.toString()
    }

    @SuppressLint("MissingPermission")
    private fun probeCamera(
        manager: CameraManager,
        id: String,
        handler: Handler,
        executor: Executor,
        log: (String) -> Unit
    ) {
        val chars = manager.getCameraCharacteristics(id)
        val map = chars.get(android.hardware.camera2.CameraCharacteristics.SCALER_STREAM_CONFIGURATION_MAP)
        val rawSizes = map?.getOutputSizes(ImageFormat.RAW_SENSOR)?.toList().orEmpty()
        log(
            "FULLRES_JAVA camera=$id rawSizes=${rawSizes.joinToString()} " +
                "highRes=${map?.getHighResolutionOutputSizes(ImageFormat.RAW_SENSOR)?.toList().orEmpty().joinToString()}"
        )
        val base = rawSizes.maxByOrNull { it.width * it.height } ?: return

        val opened = AtomicReference<CameraDevice?>()
        val openError = AtomicReference<String?>()
        val openLatch = CountDownLatch(1)
        manager.openCamera(
            id,
            object : CameraDevice.StateCallback() {
                override fun onOpened(camera: CameraDevice) {
                    opened.set(camera)
                    openLatch.countDown()
                }

                override fun onDisconnected(camera: CameraDevice) {
                    camera.close()
                    openError.set("disconnected")
                    openLatch.countDown()
                }

                override fun onError(camera: CameraDevice, error: Int) {
                    camera.close()
                    openError.set("error=$error")
                    openLatch.countDown()
                }
            },
            handler
        )
        openLatch.await(STEP_SECONDS, TimeUnit.SECONDS)
        val device = opened.get()
        if (device == null) {
            log("FULLRES_JAVA camera=$id open_failed ${openError.get() ?: "timeout"}")
            return
        }
        try {
            trial(device, id, "base", base, null, pixelMode = false, legacy = false, handler, executor, log)
            for (factor in listOf(2, 4)) {
                val size = Size(base.width * factor, base.height * factor)
                val name = "${size.width}x${size.height}"
                trial(device, id, "${name}_alone", size, null, pixelMode = false, legacy = false, handler, executor, log)
                trial(device, id, "${name}_alone_maxres", size, null, pixelMode = true, legacy = false, handler, executor, log)
                trial(device, id, "${name}_with_base", size, base, pixelMode = false, legacy = false, handler, executor, log)
                trial(device, id, "${name}_legacy", size, null, pixelMode = false, legacy = true, handler, executor, log)
            }
        } finally {
            device.close()
        }
    }

    @Suppress("DEPRECATION", "LongParameterList")
    private fun trial(
        device: CameraDevice,
        id: String,
        label: String,
        size: Size,
        withBase: Size?,
        pixelMode: Boolean,
        legacy: Boolean,
        handler: Handler,
        executor: Executor,
        log: (String) -> Unit
    ) {
        val head = "FULLRES_JAVA camera=$id trial=$label"
        val reader = ImageReader.newInstance(size.width, size.height, ImageFormat.RAW_SENSOR, 1)
        val baseReader = withBase?.let { ImageReader.newInstance(it.width, it.height, ImageFormat.RAW_SENSOR, 1) }
        val imageLatch = CountDownLatch(1)
        val imageInfo = AtomicReference<String?>()
        reader.setOnImageAvailableListener(
            { r ->
                val image = r.acquireLatestImage()
                if (image != null) {
                    imageInfo.set(
                        "delivered=${image.width}x${image.height} bytes=${image.planes[0].buffer.remaining()} " +
                            "rowStride=${image.planes[0].rowStride}"
                    )
                    image.close()
                }
                imageLatch.countDown()
            },
            handler
        )
        val sessionRef = AtomicReference<CameraCaptureSession?>()
        val sessionLatch = CountDownLatch(1)
        val callback =
            object : CameraCaptureSession.StateCallback() {
                override fun onConfigured(session: CameraCaptureSession) {
                    sessionRef.set(session)
                    sessionLatch.countDown()
                }

                override fun onConfigureFailed(session: CameraCaptureSession) {
                    sessionLatch.countDown()
                }
            }
        try {
            if (legacy) {
                device.createCaptureSession(listOfNotNull(reader.surface, baseReader?.surface), callback, handler)
            } else {
                val configs = mutableListOf(OutputConfiguration(reader.surface))
                if (pixelMode) configs[0].addSensorPixelModeUsed(CameraMetadata.SENSOR_PIXEL_MODE_MAXIMUM_RESOLUTION)
                baseReader?.let { configs.add(OutputConfiguration(it.surface)) }
                device.createCaptureSession(
                    SessionConfiguration(SessionConfiguration.SESSION_REGULAR, configs, executor, callback)
                )
            }
        } catch (t: Throwable) {
            log("$head result=create_threw ${t.javaClass.simpleName}: ${t.message}")
            reader.close()
            baseReader?.close()
            return
        }
        sessionLatch.await(STEP_SECONDS, TimeUnit.SECONDS)
        val session = sessionRef.get()
        if (session == null) {
            log("$head result=configure_failed_or_timeout")
            reader.close()
            baseReader?.close()
            return
        }
        try {
            val request =
                device.createCaptureRequest(CameraDevice.TEMPLATE_STILL_CAPTURE).apply {
                    addTarget(reader.surface)
                    if (pixelMode) set(CaptureRequest.SENSOR_PIXEL_MODE, CameraMetadata.SENSOR_PIXEL_MODE_MAXIMUM_RESOLUTION)
                }
            val resultInfo = AtomicReference("")
            session.capture(
                request.build(),
                object : CameraCaptureSession.CaptureCallback() {
                    override fun onCaptureCompleted(
                        s: CameraCaptureSession,
                        r: CaptureRequest,
                        result: TotalCaptureResult
                    ) {
                        resultInfo.set(
                            "pixelMode=${result.get(CaptureResult.SENSOR_PIXEL_MODE)} " +
                                "crop=${result.get(CaptureResult.SCALER_CROP_REGION)}"
                        )
                    }

                    override fun onCaptureFailed(s: CameraCaptureSession, r: CaptureRequest, f: CaptureFailure) {
                        resultInfo.set("captureFailed reason=${f.reason}")
                    }
                },
                handler
            )
            val got = imageLatch.await(STEP_SECONDS, TimeUnit.SECONDS)
            log("$head result=${if (got) "image" else "no_image"} requested=${size.width}x${size.height} " +
                "${imageInfo.get().orEmpty()} ${resultInfo.get()}")
        } catch (t: Throwable) {
            log("$head result=capture_threw ${t.javaClass.simpleName}: ${t.message}")
        } finally {
            session.close()
            reader.close()
            baseReader?.close()
        }
    }
}
