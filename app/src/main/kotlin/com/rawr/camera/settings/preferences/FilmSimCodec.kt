package com.rawr.camera.settings.preferences

import com.rawr.camera.settings.model.*
import java.net.URLDecoder
import java.net.URLEncoder
import java.nio.charset.StandardCharsets

// FilmSimLook + FilmPreset persistence. Pure numbers: the whole look fits in
// one DataStore string (floats/ints/enums ordered to mirror toFloatArray /
// toIntArray), presets are newline-separated like LutProfileCodec.
internal object FilmSimCodec {
    private fun enc(s: String) = URLEncoder.encode(s, StandardCharsets.UTF_8.name())

    private fun dec(s: String) = URLDecoder.decode(s, StandardCharsets.UTF_8.name())

    fun encodeLook(look: FilmSimLook): String {
        val floats = look.toFloatArray().joinToString(",")
        val ints = look.toIntArray().joinToString(",")
        return "$floats|$ints"
    }

    fun decodeLook(raw: String?): FilmSimLook? {
        if (raw.isNullOrBlank()) return null
        return runCatching {
            val parts = raw.split('|', limit = 2)
            require(parts.size == 2)
            val floats = parts[0].split(',').map { it.toFloat() }
            val ints = parts[1].split(',').map { it.toInt() }
            // Forward/backward tolerant: v1 persisted 17 ints; process (17)
            // and scanNegativeInvert (18) appended later default to print.
            // DIR floats (44..58) appended later default to upstream (off).
            // Scanner floats (59..66) + flags (19..21) appended later default
            // to upstream (off). Halation floats (67..79) + flag (22)
            // appended later default to upstream (off). Camera-diffusion
            // floats (80..88) + flags (23..24) appended later default to
            // upstream (off). Print-diffusion floats (89..97) + flags
            // (25..26) appended later default to upstream (off).
            require((floats.size == 44 || floats.size == 59 || floats.size == 67 ||
                    floats.size == 80 || floats.size == 89 || floats.size == 98) &&
                    (ints.size == 17 || ints.size == 19 || ints.size == 22 ||
                    ints.size == 23 || ints.size == 25 || ints.size == 27))
            var fi = 0
            var ii = 0
            fun nf(): Float = floats[fi++]
            fun ni(): Int = ints[ii++]
            FilmSimLook(
                film = ni(), paper = ni(), inputColorSpace = ni(), outputColorSpace = ni(),
                rgbToRawMethod = ni(), filmPushPullMode = ni(),
                printerLightsGang = ni() != 0, printerLightCalibration = ni() != 0,
                grainEnabled = ni() != 0, grainModel = ni(), filmFormat = ni(),
                grainSublayersEnabled = ni() != 0, grainSubLayerCount = ni(),
                grainSeed = ni(), grainAnimate = ni() != 0,
                cameraUvFilterEnabled = ni() != 0, cameraIrFilterEnabled = ni() != 0,
                // Appended after v1 (which persisted 17 ints): default to print.
                process = if (ints.size >= 19) ni() else 0,
                scanNegativeInvert = if (ints.size >= 19) ni() != 0 else false,
                scannerEnabled = if (ints.size >= 22) ni() != 0 else false,
                scannerWhiteCorrection = if (ints.size >= 22) ni() != 0 else false,
                scannerBlackCorrection = if (ints.size >= 22) ni() != 0 else false,
                halationEnabled = if (ints.size >= 23) ni() != 0 else false,
                cameraDiffusionEnabled = if (ints.size >= 25) ni() != 0 else false,
                cameraDiffusionFamily = if (ints.size >= 25) ni() else 1,
                printDiffusionEnabled = if (ints.size >= 27) ni() != 0 else false,
                printDiffusionFamily = if (ints.size >= 27) ni() else 1,
                filmExposureEv = nf(), printExposureEv = nf(), filmPushPullStops = nf(),
                filmGamma = nf(), printPushPullStops = nf(), printGamma = nf(),
                printShadowShape = nf(), printHighlightShape = nf(),
                negativeBleachBypassAmount = nf(), negativeLeucoCyanCoupling = nf(),
                printBleachBypassAmount = nf(), preflashExposure = nf(),
                preflashMFilterShift = nf(), preflashYFilterShift = nf(),
                filterC = nf(), filterMShift = nf(), filterYShift = nf(),
                printerLightsR = nf(), printerLightsG = nf(), printerLightsB = nf(),
                enlargerScale = nf(), enlargerOffsetXPercent = nf(),
                enlargerOffsetYPercent = nf(), grainAmount = nf(), grainSaturation = nf(),
                grainParticleAreaUm2 = nf(), grainParticleScaleR = nf(),
                grainParticleScaleG = nf(), grainParticleScaleB = nf(),
                grainParticleScaleLayer0 = nf(), grainParticleScaleLayer1 = nf(),
                grainParticleScaleLayer2 = nf(), grainDensityMinR = nf(),
                grainDensityMinG = nf(), grainDensityMinB = nf(),
                grainUniformityR = nf(), grainUniformityG = nf(), grainUniformityB = nf(),
                grainFinalBlurUm = nf(), grainBlurDyeCloudsUm = nf(),
                grainMicroStructureScale = nf(), grainMicroStructureSigmaNm = nf(),
                cameraUvCutNm = nf(), cameraIrCutNm = nf(),
                // Appended after v2 (which persisted 44 floats): default to
                // upstream (DIR off).
                dirCouplersAmount = if (floats.size >= 59) nf().coerceIn(0f, 1f) else 0f,
                dirCouplersDiffusionUm = if (floats.size >= 59) nf() else 20f,
                dirCouplersDiffusionTailUm = if (floats.size >= 59) nf() else 200f,
                dirCouplersDiffusionTailWeight = if (floats.size >= 59) nf() else 0.06f,
                dirCouplersInhibitionSameLayer = if (floats.size >= 59) nf() else 1f,
                dirCouplersInhibitionInterlayer = if (floats.size >= 59) nf() else 1f,
                dirCouplersGammaSameLayerR = if (floats.size >= 59) nf() else 0.336f,
                dirCouplersGammaSameLayerG = if (floats.size >= 59) nf() else 0.319f,
                dirCouplersGammaSameLayerB = if (floats.size >= 59) nf() else 0.273f,
                dirCouplersGammaRToG = if (floats.size >= 59) nf() else 0.353f,
                dirCouplersGammaRToB = if (floats.size >= 59) nf() else 0.302f,
                dirCouplersGammaGToR = if (floats.size >= 59) nf() else 0.154f,
                dirCouplersGammaGToB = if (floats.size >= 59) nf() else 0.353f,
                dirCouplersGammaBToR = if (floats.size >= 59) nf() else 0.168f,
                dirCouplersGammaBToG = if (floats.size >= 59) nf() else 0.226f,
                scannerWhiteLevel = if (floats.size >= 67) nf() else 0.98f,
                scannerBlackLevel = if (floats.size >= 67) nf() else 0.01f,
                glarePercent = if (floats.size >= 67) nf() else 0.03f,
                glareRoughness = if (floats.size >= 67) nf() else 0.7f,
                glareBlur = if (floats.size >= 67) nf() else 0.5f,
                scannerMtf50LpMm = if (floats.size >= 67) nf() else 60f,
                scannerUnsharpRadiusUm = if (floats.size >= 67) nf() else 5f,
                scannerUnsharpAmount = if (floats.size >= 67) nf() else 0.7f,
                scatterAmount = if (floats.size >= 80) nf() else 1f,
                scatterScale = if (floats.size >= 80) nf() else 1f,
                halationAmount = if (floats.size >= 80) nf() else 1f,
                halationScale = if (floats.size >= 80) nf() else 1f,
                halationStrengthR = if (floats.size >= 80) nf() else 0.05f,
                halationStrengthG = if (floats.size >= 80) nf() else 0.015f,
                halationStrengthB = if (floats.size >= 80) nf() else 0f,
                halationFirstSigmaUmR = if (floats.size >= 80) nf() else 65f,
                halationFirstSigmaUmG = if (floats.size >= 80) nf() else 65f,
                halationFirstSigmaUmB = if (floats.size >= 80) nf() else 65f,
                halationBoostEv = if (floats.size >= 80) nf() else 0f,
                halationBoostRange = if (floats.size >= 80) nf() else 0.3f,
                halationProtectEv = if (floats.size >= 80) nf() else 4f,
                cameraDiffusionStrength = if (floats.size >= 89) nf() else 0.5f,
                cameraDiffusionSpatialScale = if (floats.size >= 89) nf() else 1f,
                cameraDiffusionHaloWarmth = if (floats.size >= 89) nf() else 0f,
                cameraDiffusionCoreIntensity = if (floats.size >= 89) nf() else 1f,
                cameraDiffusionCoreSize = if (floats.size >= 89) nf() else 1f,
                cameraDiffusionHaloIntensity = if (floats.size >= 89) nf() else 1f,
                cameraDiffusionHaloSize = if (floats.size >= 89) nf() else 1f,
                cameraDiffusionBloomIntensity = if (floats.size >= 89) nf() else 1f,
                cameraDiffusionBloomSize = if (floats.size >= 89) nf() else 1f,
                printDiffusionStrength = if (floats.size >= 98) nf() else 0.5f,
                printDiffusionSpatialScale = if (floats.size >= 98) nf() else 1f,
                printDiffusionHaloWarmth = if (floats.size >= 98) nf() else 0f,
                printDiffusionCoreIntensity = if (floats.size >= 98) nf() else 1f,
                printDiffusionCoreSize = if (floats.size >= 98) nf() else 1f,
                printDiffusionHaloIntensity = if (floats.size >= 98) nf() else 1f,
                printDiffusionHaloSize = if (floats.size >= 98) nf() else 1f,
                printDiffusionBloomIntensity = if (floats.size >= 98) nf() else 1f,
                printDiffusionBloomSize = if (floats.size >= 98) nf() else 1f
            ).let { look ->
                // Migrate looks persisted before process existed (17 ints):
                // positive stocks stored as print (0) would render negative.
                // Looks that already carry an explicit process (19 ints) are
                // never touched: Print is a valid manual choice for positives.
                if (ints.size == 17 && look.process == 0 && look.film in FilmStocks.positiveFilmIndices) {
                    look.copy(process = 1, scanNegativeInvert = false)
                } else {
                    look
                }
            }
        }.getOrNull()
    }

    fun encodePresets(presets: List<FilmPreset>): String = presets.joinToString("\n") { p ->
        listOf(enc(p.id), enc(p.name), encodeLook(p.look)).joinToString("|")
    }

    fun decodePresets(raw: String?): List<FilmPreset> = raw
        .orEmpty()
        .lineSequence()
        .filter { it.isNotBlank() }
        .mapNotNull { line ->
            runCatching {
                val a = line.split('|', limit = 3)
                require(a.size == 3)
                FilmPreset(dec(a[0]), dec(a[1]), decodeLook(a[2]) ?: FilmSimLook())
            }.getOrNull()
        }.toList()
}
