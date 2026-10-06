package com.rawr.camera.settings.preferences

import org.junit.Assert.assertEquals
import org.junit.Assert.assertTrue
import org.junit.Assert.fail
import org.junit.Test

class CubeNormalizerTest {
    private fun table(size: Int): List<String> =
        (0 until size * size * size).map { "0.${it % 10} 0.5 1.0" }

    @Test
    fun acceptsPowerOfTwoSizesAndDropsUnknownMetadata() {
        val lines = listOf("TITLE \"x\"", "# comment", "LUT_3D_INPUT_RANGE 0.0 1.0", "LUT_IN_VIDEO_RANGE 0 1", "LUT_3D_SIZE 16") + table(16)
        val out = CubeNormalizer.normalize(lines.asSequence())
        assertEquals("LUT_3D_SIZE 16", out.first())
        assertEquals(1 + 16 * 16 * 16, out.size)
        assertTrue(out.none { it.startsWith("LUT_3D_INPUT_RANGE") || it.startsWith("TITLE") })
    }

    @Test
    fun keepsDomainLines() {
        val lines = listOf("LUT_3D_SIZE 2", "DOMAIN_MIN 0 0 0", "DOMAIN_MAX 1 1 1") + table(2)
        val out = CubeNormalizer.normalize(lines.asSequence())
        assertTrue(out.contains("DOMAIN_MIN 0 0 0"))
        assertTrue(out.contains("DOMAIN_MAX 1 1 1"))
    }

    @Test
    fun rejectsOneDimensionalTablesOversizeAndShortTables() {
        listOf(
            listOf("LUT_1D_SIZE 16"),
            listOf("LUT_3D_SIZE 66"),
            listOf("LUT_3D_SIZE 1"),
            listOf("LUT_3D_SIZE 4") + table(4).dropLast(1),
            table(2)
        ).forEach { lines ->
            try {
                CubeNormalizer.normalize(lines.asSequence())
                fail("expected rejection for ${lines.first()}")
            } catch (_: IllegalArgumentException) {
                // expected
            }
        }
    }
}
