package com.rawr.camera.renderer

import kotlinx.coroutines.CancellationException
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.delay
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.isActive
import kotlinx.coroutines.launch

internal data class RendererPreviewKey(val draft: String, val megapixels: Double)
internal fun RenderJob.previewKey() = RendererPreviewKey(draft, megapixels)

/** Serial proxy renders consume the latest draft after each render, without restarting on slider ticks. */
internal class RendererProxyScheduler(
    scope: CoroutineScope,
    jobs: StateFlow<RenderJob>,
    ready: () -> Boolean,
    busy: () -> Boolean,
    generation: () -> Long,
    render: suspend (RenderJob) -> Unit,
    onFailure: (Exception) -> Unit
) {
    init {
        scope.launch {
            var rendered: Pair<Long, RendererPreviewKey>? = null
            while (isActive) {
                if (busy()) { delay(100); continue }
                if (!ready()) { rendered = null; delay(30); continue }
                val current = jobs.value
                val token = generation() to current.previewKey()
                if (token == rendered || current.width <= 0) { delay(30); continue }
                try { render(current) }
                catch (e: CancellationException) { throw e }
                catch (e: Exception) { onFailure(e) }
                // A persistent failure waits for a new draft or surface instead of hot-looping.
                rendered = token
            }
        }
    }
}
