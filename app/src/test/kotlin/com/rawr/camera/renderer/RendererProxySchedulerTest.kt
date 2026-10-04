package com.rawr.camera.renderer

import kotlin.test.*
import kotlinx.coroutines.CompletableDeferred
import kotlinx.coroutines.ExperimentalCoroutinesApi
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.test.*

@OptIn(ExperimentalCoroutinesApi::class)
class RendererProxySchedulerTest {
    private fun frame(draft: String) = RenderJob("job", "Job", draft = draft, width = 100, height = 100)

    @Test fun rapidDraftChangesConflateWithoutCancellingOrOverlappingTheActiveRender() = runTest {
        val jobs = MutableStateFlow(frame("A"))
        val release = CompletableDeferred<Unit>()
        val rendered = mutableListOf<String>()
        var active = 0
        var maxActive = 0
        RendererProxyScheduler(backgroundScope, jobs, { true }, { false }, { 0L }, {
            active++
            maxActive = maxOf(maxActive, active)
            rendered += it.draft
            if (it.draft == "A") release.await()
            active--
        }, { throw it })
        runCurrent()
        jobs.value = frame("B")
        jobs.value = frame("C")
        runCurrent()
        assertEquals(listOf("A"), rendered)
        release.complete(Unit)
        runCurrent()
        assertEquals(listOf("A", "C"), rendered)
        assertEquals(1, maxActive)
    }

    @Test fun exportsPauseProxyRequestsAndResumeWithTheNewestDraft() = runTest {
        val jobs = MutableStateFlow(frame("A"))
        val rendered = mutableListOf<String>()
        var exporting = true
        RendererProxyScheduler(backgroundScope, jobs, { true }, { exporting }, { 0L }, { rendered += it.draft }, { throw it })
        advanceTimeBy(200)
        runCurrent()
        assertTrue(rendered.isEmpty())
        jobs.value = frame("B")
        exporting = false
        advanceTimeBy(100)
        runCurrent()
        assertEquals(listOf("B"), rendered)
    }

    @Test fun recreatedSurfaceRendersTheSameDraftAgain() = runTest {
        val jobs = MutableStateFlow(frame("A"))
        val rendered = mutableListOf<String>()
        var generation = 0L
        RendererProxyScheduler(backgroundScope, jobs, { true }, { false }, { generation }, { rendered += it.draft }, { throw it })
        runCurrent()
        generation++
        advanceTimeBy(30)
        runCurrent()
        assertEquals(listOf("A", "A"), rendered)
    }

    @Test fun resolutionChangesInvalidateTheProxyWithoutAToneEdit() = runTest {
        val jobs = MutableStateFlow(frame("A"))
        val rendered = mutableListOf<Double>()
        RendererProxyScheduler(backgroundScope, jobs, { true }, { false }, { 0L }, { rendered += it.megapixels }, { throw it })
        runCurrent()
        jobs.value = jobs.value.copy(megapixels = 12.0)
        advanceTimeBy(30)
        runCurrent()
        assertEquals(listOf(0.0, 12.0), rendered)
    }

    @Test fun failingDraftWaitsForAnotherIntentInsteadOfRetryingContinuously() = runTest {
        val jobs = MutableStateFlow(frame("bad"))
        val attempts = mutableListOf<String>()
        val errors = mutableListOf<Exception>()
        RendererProxyScheduler(backgroundScope, jobs, { true }, { false }, { 0L }, { attempts += it.draft; error("failed") }, errors::add)
        advanceTimeBy(500)
        runCurrent()
        assertEquals(listOf("bad"), attempts)
        jobs.value = frame("new")
        advanceTimeBy(30)
        runCurrent()
        assertEquals(listOf("bad", "new"), attempts)
        assertEquals(2, errors.size)
    }
}
