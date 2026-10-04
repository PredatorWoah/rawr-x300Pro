package com.rawr.camera.settings.preferences

import com.rawr.camera.model.ImageToneState
import kotlin.test.Test
import kotlin.test.assertEquals

class SettingsMigrationsTest {
    private val neutral =
        ImageToneState(
            outputColorSpaceId = "output.srgb",
            transferFunctionId = "transfer.srgb"
        )

    @Test
    fun freshInstallDoesNotMigrateAbsentColorControlsToMonochrome() {
        val migrated =
            migrateV11ColorControls(
                tone = neutral,
                defaults = neutral,
                hasPersistedSaturation = false,
                hasPersistedVibrance = false
            )

        assertEquals(0f, migrated.saturation)
        assertEquals(0f, migrated.vibrance)
    }

    @Test
    fun persistedV11ColorControlsStillUseLegacyConversion() {
        val legacy = neutral.copy(saturation = 0.5f, vibrance = 0.5f)
        val migrated =
            migrateV11ColorControls(
                tone = legacy,
                defaults = neutral,
                hasPersistedSaturation = true,
                hasPersistedVibrance = true
            )

        assertEquals(-50f, migrated.saturation)
        assertEquals(50f, migrated.vibrance)
    }
}
