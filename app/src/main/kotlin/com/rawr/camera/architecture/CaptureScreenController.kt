package com.rawr.camera.architecture

import com.rawr.camera.model.CaptureUiState
import kotlinx.coroutines.flow.StateFlow

typealias CaptureDispatch = (CaptureAction) -> Unit

/** UI/application-layer owner of capture-screen state. A future camera backend is injected behind this boundary. */
interface CaptureScreenController : AutoCloseable {
    val state: StateFlow<CaptureUiState>

    fun dispatch(action: CaptureAction)

    override fun close() = Unit
}

/** Marker for every user/UI intent accepted by [CaptureScreenController]. */
sealed interface CaptureAction

/** Pure capture-screen presentation state. These are the only inputs accepted by [CaptureReducer]. */
sealed interface PresentationCaptureAction : CaptureAction

/**
 * Application commands that may require camera/processing/backend work.
 *
 * They are intentionally not reducer actions even when an in-memory implementation can complete
 * them synchronously. The production controller/backend decides request/applied timing.
 */
sealed interface ApplicationCaptureAction : CaptureAction
