package com.rawr.camera.integration

import android.os.Build

/** Human readable device names for EXIF and DNG, instead of the bare model code. */
internal object DeviceNames {
    private val marketingNames =
        mapOf(
            "V2514" to "vivo X300 Pro",
            "V2562" to "vivo X300 Ultra",
            "V2547A" to "vivo X300 Ultra",
            "V2547DA" to "vivo X300 Ultra"
        )

    fun marketingModel(model: String): String = marketingNames[model] ?: model

    fun marketingModel(): String = marketingModel(Build.MODEL.orEmpty())
}
