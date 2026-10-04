package com.rawr.camera.renderer

import android.content.Context
import com.rawr.camera.integration.CaptureRecipe
import com.rawr.camera.settings.model.ProfileTone
import com.rawr.camera.settings.model.SettingsCatalog
import com.rawr.camera.settings.preferences.LutProfileCodec
import com.rawr.camera.settings.preferences.LutProfileFileStore
import com.rawr.camera.storage.releaseCaptureAssets
import java.io.File
import java.io.FileOutputStream
import org.json.JSONObject

/** Resolves, validates, and owns immutable LUT copies for renderer jobs. */
internal class RendererAssets(private val context: Context, private val directory: (String) -> File) {
    /**
     * Memoized SHA-256 over LUT content files, keyed by path + mtime + size.
     * resolveAssets/validateAssets re-hash the same .cube files on every
     * preview tick; content is keyed by stat so unchanged files hash once.
     * Cleared (never wrong, just re-hashed) if it grows past a small bound.
     */
    private val shaMemo = LinkedHashMap<String, String>()
    private fun sha256Of(file: File): String {
        val key = "${file.path}|${file.lastModified()}|${file.length()}"
        synchronized(shaMemo) {
            shaMemo[key]?.let { return it }
            val hash = java.security.MessageDigest.getInstance("SHA-256")
            file.inputStream().use { input -> val buffer = ByteArray(65536)
                while (true) { val count = input.read(buffer); if (count < 0) break; hash.update(buffer, 0, count) }
            }
            val sha = hash.digest().joinToString("") { "%02x".format(it) }
            if (shaMemo.size > 64) shaMemo.clear()
            shaMemo[key] = sha
            return sha
        }
    }
    /** DNG recipes are portable: locate installed LUT content even when capture paths differ. */
    fun resolve(recipe: String): String {
        val json = JSONObject(recipe)
        val stages = json.optJSONObject("importedProfile")?.optJSONArray("stages") ?: return recipe
        val root = context.filesDir.canonicalFile
        for (i in 0 until stages.length()) {
            val stage = stages.getJSONObject(i)
            val expected = stage.optString("sha256").takeIf { it.matches(Regex("[a-f0-9]{64}")) } ?: continue
            val requested = File(root, stage.getString("relativePath")).canonicalFile
            fun matches(file: File) = file.path.startsWith(root.path + File.separator) && file.isFile &&
                sha256Of(file) == expected
            if (!matches(requested)) {
                File(root, "lut_profiles/files").listFiles().orEmpty().firstOrNull { it.extension == "cube" && matches(it) }?.let {
                    stage.put("relativePath", it.relativeTo(root).path)
                }
            }
        }
        return json.toString()
    }
    fun validate(recipe: String) {
        val j = JSONObject(recipe)
        val id = j.optString("importedProfileId").takeUnless { it.isBlank() || it == "null" } ?: return
        val profile = j.optJSONObject("importedProfile") ?: error("Missing LUT profile metadata; select a profile")
        val stages = profile.getJSONArray("stages")
        for (i in 0 until stages.length()) {
            val stage = stages.getJSONObject(i)
            val root = context.filesDir.canonicalFile
            val file = File(root, stage.getString("relativePath")).canonicalFile
            check(file.path.startsWith(root.path + File.separator) && file.isFile) { "Missing LUT for $id; select a replacement profile" }
            if (stage.has("sha256")) {
                check(sha256Of(file) == stage.getString("sha256")) { "LUT content changed; select a replacement profile" }
            }
        }
    }
    fun prepareRecipe(job: RenderJob, requested: String): String {
        val recipe = resolve(requested)
        val j = JSONObject(recipe)
        val v = RendererRecipe.decode(recipe, com.rawr.camera.settings.model.SettingsCatalog.initialState().values)
        val selected = v.userLutProfiles.firstOrNull { it.id == v.selectedUserLutProfileId } ?: return recipe
        if (directory(job.id).listFiles().orEmpty().filter { it.name.startsWith("asset-") }.any { file ->
                runCatching { LutProfileCodec.decode(file.readText()).any { it.id == selected.id } }.getOrDefault(false)
            }) { validate(recipe); return recipe }
        val signature = java.security.MessageDigest.getInstance("SHA-256").digest(
            LutProfileCodec.encode(listOf(selected.copy(tone = ProfileTone.Neutral))).toByteArray()).joinToString("") { "%02x".format(it) }
        val record = File(directory(job.id), "asset-$signature")
        val frozen = if (record.isFile) LutProfileCodec.decode(record.readText()).single() else {
            validate(recipe)
            LutProfileFileStore(context).freezeForCapture(selected).also { profile ->
                FileOutputStream(record).use { it.write(LutProfileCodec.encode(listOf(profile)).toByteArray()); it.fd.sync() }
            }
        }
        val snapshot = v.copy(selectedUserLutProfileId = frozen.id, userLutProfiles = v.userLutProfiles + frozen.copy(tone = selected.tone))
        val encoded = JSONObject(CaptureRecipe.encode(snapshot, context.filesDir))
        j.put("importedProfileId", frozen.id); j.put("importedProfile", encoded.getJSONObject("importedProfile"))
        return j.toString().also(::validate)
    }
    fun release(job: RenderJob) {
        directory(job.id).listFiles().orEmpty().filter { it.name.startsWith("asset-") }.forEach { file ->
            runCatching { LutProfileCodec.decode(file.readText()).forEach { releaseCaptureAssets(context.filesDir, it.id) } }
        }
    }
}
