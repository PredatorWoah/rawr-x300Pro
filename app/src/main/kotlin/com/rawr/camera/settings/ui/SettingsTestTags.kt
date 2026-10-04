package com.rawr.camera.settings.ui

/**
 * Stable automation identifiers for Settings.
 *
 * Titles repeat across sections (`DCG`, `Exposure`, `Film Simulation`) and values embed
 * live state, so UI tests must match on these tags instead of text. Tags are constant
 * across state and navigation. Adding a tag never changes visuals.
 */
object SettingsTestTags {
    const val SCREEN_HOME = "settings_home"
    const val TOPBAR_BACK = "settings_topbar_back"

    fun homeEntry(section: String): String = "settings_home_$section"

    fun sectionRoot(section: String): String = "settings_section_$section"

    fun row(section: String, slug: String): String = "settings_row_${section}_$slug"

    fun selectorRoot(kind: String): String = "settings_selector_$kind"

    fun selectorOption(kind: String, id: String): String = "settings_option_${kind}_$id"

    fun filmSubPage(section: String): String = "settings_filmsim_$section"

    fun filmDetail(detail: String): String = "settings_filmdetail_$detail"

    /** URL-safe slug for building tags from titles. Letters/digits only, lowercase. */
    fun slug(text: String): String {
        val s = text.lowercase().map { if (it.isLetterOrDigit()) it else '_' }.joinToString("")
        return s.trim('_').replace(Regex("_+"), "_").take(48).ifBlank { "row" }
    }
}
