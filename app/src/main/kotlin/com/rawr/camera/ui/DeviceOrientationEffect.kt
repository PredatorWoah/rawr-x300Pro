package com.rawr.camera.ui

import android.view.OrientationEventListener
import androidx.compose.runtime.Composable
import androidx.compose.runtime.DisposableEffect
import androidx.compose.runtime.rememberUpdatedState
import androidx.compose.ui.platform.LocalContext
import com.rawr.camera.model.Orientation

/**
 * Observes the physical device posture while the Activity itself remains portrait-locked.
 * This is intentionally separate from Android configuration orientation: the capture
 * anatomy never reflows when the phone is turned sideways.
 */
@Composable
internal fun DeviceOrientationEffect(
    onOrientationChanged: (Orientation) -> Unit,
    onDeviceRotationChanged: (Int) -> Unit = {}
) {
    val context = LocalContext.current
    val latestCallback = rememberUpdatedState(onOrientationChanged)
    val latestRotationCallback = rememberUpdatedState(onDeviceRotationChanged)

    DisposableEffect(context) {
        var last: Orientation? = null
        var lastRotation: Int? = null
        val listener =
            object : OrientationEventListener(context) {
                override fun onOrientationChanged(angle: Int) {
                    if (angle == ORIENTATION_UNKNOWN) return
                    val rotation = physicalAngleToDisplayRotation(angle)
                    if (rotation != lastRotation) {
                        lastRotation = rotation
                        latestRotationCallback.value(rotation)
                    }
                    val next = classifyPhysicalOrientation(angle, last ?: Orientation.Portrait)
                    if (next != last) {
                        last = next
                        latestCallback.value(next)
                    }
                }
            }
        if (listener.canDetectOrientation()) listener.enable()
        onDispose { listener.disable() }
    }
}
