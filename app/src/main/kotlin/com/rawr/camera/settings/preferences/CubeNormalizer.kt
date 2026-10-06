package com.rawr.camera.settings.preferences

/**
 * Turns a `.cube` file as found in the wild into the subset the renderer parses. The renderer accepts 3D tables from
 * 2 to 65 points and `DOMAIN_MIN` / `DOMAIN_MAX`, and rejects every other directive, so metadata that tools add
 * (`LUT_3D_INPUT_RANGE`, `LUT_IN_VIDEO_RANGE`, `TITLE`, comments) is dropped here instead of failing the import.
 * 1D tables stay unsupported.
 */
internal object CubeNormalizer {
    const val MIN_SIZE = 2
    const val MAX_SIZE = 65

    /** Returns the cleaned lines, or throws [IllegalArgumentException] with a reason the user can act on. */
    fun normalize(lines: Sequence<String>): List<String> {
        val out = ArrayList<String>()
        var size: Int? = null
        var rows = 0
        for (raw in lines) {
            val line = raw.trim()
            if (line.isEmpty() || line.startsWith("#")) continue
            val head = line.substringBefore(' ').substringBefore('\t')
            when {
                head == "LUT_1D_SIZE" -> throw IllegalArgumentException("1D LUTs are not supported")
                head == "LUT_3D_SIZE" -> {
                    val n = line.substringAfter(head).trim().toIntOrNull()
                    require(n != null && n in MIN_SIZE..MAX_SIZE) {
                        "LUT size must be between $MIN_SIZE and $MAX_SIZE points per axis"
                    }
                    size = n
                    out.add("LUT_3D_SIZE $n")
                }
                head == "DOMAIN_MIN" || head == "DOMAIN_MAX" -> out.add(line)
                line[0].isDigit() || line[0] == '-' || line[0] == '+' || line[0] == '.' -> {
                    require(line.split(Regex("\\s+")).size >= 3) { "A LUT row needs three numbers" }
                    require(size != null) { "LUT rows come before LUT_3D_SIZE" }
                    rows++
                    out.add(line)
                }
                // Any other letter-led line is metadata the renderer would reject; drop it.
                else -> Unit
            }
        }
        val n = requireNotNull(size) { "Missing LUT_3D_SIZE" }
        require(rows == n * n * n) { "LUT table has $rows rows, expected ${n * n * n} for size $n" }
        return out
    }
}
