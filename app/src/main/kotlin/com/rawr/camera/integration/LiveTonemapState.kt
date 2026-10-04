package com.rawr.camera.integration

import com.rawr.camera.model.ImageToneState
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asStateFlow

/**
 * Process-local live rendering state.
 *
 * Persistence and realtime application are deliberately separate: Settings publishes here
 * synchronously, while DataStore writes happen asynchronously.  Camera/native code consumes
 * this state but does not own Settings persistence.
 */
object LiveTonemapState {
    private val mutable = MutableStateFlow<ImageToneState?>(null)
    val values: StateFlow<ImageToneState?> = mutable.asStateFlow()

    fun publish(value: ImageToneState) {
        mutable.value = value
    }
}
