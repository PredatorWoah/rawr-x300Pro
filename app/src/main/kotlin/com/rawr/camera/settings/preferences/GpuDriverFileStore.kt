package com.rawr.camera.settings.preferences

import androidx.core.content.edit
import android.content.Context
import android.net.Uri
import java.io.File
import java.util.zip.ZipInputStream
import org.json.JSONObject

data class ImportedGpuDriver(val displayName: String, val libraryPath: String)

/** Owns imported app-local Vulkan driver files and the tiny startup bootstrap flag. */
class GpuDriverFileStore(context: Context) {
    private val app = context.applicationContext
    private val root = File(app.filesDir, "gpu_driver")
    private val prefs = app.getSharedPreferences("gpu_driver_bootstrap", Context.MODE_PRIVATE)

    fun importZip(uri: Uri): ImportedGpuDriver {
        val staging =
            File(root.parentFile, "gpu_driver_staging").apply {
                deleteRecursively()
                mkdirs()
            }
        var metaText: String? = null
        val extracted = mutableListOf<File>()
        app.contentResolver.openInputStream(uri).use { input ->
            requireNotNull(input) { "Unable to open GPU driver package" }
            ZipInputStream(input.buffered()).use { zip ->
                while (true) {
                    val entry = zip.nextEntry ?: break
                    if (entry.isDirectory) continue
                    val base = File(entry.name).name
                    if (base == "meta.json") {
                        metaText = zip.readBytes().toString(Charsets.UTF_8)
                    } else if (base.endsWith(".so")) {
                        val out = File(staging, base)
                        out.outputStream().use { zip.copyTo(it) }
                        require(out.length() in 1..(256L * 1024L * 1024L)) { "Invalid GPU driver library size" }
                        out.setReadable(true, true)
                        out.setExecutable(true, true)
                        out.setWritable(false, false)
                        extracted += out
                    }
                }
            }
        }
        require(extracted.isNotEmpty()) { "GPU driver ZIP contains no .so library" }
        val meta = metaText?.let { runCatching { JSONObject(it) }.getOrNull() }
        val libraryName =
            meta?.optString("libraryName")?.takeIf { it.isNotBlank() }
                ?: extracted.firstOrNull { it.name == "libvulkan_freedreno.so" }?.name
                ?: extracted.first().name
        require(extracted.any { it.name == libraryName }) { "Driver library '$libraryName' is missing from ZIP" }
        val displayName = meta?.optString("name")?.takeIf { it.isNotBlank() } ?: libraryName
        root.deleteRecursively()
        require(staging.renameTo(root)) { "Unable to install GPU driver" }
        prefs.edit {
            putString(KEY_LIBRARY, libraryName)
            putString(KEY_NAME, displayName)
        }
        return ImportedGpuDriver(displayName, File(root, libraryName).absolutePath)
    }

    fun setEnabled(enabled: Boolean) {
        prefs.edit { putBoolean(KEY_ENABLED, enabled) }
    }

    fun activeDriverPath(): String? {
        if (!prefs.getBoolean(KEY_ENABLED, false)) return null
        val name = prefs.getString(KEY_LIBRARY, null) ?: return null
        return File(root, name).takeIf { it.isFile }?.absolutePath
    }

    private companion object {
        const val KEY_ENABLED = "enabled"
        const val KEY_LIBRARY = "library"
        const val KEY_NAME = "name"
    }
}
