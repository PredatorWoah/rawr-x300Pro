package com.rawr.camera.model

import com.rawr.camera.settings.model.ColorRenderProfile
import com.rawr.camera.settings.model.VideoLogProfile

sealed interface RenderProfileSelection {
    data class BuiltIn(val profile: ColorRenderProfile) : RenderProfileSelection
    data class Imported(val profileId: String) : RenderProfileSelection
    data class Log(val profile: VideoLogProfile) : RenderProfileSelection

    val key: String get() = when (this) {
        is BuiltIn -> when (profile) {
            ColorRenderProfile.RawrBase -> "base"
            else -> "builtin_${profile.name}"
        }
        is Imported -> profileId
        is Log -> "log_${profile.name}"
    }
}

data class RenderProfileQuickOption(val id: RenderProfileSelection, val label: String)

data class RenderProfileQuickState(
    val options: List<RenderProfileQuickOption> = listOf(
        RenderProfileQuickOption(RenderProfileSelection.BuiltIn(ColorRenderProfile.RawrBase), "RAWR NTRL")),
    val selectedId: RenderProfileSelection = RenderProfileSelection.BuiltIn(ColorRenderProfile.RawrBase),
    val log: Boolean = false,
    val locked: Boolean = false
)
