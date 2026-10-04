package com.rawr.camera.settings.ui

import android.view.HapticFeedbackConstants
import android.view.View
import androidx.compose.runtime.Composable
import androidx.compose.runtime.remember
import androidx.compose.runtime.staticCompositionLocalOf
import androidx.compose.ui.platform.LocalView

/** Presentation-only haptics for Settings interactions. */
internal interface SettingsHaptics {
    fun detent()
}

private object NoOpSettingsHaptics : SettingsHaptics {
    override fun detent() = Unit
}

internal val LocalSettingsHaptics = staticCompositionLocalOf<SettingsHaptics> { NoOpSettingsHaptics }

private class AndroidSettingsHaptics(private val view: View) : SettingsHaptics {
    override fun detent() {
        view.performHapticFeedback(HapticFeedbackConstants.CLOCK_TICK)
    }
}

@Composable
internal fun rememberSettingsHaptics(): SettingsHaptics {
    val view = LocalView.current
    return remember(view) { AndroidSettingsHaptics(view) }
}
