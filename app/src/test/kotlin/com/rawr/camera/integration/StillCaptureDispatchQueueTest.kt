package com.rawr.camera.integration

import com.rawr.camera.model.CaptureOutputFormat
import java.util.concurrent.CountDownLatch
import java.util.concurrent.TimeUnit
import kotlin.test.Test
import kotlin.test.assertEquals
import kotlin.test.assertTrue
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.SupervisorJob
import kotlinx.coroutines.cancel
import kotlinx.coroutines.launch
import kotlinx.coroutines.test.runCurrent
import kotlinx.coroutines.test.runTest
import kotlinx.coroutines.ExperimentalCoroutinesApi

@OptIn(ExperimentalCoroutinesApi::class)
class StillCaptureDispatchQueueTest {
    @Test
    fun eightPressesRetainOrderAndTheirOutputFormat() = runTest {
        var admitted = 0
        val acquired = mutableListOf<CaptureOutputFormat>()
        val queue = StillCaptureDispatchQueue(backgroundScope, { admitted++ }, acquired::add)
        val presses = List(8) { CaptureOutputFormat.entries[it % 3] }
        presses.forEach(queue::enqueue)
        assertEquals(8, admitted)
        assertTrue(acquired.isEmpty(), "Shutter caller must not execute provider/native work")
        runCurrent()
        assertEquals(presses, acquired)
        var published = 0
        queue.poll { published = acquired.size }
        assertEquals(8, published)
    }

    @Test
    fun slowPreparationDoesNotBlockFurtherPressesOrLetCompletionOvertakeRegistration() {
        val scope = CoroutineScope(SupervisorJob() + Dispatchers.Default)
        val entered = CountDownLatch(1)
        val finishPreparation = CountDownLatch(1)
        val captured = CountDownLatch(8)
        val pollFinished = CountDownLatch(1)
        try {
            val queue = StillCaptureDispatchQueue(scope, {}, {
                entered.countDown()
                check(finishPreparation.await(5, TimeUnit.SECONDS))
                captured.countDown()
            })
            queue.enqueue(CaptureOutputFormat.DngAndJpeg)
            assertTrue(entered.await(5, TimeUnit.SECONDS))
            repeat(7) { queue.enqueue(CaptureOutputFormat.DngAndJpeg) }
            scope.launch { queue.poll { pollFinished.countDown() } }
            assertTrue(!pollFinished.await(100, TimeUnit.MILLISECONDS))
            finishPreparation.countDown()
            assertTrue(captured.await(5, TimeUnit.SECONDS))
            assertTrue(pollFinished.await(5, TimeUnit.SECONDS))
        } finally {
            finishPreparation.countDown()
            scope.cancel()
        }
    }
}
