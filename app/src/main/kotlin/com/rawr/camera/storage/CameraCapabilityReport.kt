package com.rawr.camera.storage

import android.content.Context
import android.graphics.ImageFormat
import android.hardware.camera2.CameraCharacteristics
import android.hardware.camera2.CameraManager
import android.hardware.camera2.CameraMetadata
import android.util.Size

/**
 * Plain-text dump of what each camera advertises, written into the diagnostics bundle.
 *
 * It answers questions the app cannot settle on its own: whether a sensor offers a full-resolution (50 / 200 MP) RAW
 * mode through the standard maximum-resolution tables, and which vendor-specific request, session and result keys the
 * HAL exposes (the way a vendor mode such as a forced sensor mode would be selected).
 */
internal object CameraCapabilityReport {
    fun build(context: Context): String {
        val manager = context.applicationContext.getSystemService(CameraManager::class.java)
            ?: return "No CameraManager\n"
        val out = StringBuilder()
        val ids = LinkedHashSet<String>()
        val listed = runCatching { manager.cameraIdList.toList() }.getOrDefault(emptyList())
        ids += listed
        for (id in listed) {
            runCatching { manager.getCameraCharacteristics(id).physicalCameraIds }.getOrNull()?.let { ids += it }
        }
        // Hidden physical ids that profiles route to directly.
        for (n in 0..15) ids += n.toString()
        out.appendLine("# Camera capability report")
        out.appendLine("listed ids: $listed")
        for (id in ids) {
            val chars = runCatching { manager.getCameraCharacteristics(id) }.getOrNull()
            if (chars == null) {
                out.appendLine("\n== camera $id: not visible to the Java CameraManager (the native discovery line still covers it)")
                continue
            }
            describe(out, id, chars)
        }
        return out.toString()
    }

    private fun describe(out: StringBuilder, id: String, chars: CameraCharacteristics) {
        out.appendLine("\n== camera $id")
        out.appendLine("facing: ${chars.get(CameraCharacteristics.LENS_FACING)}")
        out.appendLine("physical ids: ${chars.physicalCameraIds}")
        out.appendLine("focal lengths: ${chars.get(CameraCharacteristics.LENS_INFO_AVAILABLE_FOCAL_LENGTHS)?.toList()}")
        out.appendLine("pixel array: ${chars.get(CameraCharacteristics.SENSOR_INFO_PIXEL_ARRAY_SIZE)}")
        out.appendLine("active array: ${chars.get(CameraCharacteristics.SENSOR_INFO_ACTIVE_ARRAY_SIZE)}")
        out.appendLine(
            "max-resolution pixel array: ${chars.get(CameraCharacteristics.SENSOR_INFO_PIXEL_ARRAY_SIZE_MAXIMUM_RESOLUTION)}"
        )
        out.appendLine(
            "max-resolution active array: ${chars.get(CameraCharacteristics.SENSOR_INFO_ACTIVE_ARRAY_SIZE_MAXIMUM_RESOLUTION)}"
        )
        val capabilities = chars.get(CameraCharacteristics.REQUEST_AVAILABLE_CAPABILITIES)?.toList().orEmpty()
        out.appendLine("capabilities: $capabilities")
        out.appendLine(
            "ultra high resolution sensor capability: " +
                (CameraMetadata.REQUEST_AVAILABLE_CAPABILITIES_ULTRA_HIGH_RESOLUTION_SENSOR in capabilities)
        )
        out.appendLine("RAW sensor sizes, default: ${sizes(chars, ImageFormat.RAW_SENSOR, false)}")
        out.appendLine("RAW10 sizes, default: ${sizes(chars, ImageFormat.RAW10, false)}")
        out.appendLine("RAW sensor sizes, maximum resolution: ${sizes(chars, ImageFormat.RAW_SENSOR, true)}")
        out.appendLine("RAW10 sizes, maximum resolution: ${sizes(chars, ImageFormat.RAW10, true)}")
        out.appendLine("max resolution YUV/JPEG: ${sizes(chars, ImageFormat.JPEG, true)}")
        out.appendLine("vendor characteristics keys: ${vendor(chars.keys.map { it.name })}")
        out.appendLine("vendor request keys: ${vendor(chars.availableCaptureRequestKeys.map { it.name })}")
        out.appendLine("vendor session keys: ${vendor(chars.availableSessionKeys.orEmpty().map { it.name })}")
        out.appendLine("vendor result keys: ${vendor(chars.availableCaptureResultKeys.map { it.name })}")
    }

    private fun sizes(chars: CameraCharacteristics, format: Int, maximumResolution: Boolean): List<String> {
        val key =
            if (maximumResolution) {
                CameraCharacteristics.SCALER_STREAM_CONFIGURATION_MAP_MAXIMUM_RESOLUTION
            } else {
                CameraCharacteristics.SCALER_STREAM_CONFIGURATION_MAP
            }
        val map = runCatching { chars.get(key) }.getOrNull() ?: return emptyList()
        val found: Array<Size> = runCatching { map.getOutputSizes(format) }.getOrNull() ?: return emptyList()
        return found.map { "${it.width}x${it.height}" }
    }

    // Standard AOSP sections all start with "android."; everything else is vendor-defined.
    private fun vendor(names: List<String>): List<String> = names.filterNot { it.startsWith("android.") }.sorted()
}
