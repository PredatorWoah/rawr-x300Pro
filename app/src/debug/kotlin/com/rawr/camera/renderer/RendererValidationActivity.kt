package com.rawr.camera.renderer

import android.app.Activity
import android.os.Bundle
import android.os.ParcelFileDescriptor
import com.rawr.camera.settings.model.SettingsCatalog
import com.rawr.camera.settings.model.DemosaicAlgorithm
import org.json.JSONObject
import java.io.File

/** Debug-only batch harness. Inputs and outputs stay in the app's private validation directory. */
class RendererValidationActivity : Activity() {
    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        Thread {
            val dir = File(filesDir, "renderer_validation").apply { mkdirs() }
            var handle = 0L
            val report = JSONObject()
            try {
                handle = RendererNative.open(File(dir, "input.dng").absolutePath, filesDir.absolutePath, applicationInfo.nativeLibraryDir, assets)
                report.put("metadata", RendererNative.inspect(handle))
                val recipe = RendererRecipe.encode(RendererRecipe.neutral(SettingsCatalog.initialState().values))
                val started = System.currentTimeMillis()
                val pixels = RendererNative.render(handle, recipe, 512, 384, -1)
                report.put("previewPixels", pixels?.size).put("previewMs", System.currentTimeMillis() - started)
                File(dir, "progress.json").writeText(report.toString())
                for (mp in listOf(25.0, 50.0, 12.5)) {
                    val size = RenderResolution.dimensions(16304, 12240, mp)
                    ParcelFileDescriptor.open(File(dir, "${mp}mp.jpg"), ParcelFileDescriptor.MODE_CREATE or ParcelFileDescriptor.MODE_TRUNCATE or ParcelFileDescriptor.MODE_READ_WRITE).use { fd ->
                        RendererNative.render(handle, recipe, size.first, size.second, fd.fd)
                    }
                    report.put("${mp}mp", "${size.first}x${size.second}")
                    File(dir, "progress.json").writeText(report.toString())
                }
                val filmRecipe = RendererRecipe.encode(RendererRecipe.neutral(SettingsCatalog.initialState().values).copy(filmSimEnabled = true))
                val filmSize = RenderResolution.dimensions(16304, 12240, 25.0)
                ParcelFileDescriptor.open(File(dir, "film-25mp.jpg"), ParcelFileDescriptor.MODE_CREATE or ParcelFileDescriptor.MODE_TRUNCATE or ParcelFileDescriptor.MODE_READ_WRITE).use { fd ->
                    RendererNative.render(handle, filmRecipe, filmSize.first, filmSize.second, fd.fd)
                }
                report.put("film25mp", true)
                val region = intArrayOf(4000, 3000, 1000, 1000)
                check(RendererNative.render(handle, filmRecipe, 354, 354, -1, region, filmSize.first, filmSize.second)?.size == 354 * 354)
                report.put("detail", true)
                RendererNative.close(handle); handle = 0L
                val rawr = File(dir, "rawr.dng")
                if (rawr.exists()) {
                    handle = RendererNative.open(rawr.absolutePath, filesDir.absolutePath, applicationInfo.nativeLibraryDir, assets)
                    val metadata = RendererNative.inspect(handle).split('\n', limit = 5)
                    check(metadata[3] == "1")
                    check(RenderResolution.defaultMegapixels(metadata[0].toInt(), metadata[1].toInt(), true) == 0.0)
                    report.put("rawrMetadata", metadata.take(4).joinToString("/"))
                    val provenance = JSONObject(metadata[4]).getJSONObject("extensions")
                    val captured = provenance.getString("com.rawrcam.processing.recipe.v1")
                    val decoded = RendererRecipe.decode(captured, SettingsCatalog.initialState().values)
                    check(decoded.filmSimEnabled == JSONObject(captured).getBoolean("filmSimEnabled"))
                    check(RendererNative.render(handle, RendererRecipe.encode(decoded.copy(filmSimEnabled = false)), 1024, 768, -1)?.size == 1024 * 768)
                    report.put("rawrRecipe", true)
                    for (algorithm in listOf(DemosaicAlgorithm.Vng4, DemosaicAlgorithm.DualRcdVng4)) {
                        val configured = decoded.copy(filmSimEnabled = false, demosaicAlgorithm = algorithm)
                        val encoded = RendererRecipe.encode(configured)
                        check(RendererRecipe.decode(encoded, SettingsCatalog.initialState().values).demosaicAlgorithm == algorithm)
                        ParcelFileDescriptor.open(File(dir, "${algorithm.name}.jpg"), ParcelFileDescriptor.MODE_CREATE or ParcelFileDescriptor.MODE_TRUNCATE or ParcelFileDescriptor.MODE_READ_WRITE).use { fd ->
                            RendererNative.render(handle, encoded, 1024, 768, fd.fd)
                        }
                        report.put(algorithm.name, true)
                        File(dir, "progress.json").writeText(report.toString())
                    }
                }
                report.put("totalMs", System.currentTimeMillis() - started).put("success", true)
            } catch (e: Throwable) { report.put("error", e.stackTraceToString()); android.util.Log.e("RendererValidation", "Batch failed", e) }
            finally { if (handle != 0L) RendererNative.close(handle); File(dir, "result.json").writeText(report.toString()); runOnUiThread { finish() } }
        }.start()
    }
}
