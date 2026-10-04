package com.rawr.camera.model

import com.rawr.camera.settings.model.FilmControl
import com.rawr.camera.settings.model.FilmSimCatalog
import com.rawr.camera.settings.model.FilmSimDiscreteField
import com.rawr.camera.settings.model.FilmSimFlag
import com.rawr.camera.settings.model.FilmSimNumericParameter
import com.rawr.camera.settings.model.FilmStocks
import kotlin.test.*

class FilmSimQuickStateTest {
    private fun allCatalogControls() = FilmSimCatalog.sections
        .flatMap { it.subsections }
        .flatMap { it.controls }

    @Test fun everyNumericSpecIsInTheCatalog() {
        val numericParams = allCatalogControls()
            .filterIsInstance<FilmControl.Numeric>()
            .map { it.parameter }
            .toSet()
        val missing = FilmSimNumericParameter.entries.filter { it !in numericParams }
        assertTrue(missing.isEmpty(), "Numerics missing from catalog: $missing")
    }

    @Test fun everyDiscreteFieldIsInTheCatalog() {
        val discreteFields = allCatalogControls()
            .filterIsInstance<FilmControl.Discrete>()
            .map { it.field }
            .toSet()
        // Input space is intentionally hidden everywhere (fixed linear sRGB).
        val missing = FilmSimDiscreteField.entries
            .filter { it !in discreteFields && it != FilmSimDiscreteField.InputColorSpace }
        assertTrue(missing.isEmpty(), "Discrete fields missing from catalog: $missing")
        assertFalse(
            FilmSimDiscreteField.InputColorSpace in discreteFields,
            "Input space has no effect and must stay hidden"
        )
    }

    @Test fun outputSpaceOffersOnlyPreviewVerifiedEncodings() {
        val supported = FilmStocks.previewSupportedOutputSpaces
        assertEquals(setOf(17, 18, 24, 25), supported)
        val labels = supported.sorted().map { FilmStocks.colorSpaces[it] }
        assertEquals(listOf("sRGB", "Display P3", "Rec.709 G2.2", "Rec.709 G2.4"), labels)
    }

    @Test fun everyFlagIsInTheCatalog() {
        val flags = allCatalogControls()
            .filterIsInstance<FilmControl.Flag>()
            .map { it.flag }
            .toSet()
        val missing = FilmSimFlag.entries.filter { it !in flags }
        assertTrue(missing.isEmpty(), "Flags missing from catalog: $missing")
    }

    @Test fun numericShortLabelsFitTheStrip() {
        // Numerics must never fall back to the raw uppercase spec label:
        // every known param has a hand-tuned short label of 10 chars max.
        val long = FilmSimNumericParameter.entries.mapNotNull { param ->
            val label = FilmSimCatalog.shortLabel(
                com.rawr.camera.settings.model.FilmControl.Numeric(param)
            )
            label.takeIf { it.length > 10 }?.let { param.name to it }
        }.toMap()
        assertTrue(long.isEmpty(), "Numerics needing short labels: $long")
    }

    @Test fun stripShortLabelsArePresentable() {
        val labels = allCatalogControls().map { FilmSimCatalog.shortLabel(it) } +
            FilmSimCatalog.sections.flatMap { it.subsections }.map { it.shortLabel }
        assertTrue(labels.isNotEmpty())
        assertTrue(labels.all { it.isNotBlank() }, "Short labels must not be blank")
        assertTrue(
            labels.all { it.length <= 16 },
            "Short labels must stay compact: ${labels.filter { it.length > 16 }}"
        )
    }

    @Test fun valueFormattingNeverShowsNegativeZero() {
        assertEquals("+0.0", formatFilmStripValue(-0.04f, -5f, 1))
        assertEquals("+0.3", formatFilmStripValue(0.3f, -5f, 1))
        assertEquals("-0.3", formatFilmStripValue(-0.3f, -5f, 1))
        assertEquals("1.00", formatFilmStripValue(1f, 0f, 2))
        assertEquals("0.060", formatFilmStripValue(0.06f, 0f, 3))
    }
}
