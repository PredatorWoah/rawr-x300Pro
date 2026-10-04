package com.rawr.camera.settings.preferences

import com.rawr.camera.settings.model.ImportedLutProfile

internal data class RemovedLutAssets(val profileIds: Set<String>, val stagePaths: Set<String>)

/** Only the settings library's explicitly removed assets are eligible for cleanup. */
internal fun removedLutAssets(
    previous: List<ImportedLutProfile>,
    current: List<ImportedLutProfile>
): RemovedLutAssets = RemovedLutAssets(
    profileIds = previous.map { it.id }.toSet() - current.map { it.id }.toSet(),
    stagePaths = previous.flatMap { it.stages }.map { it.relativePath }.toSet() -
        current.flatMap { it.stages }.map { it.relativePath }.toSet()
)
