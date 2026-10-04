package com.rawr.camera.model

/** Normalized geometry shared by the instrument card and native scope renderer. */
data class ScopePresentation(
    val type: ScopeType,
    val mode: WaveformMode,
    val quarterTurns: Int,
    val x: Float,
    val y: Float,
    val width: Float,
    val height: Float,
    val cornerFraction: Float
)
