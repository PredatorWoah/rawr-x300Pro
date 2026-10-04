package com.rawr.camera.renderer

import com.rawr.camera.storage.StillOutputStore
import org.junit.Test
import org.junit.Assert.*
import java.nio.file.Files
import java.io.File
import java.util.concurrent.Executors
import java.util.concurrent.CountDownLatch

class RendererOwnershipTest {
    @Test fun oneOwnerSurvivesConcurrentClaims() {
        val root = Files.createTempDirectory("renderer-claim").toFile()
        File(root, "still_jobs").mkdirs()
        val executor = Executors.newFixedThreadPool(4)
        val start = CountDownLatch(1)
        try {
            val attempts = List(12) { executor.submit<Boolean> { start.await(); StillOutputStore.claimForRenderer(root, "test.dng") } }
            start.countDown()
            assertEquals(1, attempts.count { it.get() })
            assertTrue(StillOutputStore.isClaimed("test.dng"))
            assertTrue(File(root, "still_jobs/test.dng.renderer-owner").isFile)
            assertFalse(StillOutputStore.hasActiveCaptures(root))
            StillOutputStore.rendererRelease(root, "test.dng")
            assertFalse(StillOutputStore.isClaimed("test.dng"))
            assertFalse(File(root, "still_jobs/test.dng.renderer-owner").exists())
        } finally { executor.shutdownNow(); StillOutputStore.rendererRelease(root, "test.dng"); root.deleteRecursively() }
    }
    @Test fun failedReservationDoesNotLeaveAnOwner() {
        val root = Files.createTempDirectory("renderer-claim-failure").toFile()
        try {
            runCatching { StillOutputStore.claimForRenderer(root, "failed.dng") }
            assertFalse(StillOutputStore.isClaimed("failed.dng"))
        } finally { root.deleteRecursively() }
    }
}
