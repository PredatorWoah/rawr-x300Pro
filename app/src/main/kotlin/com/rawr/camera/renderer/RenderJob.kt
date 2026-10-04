package com.rawr.camera.renderer

internal data class RenderJob(val id: String, val name: String, val capture: String = "",
    val original: String = "", val draft: String = "", val width: Int = 0, val height: Int = 0,
    val orientation: Int = 1, val rawr: Boolean = false, val megapixels: Double = 0.0,
    val status: String = "Editing", val error: String = "", val output: String = "", val runRecipe: String = "")
