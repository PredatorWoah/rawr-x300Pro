package com.rawr.camera.ui

import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Typography
import androidx.compose.material3.darkColorScheme
import androidx.compose.runtime.Composable

@Composable
fun CaptureTheme(content: @Composable () -> Unit) {
    MaterialTheme(
        colorScheme =
            darkColorScheme(
                background = CaptureColors.Background,
                surface = CaptureColors.Surface,
                primary = CaptureColors.Accent,
                secondary = CaptureColors.AccentSoft,
                surfaceContainerLow =
                    androidx.compose.ui.graphics
                        .Color(0xFF111315),
                surfaceContainer =
                    androidx.compose.ui.graphics
                        .Color(0xFF151719),
                outlineVariant =
                    androidx.compose.ui.graphics
                        .Color(0xFF35383C),
                onBackground =
                    androidx.compose.ui.graphics
                        .Color(0xFFF4F4F4),
                onSurface =
                    androidx.compose.ui.graphics
                        .Color(0xFFF4F4F4)
            ),
        typography =
            Typography(
                labelSmall = MaterialTheme.typography.labelSmall.copy(fontFamily = CaptureMono),
                labelMedium = MaterialTheme.typography.labelMedium.copy(fontFamily = CaptureMono),
                bodySmall = MaterialTheme.typography.bodySmall.copy(fontFamily = CaptureMono)
            ),
        content = content
    )
}
