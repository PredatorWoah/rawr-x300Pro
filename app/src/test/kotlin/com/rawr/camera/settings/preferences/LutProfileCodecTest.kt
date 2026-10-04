package com.rawr.camera.settings.preferences

import com.rawr.camera.settings.model.AfterLutAction
import com.rawr.camera.settings.model.ImportedLutProfile
import com.rawr.camera.settings.model.ImportedLutStage
import com.rawr.camera.settings.model.LutGamut
import com.rawr.camera.settings.model.LutTransfer
import com.rawr.camera.settings.model.ProfileTone
import kotlin.test.Test
import kotlin.test.assertEquals

class LutProfileCodecTest {
    @Test
    fun fLog2CProfileKeepsStableNativeIdsAndRoundTrips() {
        assertEquals(7, LutGamut.FujifilmFGamutC.nativeId)
        assertEquals(9, LutTransfer.FLog2C.nativeId)

        val profile =
            ImportedLutProfile(
                id = "fujifilm-profile",
                name = "F-Log2 C Film Simulation",
                stages =
                    listOf(
                        ImportedLutStage(
                            id = "stage-1",
                            fileName = "film-simulation.cube",
                            relativePath = "lut_profiles/files/stage-1.cube"
                        )
                    ),
                inputGamut = LutGamut.FujifilmFGamutC,
                inputTransfer = LutTransfer.FLog2C,
                outputGamut = LutGamut.FujifilmFGamutC,
                outputTransfer = LutTransfer.FLog2C,
                afterLut = AfterLutAction.ConvertToJpegSrgb
            )

        assertEquals(listOf(profile), LutProfileCodec.decode(LutProfileCodec.encode(listOf(profile))))
    }

    @Test
    fun perProfileToneRoundTrips() {
        val profile =
            ImportedLutProfile(
                id = "tone-profile",
                name = "Tone",
                stages =
                    listOf(
                        ImportedLutStage(
                            id = "stage-1",
                            fileName = "look.cube",
                            relativePath = "lut_profiles/files/stage-1.cube"
                        )
                    ),
                tone = ProfileTone(shadows = 12f, highlights = -8f, saturation = 20f, vibrance = -5f)
            )

        assertEquals(listOf(profile), LutProfileCodec.decode(LutProfileCodec.encode(listOf(profile))))
    }

    @Test
    fun legacyPayloadWithoutToneDecodesToNeutral() {
        val profile =
            ImportedLutProfile(
                id = "legacy",
                name = "Legacy",
                stages =
                    listOf(
                        ImportedLutStage(
                            id = "stage-1",
                            fileName = "look.cube",
                            relativePath = "lut_profiles/files/stage-1.cube"
                        )
                    )
            )
        // Strip the 9th pipe field to simulate a pre-per-profile-tone payload.
        val legacy = LutProfileCodec.encode(listOf(profile)).substringBeforeLast('|')
        assertEquals(listOf(profile), LutProfileCodec.decode(legacy))
        assertEquals(ProfileTone.Neutral, LutProfileCodec.decode(legacy).single().tone)
    }
}
