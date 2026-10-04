package com.rawr.camera.storage

import java.io.FileNotFoundException
import org.junit.Assert.*
import org.junit.Test

/**
 * Failure classification for still-capture recovery: only provably permanent
 * output loss may quarantine an entry. Everything else keeps today's
 * retry behavior.
 */
class StillOutputStoreRecoveryTest {

    @Test fun directMissingFileIsPermanent() {
        assertTrue(
            isOutputGonePermanently(
                FileNotFoundException("Missing file for primary:DCIM/Rawr/RAWR_1.dng")
            )
        )
    }

    @Test fun providerWrappedMissingFileIsPermanent() {
        // Observed MediaStore shape on device: IllegalArgumentException
        // ("... is child of ...") caused by FileNotFoundException
        // ("Missing file ...").
        val wrapped =
            IllegalArgumentException(
                "Failed to determine if primary:DCIM/Rawr/RAWR_1.dng is child of primary:DCIM/Rawr",
                FileNotFoundException("Missing file for primary:DCIM/Rawr/RAWR_1.dng")
            )
        assertTrue(isOutputGonePermanently(wrapped))
    }

    @Test fun deeplyWrappedMissingFileIsPermanent() {
        val root = FileNotFoundException("gone")
        val wrapped = RuntimeException("open failed", IllegalStateException("wrap", root))
        assertTrue(isOutputGonePermanently(wrapped))
    }

    @Test fun providerFlattenedMissingFileMessageIsPermanent() {
        // Some providers flatten the signal into the message text with no
        // typed cause to walk (observed via DatabaseUtils parcel decoding).
        val flattened =
            IllegalArgumentException(
                "Failed to determine if primary:DCIM/Rawr/RAWR_1.dng is child of " +
                    "primary:DCIM/Rawr: java.io.FileNotFoundException: Missing file " +
                    "for primary:DCIM/Rawr/RAWR_1.dng"
            )
        assertTrue(isOutputGonePermanently(flattened))
    }

    @Test fun transientIoFailureIsNotPermanent() {
        assertFalse(isOutputGonePermanently(java.io.IOException("I/O error")))
    }

    @Test fun securityFailureIsNotPermanent() {
        // A revoked grant can be re-granted; never quarantine on a guess.
        assertFalse(isOutputGonePermanently(SecurityException("Permission Denial")))
    }

    @Test fun unrelatedIllegalArgumentIsNotPermanent() {
        assertFalse(isOutputGonePermanently(IllegalArgumentException("bad mode")))
    }

    @Test fun nullFdWithoutCauseIsNotPermanent() {
        assertFalse(isOutputGonePermanently(IllegalStateException("Required value was null")))
    }
}
