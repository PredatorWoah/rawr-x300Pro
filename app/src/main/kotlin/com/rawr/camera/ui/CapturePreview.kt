package com.rawr.camera.ui

import androidx.compose.runtime.Composable
import androidx.compose.ui.Modifier
import com.rawr.camera.model.ScopePresentation

/** UI-facing preview slots and measurements. Native integration belongs to CaptureRoute. */
data class CapturePreview(
    val nativeContent: Boolean = false,
    val deviceRotationDegrees: Int = 0,
    val content: @Composable (Modifier) -> Unit = { ViewfinderFixture(it) },
    val faceOverlay: @Composable () -> Unit = {},
    val onScopePresentation: (List<ScopePresentation>) -> Unit = {}
)
