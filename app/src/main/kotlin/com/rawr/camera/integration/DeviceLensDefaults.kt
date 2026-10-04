package com.rawr.camera.integration

import android.os.Build
import com.rawr.camera.settings.model.LensProfile
import com.rawr.camera.settings.preferences.LensProfileCodec

/** This device's built-in lenses (native camera profile for Build.MODEL); the Lens settings start from these. */
object DeviceLensDefaults {
    val lenses: List<LensProfile> by lazy {
        LensProfileCodec.decode(NativePreviewEngine().builtInCameraProfile(Build.MODEL)).orEmpty()
    }
}
