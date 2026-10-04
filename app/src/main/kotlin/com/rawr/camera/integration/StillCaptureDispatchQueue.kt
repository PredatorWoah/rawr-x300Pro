package com.rawr.camera.integration

import com.rawr.camera.model.CaptureOutputFormat
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.channels.Channel
import kotlinx.coroutines.launch
import kotlinx.coroutines.sync.Mutex
import kotlinx.coroutines.sync.withLock

/** Serializes provider preparation and completion publication away from the shutter caller. */
internal class StillCaptureDispatchQueue(
    scope: CoroutineScope,
    private val onQueued: (CaptureOutputFormat) -> Unit,
    private val capture: (CaptureOutputFormat) -> Unit
) {
    private val requests = Channel<CaptureOutputFormat>(Channel.UNLIMITED)
    private val mutex = Mutex()

    init {
        scope.launch {
            for (format in requests) mutex.withLock { capture(format) }
        }
    }

    fun enqueue(format: CaptureOutputFormat) {
        // Admission runs while the originating activity is still foreground.
        onQueued(format)
        check(requests.trySend(format).isSuccess)
    }

    suspend fun poll(completion: () -> Unit) {
        // A fast native completion must not overtake output registration or UI enqueue.
        mutex.withLock { completion() }
    }
}
