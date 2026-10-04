package com.rawr.camera.ui

import android.view.SurfaceHolder
import android.view.SurfaceView
import androidx.compose.runtime.Composable
import androidx.compose.runtime.DisposableEffect
import androidx.compose.runtime.remember
import androidx.compose.ui.Modifier
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.viewinterop.AndroidView
import com.rawr.camera.integration.RawPreviewCoordinator

@Composable
internal fun RawPreviewSurface(coordinator: RawPreviewCoordinator, modifier: Modifier = Modifier) {
    val context = LocalContext.current
    val view = remember { SurfaceView(context).apply { setZOrderOnTop(false) } }
    DisposableEffect(view, coordinator) {
        val callback =
            object : SurfaceHolder.Callback {
                override fun surfaceCreated(holder: SurfaceHolder) {
                    coordinator.onSurfaceAvailable(
                        holder.surface,
                        (
                            view.display?.rotation
                                ?: android.view.Surface.ROTATION_0
                            ).toDegrees()
                    )
                }

                override fun surfaceChanged(holder: SurfaceHolder, format: Int, width: Int, height: Int) {
                    coordinator.onSurfaceAvailable(
                        holder.surface,
                        (
                            view.display?.rotation
                                ?: android.view.Surface.ROTATION_0
                            ).toDegrees(),
                        width,
                        height
                    )
                }

                override fun surfaceDestroyed(holder: SurfaceHolder) = coordinator.onSurfaceDestroyed()
            }
        view.holder.addCallback(callback)
        onDispose {
            view.holder.removeCallback(callback)
            coordinator.onSurfaceDestroyed()
        }
    }
    AndroidView(factory = { view }, modifier = modifier)
}

private fun Int.toDegrees(): Int = when (this) {
    android.view.Surface.ROTATION_0 -> 0
    android.view.Surface.ROTATION_90 -> 90
    android.view.Surface.ROTATION_180 -> 180
    android.view.Surface.ROTATION_270 -> 270
    else -> 0
}
