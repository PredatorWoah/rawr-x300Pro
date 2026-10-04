package com.rawr.camera.model

/**
 * White-balance request modes. [awbValue] mirrors ACAMERA_CONTROL_AWB_MODE
 * (1..8); [ManualTempTint] is a Rawr-side sentinel (9) for AWB OFF +
 * TRANSFORM_MATRIX with in-app Kelvin/tint-derived gains.
 */
enum class WhiteBalanceMode(val awbValue: Int, val label: String, val shortLabel: String) {
    Auto(1, "Auto", "AUTO"),
    Incandescent(2, "Tungsten", "TUNG"),
    Fluorescent(3, "Fluorescent", "FLUOR"),
    WarmFluorescent(4, "Warm fluorescent", "WARM"),
    Daylight(5, "Daylight", "DAY"),
    CloudyDaylight(6, "Cloudy", "CLOUDY"),
    Twilight(7, "Twilight", "TWILIGHT"),
    Shade(8, "Shade", "SHADE"),
    ManualTempTint(9, "Manual", "MANUAL");

    companion object {
        const val TEMP_MIN_K = 2000
        const val TEMP_MAX_K = 10000
        const val TEMP_DEFAULT_K = 5200
        const val TINT_MIN = -50
        const val TINT_MAX = 50

        fun fromAwbValue(value: Int): WhiteBalanceMode = entries.firstOrNull { it.awbValue == value } ?: Auto
    }
}
