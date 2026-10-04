package com.rawr.camera.architecture

import com.rawr.camera.settings.model.FilmSimDiscreteField
import com.rawr.camera.settings.model.FilmSimFlag
import com.rawr.camera.settings.model.FilmSimNumericParameter

/** Typed commands for the film strip; section/navigation keys remain presentation-only. */
sealed interface CaptureFilmEvent {
    data class SelectPreset(val id: String) : CaptureFilmEvent
    data class ScrubNumeric(val parameter: FilmSimNumericParameter, val value: Float) : CaptureFilmEvent
    data class ResetNumeric(val parameter: FilmSimNumericParameter) : CaptureFilmEvent
    data class ScrubDiscrete(val field: FilmSimDiscreteField, val index: Int) : CaptureFilmEvent
    data class SetFlag(val flag: FilmSimFlag, val enabled: Boolean) : CaptureFilmEvent
}
