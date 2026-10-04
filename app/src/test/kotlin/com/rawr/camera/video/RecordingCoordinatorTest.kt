package com.rawr.camera.video

import android.net.Uri
import com.rawr.camera.architecture.*
import com.rawr.camera.fixtures.CaptureFixtures
import com.rawr.camera.model.CaptureUiState
import com.rawr.camera.settings.model.SelfTimer
import kotlin.test.*
import kotlinx.coroutines.CompletableDeferred
import kotlinx.coroutines.ExperimentalCoroutinesApi
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.asStateFlow
import kotlinx.coroutines.test.*
import org.json.JSONObject

@OptIn(ExperimentalCoroutinesApi::class)
class RecordingCoordinatorTest {
    private class Controller(timer: SelfTimer = SelfTimer.Off) : CaptureScreenController {
        private val mutable = MutableStateFlow(CaptureUiState(CaptureFixtures.baseline(), selfTimer = timer))
        override val state = mutable.asStateFlow()
        override fun dispatch(action: CaptureAction) {
            if (action is PresentationCaptureAction) mutable.value = CaptureReducer.reduce(mutable.value, action)
        }
    }
    private class Recorder(private val failure: Boolean = false) : VideoRecording {
        private var active = true
        var closes = 0
        override val isRecording: Boolean get() = active
        override val outputUri: Uri? = null
        override fun stats(): JSONObject = error("No statistics tick expected in this test")
        override fun journalSnapshot() = Unit
        override fun close() {
            closes++
            active = false
            if (failure) error("finalize failed")
        }
    }
    private fun TestScope.coordinator(
        controller: CaptureScreenController = Controller(),
        start: suspend () -> VideoRecording
    ) = RecordingCoordinator(
        controller, start, { testScheduler.currentTime * 1_000_000 },
        dispatcher = StandardTestDispatcher(testScheduler), ioDispatcher = StandardTestDispatcher(testScheduler)
    )
    private fun RecordingCoordinator.release() { closeWhenIdle {} }

    @Test fun backgroundStopWaitsForStartingRecorderAndFinalizesExactlyOnce() = runTest {
        val startGate = CompletableDeferred<Unit>()
        val recorder = Recorder()
        val owner = coordinator { startGate.await(); recorder }
        owner.start()
        runCurrent()
        assertEquals(RecordingPhase.Starting, owner.state.value.phase)
        val stopping = owner.stop()
        runCurrent()
        assertFalse(stopping.isCompleted)
        startGate.complete(Unit)
        runCurrent()
        assertTrue(stopping.isCompleted)
        assertEquals(RecordingPhase.Idle, owner.state.value.phase)
        assertEquals(1, recorder.closes)
        owner.stop()
        runCurrent()
        assertEquals(1, recorder.closes)
        owner.release()
        runCurrent()
    }

    @Test fun countdownCanBeCancelledWithoutStarting() = runTest {
        val controller = Controller(SelfTimer.TwoSeconds)
        var starts = 0
        val owner = coordinator(controller) { starts++; Recorder() }
        owner.toggle()
        advanceTimeBy(500)
        runCurrent()
        owner.toggle()
        advanceTimeBy(3_000)
        runCurrent()
        assertEquals(0, starts)
        assertNull(controller.state.value.selfTimerRemainingMs)
        owner.release()
        runCurrent()
    }

    @Test fun staleCountdownCannotStartAfterModeChange() = runTest {
        val controller = Controller(SelfTimer.TwoSeconds)
        var starts = 0
        val owner = coordinator(controller) { starts++; Recorder() }
        owner.toggle()
        advanceTimeBy(500)
        controller.dispatch(CancelSelfTimer)
        advanceTimeBy(2_000)
        runCurrent()
        assertEquals(0, starts)
        owner.release()
        runCurrent()
    }

    @Test fun countdownStartsOnceAtExpiry() = runTest {
        val controller = Controller(SelfTimer.TwoSeconds)
        var starts = 0
        val owner = coordinator(controller) { starts++; Recorder() }
        owner.toggle()
        advanceTimeBy(2_000)
        runCurrent()
        assertEquals(1, starts)
        assertEquals(RecordingPhase.Recording, owner.state.value.phase)
        assertNull(controller.state.value.selfTimerRemainingMs)
        owner.release()
        runCurrent()
    }

    @Test fun startFailureClearsBusyAndAllowsRetry() = runTest {
        var starts = 0
        val owner = coordinator { starts++; if (starts == 1) error("start failed") else Recorder() }
        owner.start()
        runCurrent()
        assertEquals(RecordingPhase.Failed, owner.state.value.phase)
        assertFalse(owner.state.value.busy)
        owner.start()
        runCurrent()
        assertEquals(RecordingPhase.Recording, owner.state.value.phase)
        owner.release()
        runCurrent()
    }

    @Test fun closingRetainsFinalizationAndReleasesDependenciesAfterward() = runTest {
        val startGate = CompletableDeferred<Unit>()
        val recorder = Recorder(failure = true)
        val owner = coordinator { startGate.await(); recorder }
        var released = false
        owner.start()
        runCurrent()
        owner.closeWhenIdle { released = true }
        runCurrent()
        assertFalse(released)
        startGate.complete(Unit)
        runCurrent()
        assertEquals(1, recorder.closes)
        assertTrue(released)
        assertEquals(RecordingPhase.Failed, owner.state.value.phase)
        assertFalse(owner.state.value.recording)
    }
}
