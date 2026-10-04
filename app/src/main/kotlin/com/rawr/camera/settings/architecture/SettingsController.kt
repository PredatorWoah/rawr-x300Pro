package com.rawr.camera.settings.architecture

import com.rawr.camera.settings.model.SettingsUiState
import kotlinx.coroutines.flow.StateFlow

fun interface SettingsDispatch {
    fun invoke(action: SettingsAction)
}

interface SettingsController {
    val state: StateFlow<SettingsUiState>

    fun dispatch(action: SettingsAction)
}
