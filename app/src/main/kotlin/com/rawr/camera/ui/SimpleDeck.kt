package com.rawr.camera.ui

import androidx.compose.foundation.background
import androidx.compose.foundation.gestures.detectTapGestures
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.fillMaxHeight
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.saveable.rememberSaveable
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.input.pointer.pointerInput
import androidx.compose.ui.unit.dp
import com.rawr.camera.architecture.CaptureDispatch
import com.rawr.camera.architecture.CaptureFilmEvent
import com.rawr.camera.model.CaptureUiState
import com.rawr.camera.model.FilmSimQuickState
import com.rawr.camera.model.RenderProfileQuickState

/**
 * The Simple layout's bottom deck: every control the Compact row has (mode, white balance, shutter, ISO, EV, focus),
 * on one rounded translucent surface so it reads on any scene, plus a LOOK chip. The look (profile, film, params) is
 * hidden until LOOK is tapped and then opens straight to its picker, so the viewfinder stays clear while shooting.
 */
@Composable
internal fun SimpleViewfinderDeck(
    state: CaptureUiState,
    dispatch: CaptureDispatch,
    renderProfiles: RenderProfileQuickState,
    onSelectRenderProfile: (com.rawr.camera.model.RenderProfileSelection) -> Unit,
    filmQuick: FilmSimQuickState?,
    onFilmEvent: (CaptureFilmEvent) -> Unit,
    modifier: Modifier = Modifier
) {
    var lookOpen by rememberSaveable { mutableStateOf(false) }
    val lookValue =
        if (state.filmSimEnabled) {
            "FILM"
        } else {
            renderProfiles.options.firstOrNull { it.id == renderProfiles.selectedId }?.label ?: "LOOK"
        }
    Column(
        modifier.pointerInput(Unit) {
            // Plain taps must not fall through to tap-to-focus; the controls handle their own gestures.
            detectTapGestures(onTap = {})
        },
        horizontalAlignment = Alignment.CenterHorizontally
    ) {
        if (lookOpen) {
            LookStrip(
                state = state,
                dispatch = dispatch,
                forceTonemapStrip = false,
                renderProfiles = renderProfiles,
                onSelectRenderProfile = onSelectRenderProfile,
                filmQuick = filmQuick,
                onFilmEvent = onFilmEvent,
                startExpanded = true
            )
        }
        val haptics = LocalCaptureHaptics.current
        CompactParamRow(
            state = state,
            dispatch = dispatch,
            modifier = Modifier
                .padding(horizontal = 10.dp, vertical = 4.dp)
                .clip(RoundedCornerShape(22.dp))
                .background(Color.Black.copy(alpha = .46f)),
            trailing = {
                CompactTextChip(
                    value = lookValue,
                    title = "LOOK",
                    engaged = lookOpen,
                    testTag = CaptureTestTags.COMPACT_LOOK,
                    description = "Look $lookValue. Tap to ${if (lookOpen) "close" else "open"} profiles and film.",
                    onClick = {
                        haptics.selection()
                        lookOpen = !lookOpen
                    },
                    modifier = Modifier.weight(1f).fillMaxHeight()
                )
            }
        )
    }
}
