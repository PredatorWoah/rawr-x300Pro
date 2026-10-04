package com.rawr.camera.settings.preferences

import com.rawr.camera.settings.model.ImportedLutProfile
import com.rawr.camera.settings.model.ImportedLutStage
import com.rawr.camera.settings.model.ProfileTone
import kotlin.test.*

class LutProfileChangesTest {
    private val shared = ImportedLutStage("shared", "Shared.cube", "lut_profiles/files/shared.cube")
    private val own = ImportedLutStage("own", "Own.cube", "lut_profiles/files/own.cube")
    private val first = ImportedLutProfile("first", "First", listOf(shared, own))
    private val second = ImportedLutProfile("second", "Second", listOf(shared))

    @Test fun initializationAndAdditionsNeverCollectUnownedAssets() {
        assertEquals(RemovedLutAssets(emptySet(), emptySet()), removedLutAssets(emptyList(), listOf(first)))
        assertEquals(RemovedLutAssets(emptySet(), emptySet()), removedLutAssets(listOf(first), listOf(first, second)))
    }

    @Test fun metadataEditsDoNotRemoveAssets() {
        val edited = first.copy(tone = ProfileTone.Neutral.copy(contrast = 10f))
        assertEquals(RemovedLutAssets(emptySet(), emptySet()), removedLutAssets(listOf(first), listOf(edited)))
    }

    @Test fun deletionRetainsStagesStillReferencedByAnotherProfile() {
        assertEquals(RemovedLutAssets(setOf(first.id), setOf(own.relativePath)),
            removedLutAssets(listOf(first, second), listOf(second)))
    }

    @Test fun removingAStageKeepsItsProfileMetadata() {
        assertEquals(RemovedLutAssets(emptySet(), setOf(own.relativePath)),
            removedLutAssets(listOf(first), listOf(first.copy(stages = listOf(shared)))))
    }
}
