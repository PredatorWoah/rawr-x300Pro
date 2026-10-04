package com.rawr.camera.settings.model

/**
 * Single source of truth for every film-simulation control.
 *
 * Three surfaces read this catalog — Settings screens, the capture
 * viewfinder strip and the renderer film tab (which reuses the Settings
 * screens) — so a control added here with its subsection automatically
 * appears everywhere with the same membership. Presentation stays with
 * each surface: Settings renders titled groups with switches/chips/drills,
 * the strip renders compact scrubbable rows.
 *
 * Conventions:
 * - [FilmSubsection.title] is the Settings group title; [shortLabel] is the
 *   compact strip label (uppercase, ellipsized to one line there).
 * - Numeric ordering inside a subsection is display order everywhere.
 * - [FilmSimDiscreteField.options] is the only option list: the Settings
 *   chip rows, the output-space selector and the strip all read it, so a
 *   non-functional option hidden here disappears everywhere.
 */
sealed interface FilmControl {
    data class Numeric(val parameter: FilmSimNumericParameter) : FilmControl
    data class Discrete(val field: FilmSimDiscreteField) : FilmControl
    data class Flag(val flag: FilmSimFlag) : FilmControl
}

data class FilmSubsection(
    val key: String,
    val title: String,
    val shortLabel: String,
    val controls: List<FilmControl>
) {
    fun numerics(): List<FilmSimNumericSpec> = controls
        .filterIsInstance<FilmControl.Numeric>()
        .map { FilmSimSpecs.forParameter(it.parameter) }
}

data class FilmSection(
    val section: FilmSimSection,
    val subsections: List<FilmSubsection>
) {
    fun controls(): List<FilmControl> = subsections.flatMap { it.controls }
}

object FilmSimCatalog {
    private fun n(parameter: FilmSimNumericParameter) = FilmControl.Numeric(parameter)
    private fun d(field: FilmSimDiscreteField) = FilmControl.Discrete(field)
    private fun f(flag: FilmSimFlag) = FilmControl.Flag(flag)
    private fun sub(key: String, title: String, shortLabel: String, controls: List<FilmControl>) =
        FilmSubsection(key, title, shortLabel, controls)

    val sections: List<FilmSection> = listOf(
        FilmSection(
            FilmSimSection.Film, listOf(
                sub("stock", "Stock", "STOCK", listOf(d(FilmSimDiscreteField.Film), d(FilmSimDiscreteField.FilmFormat))),
                sub("workflow", "Workflow", "WORKFLOW", listOf(d(FilmSimDiscreteField.Process), f(FilmSimFlag.ScanNegativeInvert))),
                sub("exposure", "Exposure", "EXPOSURE", listOf(
                    n(FilmSimNumericParameter.FilmExposureEv),
                    n(FilmSimNumericParameter.FilmPushPullStops),
                    d(FilmSimDiscreteField.PushPullMode)
                )),
                sub("chemistry", "Chemistry", "CHEMISTRY", listOf(
                    n(FilmSimNumericParameter.FilmGamma),
                    n(FilmSimNumericParameter.NegativeBleachBypassAmount),
                    n(FilmSimNumericParameter.NegativeLeucoCyanCoupling)
                )),
                sub("model", "Spectral model", "MODEL", listOf(d(FilmSimDiscreteField.SpectralMethod)))
            )
        ),
        FilmSection(
            FilmSimSection.DirCouplers, listOf(
                sub("couplers", "Couplers", "COUPLERS", listOf(
                    n(FilmSimNumericParameter.DirCouplersAmount),
                    n(FilmSimNumericParameter.DirCouplersDiffusionUm),
                    n(FilmSimNumericParameter.DirCouplersDiffusionTailUm),
                    n(FilmSimNumericParameter.DirCouplersDiffusionTailWeight),
                    n(FilmSimNumericParameter.DirCouplersInhibitionSameLayer),
                    n(FilmSimNumericParameter.DirCouplersInhibitionInterlayer)
                )),
                sub("calib", "Calibration", "CALIB", listOf(
                    n(FilmSimNumericParameter.DirCouplersGammaSameLayerR),
                    n(FilmSimNumericParameter.DirCouplersGammaSameLayerG),
                    n(FilmSimNumericParameter.DirCouplersGammaSameLayerB),
                    n(FilmSimNumericParameter.DirCouplersGammaRToG),
                    n(FilmSimNumericParameter.DirCouplersGammaRToB),
                    n(FilmSimNumericParameter.DirCouplersGammaGToR),
                    n(FilmSimNumericParameter.DirCouplersGammaGToB),
                    n(FilmSimNumericParameter.DirCouplersGammaBToR),
                    n(FilmSimNumericParameter.DirCouplersGammaBToG)
                ))
            )
        ),
        FilmSection(
            FilmSimSection.Print, listOf(
                sub("paper", "Paper", "PAPER", listOf(d(FilmSimDiscreteField.Paper))),
                sub("exposure", "Exposure", "EXPOSURE", listOf(
                    n(FilmSimNumericParameter.PrintExposureEv),
                    n(FilmSimNumericParameter.PrintGamma)
                )),
                sub("chemistry", "Chemistry", "CHEMISTRY", listOf(
                    n(FilmSimNumericParameter.PrintPushPullStops),
                    n(FilmSimNumericParameter.PrintBleachBypassAmount),
                    n(FilmSimNumericParameter.PrintShadowShape),
                    n(FilmSimNumericParameter.PrintHighlightShape)
                )),
                sub("filtration", "Filtration", "FILTRATION", listOf(
                    n(FilmSimNumericParameter.FilterMShift),
                    n(FilmSimNumericParameter.FilterYShift)
                )),
                sub("lab", "Lab", "LAB", listOf(
                    n(FilmSimNumericParameter.FilterC),
                    n(FilmSimNumericParameter.PrinterLightsR),
                    n(FilmSimNumericParameter.PrinterLightsG),
                    n(FilmSimNumericParameter.PrinterLightsB),
                    f(FilmSimFlag.PrinterLightsGang),
                    f(FilmSimFlag.PrinterLightCalibration)
                )),
                sub("preflash", "Preflash", "PREFLASH", listOf(
                    n(FilmSimNumericParameter.PreflashExposure),
                    n(FilmSimNumericParameter.PreflashMFilterShift),
                    n(FilmSimNumericParameter.PreflashYFilterShift)
                )),
                sub("geometry", "Enlarger geometry", "GEOMETRY", listOf(
                    n(FilmSimNumericParameter.EnlargerScale),
                    n(FilmSimNumericParameter.EnlargerOffsetXPercent),
                    n(FilmSimNumericParameter.EnlargerOffsetYPercent)
                ))
            )
        ),
        FilmSection(
            FilmSimSection.Filters, listOf(
                sub("filtration", "Filtration", "FILTRATION", listOf(
                    f(FilmSimFlag.CameraUvEnabled),
                    f(FilmSimFlag.CameraIrEnabled),
                    n(FilmSimNumericParameter.CameraUvCutNm),
                    n(FilmSimNumericParameter.CameraIrCutNm)
                ))
            )
        ),
        FilmSection(
            FilmSimSection.Diffusion, listOf(
                sub("camera-setup", "Setup", "SETUP", listOf(
                    f(FilmSimFlag.CameraDiffusionEnabled),
                    d(FilmSimDiscreteField.CameraDiffusionFamily)
                )),
                sub("camera-main", "Diffusion", "MAIN", listOf(
                    n(FilmSimNumericParameter.CameraDiffusionStrength),
                    n(FilmSimNumericParameter.CameraDiffusionSpatialScale),
                    n(FilmSimNumericParameter.CameraDiffusionHaloWarmth)
                )),
                sub("camera-shaping", "Shaping", "SHAPING", listOf(
                    n(FilmSimNumericParameter.CameraDiffusionCoreIntensity),
                    n(FilmSimNumericParameter.CameraDiffusionCoreSize),
                    n(FilmSimNumericParameter.CameraDiffusionHaloIntensity),
                    n(FilmSimNumericParameter.CameraDiffusionHaloSize),
                    n(FilmSimNumericParameter.CameraDiffusionBloomIntensity),
                    n(FilmSimNumericParameter.CameraDiffusionBloomSize)
                )),
                sub("print-setup", "Setup", "SETUP", listOf(
                    f(FilmSimFlag.PrintDiffusionEnabled),
                    d(FilmSimDiscreteField.PrintDiffusionFamily)
                )),
                sub("print-main", "Diffusion", "MAIN", listOf(
                    n(FilmSimNumericParameter.PrintDiffusionStrength),
                    n(FilmSimNumericParameter.PrintDiffusionSpatialScale),
                    n(FilmSimNumericParameter.PrintDiffusionHaloWarmth)
                )),
                sub("print-shaping", "Shaping", "SHAPING", listOf(
                    n(FilmSimNumericParameter.PrintDiffusionCoreIntensity),
                    n(FilmSimNumericParameter.PrintDiffusionCoreSize),
                    n(FilmSimNumericParameter.PrintDiffusionHaloIntensity),
                    n(FilmSimNumericParameter.PrintDiffusionHaloSize),
                    n(FilmSimNumericParameter.PrintDiffusionBloomIntensity),
                    n(FilmSimNumericParameter.PrintDiffusionBloomSize)
                ))
            )
        ),
        FilmSection(
            FilmSimSection.Grain, listOf(
                sub("setup", "Setup", "SETUP", listOf(
                    f(FilmSimFlag.GrainEnabled),
                    f(FilmSimFlag.GrainSublayersEnabled),
                    f(FilmSimFlag.GrainAnimate),
                    d(FilmSimDiscreteField.GrainModel),
                    d(FilmSimDiscreteField.GrainSubLayerCount)
                )),
                sub("emulsion", "Emulsion", "EMULSION", listOf(
                    n(FilmSimNumericParameter.GrainAmount),
                    n(FilmSimNumericParameter.GrainSaturation),
                    n(FilmSimNumericParameter.GrainParticleAreaUm2),
                    n(FilmSimNumericParameter.GrainParticleScaleR),
                    n(FilmSimNumericParameter.GrainParticleScaleG),
                    n(FilmSimNumericParameter.GrainParticleScaleB),
                    n(FilmSimNumericParameter.GrainParticleScaleLayer0),
                    n(FilmSimNumericParameter.GrainParticleScaleLayer1),
                    n(FilmSimNumericParameter.GrainParticleScaleLayer2),
                    n(FilmSimNumericParameter.GrainDensityMinR),
                    n(FilmSimNumericParameter.GrainDensityMinG),
                    n(FilmSimNumericParameter.GrainDensityMinB),
                    n(FilmSimNumericParameter.GrainUniformityR),
                    n(FilmSimNumericParameter.GrainUniformityG),
                    n(FilmSimNumericParameter.GrainUniformityB),
                    n(FilmSimNumericParameter.GrainMicroStructureScale),
                    n(FilmSimNumericParameter.GrainMicroStructureSigmaNm),
                    n(FilmSimNumericParameter.GrainSeed)
                )),
                sub("texture", "Texture", "TEXTURE", listOf(
                    n(FilmSimNumericParameter.GrainFinalBlurUm),
                    n(FilmSimNumericParameter.GrainBlurDyeCloudsUm)
                ))
            )
        ),
        FilmSection(
            FilmSimSection.Halation, listOf(
                sub("setup", "Setup", "SETUP", listOf(f(FilmSimFlag.HalationEnabled))),
                sub("scatter", "Scatter", "SCATTER", listOf(
                    n(FilmSimNumericParameter.ScatterAmount),
                    n(FilmSimNumericParameter.ScatterScale)
                )),
                sub("halation", "Halation", "HALATION", listOf(
                    n(FilmSimNumericParameter.HalationAmount),
                    n(FilmSimNumericParameter.HalationScale)
                )),
                sub("threshold", "Threshold", "THRESHOLD", listOf(
                    n(FilmSimNumericParameter.HalationBoostEv),
                    n(FilmSimNumericParameter.HalationBoostRange),
                    n(FilmSimNumericParameter.HalationProtectEv)
                )),
                sub("coupling", "Coupling", "COUPLING", listOf(
                    n(FilmSimNumericParameter.HalationStrengthR),
                    n(FilmSimNumericParameter.HalationStrengthG),
                    n(FilmSimNumericParameter.HalationStrengthB),
                    n(FilmSimNumericParameter.HalationFirstSigmaUmR),
                    n(FilmSimNumericParameter.HalationFirstSigmaUmG),
                    n(FilmSimNumericParameter.HalationFirstSigmaUmB)
                ))
            )
        ),
        FilmSection(
            FilmSimSection.Scanner, listOf(
                sub("setup", "Setup", "SETUP", listOf(
                    f(FilmSimFlag.ScannerEnabled),
                    f(FilmSimFlag.ScannerWhiteCorrection),
                    f(FilmSimFlag.ScannerBlackCorrection)
                )),
                sub("levels", "Levels", "LEVELS", listOf(
                    n(FilmSimNumericParameter.ScannerWhiteLevel),
                    n(FilmSimNumericParameter.ScannerBlackLevel)
                )),
                sub("sharpness", "Sharpness", "SHARPNESS", listOf(
                    n(FilmSimNumericParameter.ScannerMtf50LpMm),
                    n(FilmSimNumericParameter.ScannerUnsharpRadiusUm),
                    n(FilmSimNumericParameter.ScannerUnsharpAmount)
                )),
                sub("glare", "Glare", "GLARE", listOf(
                    n(FilmSimNumericParameter.GlarePercent),
                    n(FilmSimNumericParameter.GlareRoughness),
                    n(FilmSimNumericParameter.GlareBlur)
                ))
            )
        ),
        FilmSection(
            FilmSimSection.Output, listOf(
                sub("space", "Output", "SPACE", listOf(d(FilmSimDiscreteField.OutputColorSpace)))
            )
        )
    )

    fun section(section: FilmSimSection): FilmSection =
        sections.first { it.section == section }

    fun subsection(section: FilmSimSection, key: String): FilmSubsection =
        section(section).subsections.first { it.key == key }

    /** Slider specs for a subsection, in display order. */
    fun numerics(section: FilmSimSection, subsection: String): List<FilmSimNumericSpec> =
        subsection(section, subsection).numerics()

    /** Compact strip label for a control: hand-tuned where the spec label is too long. */
    fun shortLabel(control: FilmControl): String = when (control) {
        is FilmControl.Numeric -> numericShortLabels[control.parameter]
            ?: FilmSimSpecs.forParameter(control.parameter).label.uppercase()
        is FilmControl.Discrete -> discreteShortLabels[control.field] ?: control.field.name.uppercase()
        is FilmControl.Flag -> flagShortLabels[control.flag] ?: control.flag.name.uppercase()
    }

    private val numericShortLabels = mapOf(
        FilmSimNumericParameter.FilmExposureEv to "FILM EV",
        FilmSimNumericParameter.FilmPushPullStops to "PUSHPL",
        FilmSimNumericParameter.FilmGamma to "GAMMA",
        FilmSimNumericParameter.NegativeBleachBypassAmount to "N BLEACH",
        FilmSimNumericParameter.NegativeLeucoCyanCoupling to "LEUCO",
        FilmSimNumericParameter.PrintExposureEv to "PRINT EV",
        FilmSimNumericParameter.PrintPushPullStops to "PUSHPL",
        FilmSimNumericParameter.PrintGamma to "PRINT CON",
        FilmSimNumericParameter.PrintShadowShape to "SHADOW",
        FilmSimNumericParameter.PrintHighlightShape to "HILITE",
        FilmSimNumericParameter.PrintBleachBypassAmount to "P BLEACH",
        FilmSimNumericParameter.FilterC to "FILT C",
        FilmSimNumericParameter.FilterMShift to "FILT M",
        FilmSimNumericParameter.FilterYShift to "FILT Y",
        FilmSimNumericParameter.PreflashExposure to "PREFLASH",
        FilmSimNumericParameter.PreflashMFilterShift to "PF M",
        FilmSimNumericParameter.PreflashYFilterShift to "PF Y",
        FilmSimNumericParameter.PrinterLightsR to "LIGHT R",
        FilmSimNumericParameter.PrinterLightsG to "LIGHT G",
        FilmSimNumericParameter.PrinterLightsB to "LIGHT B",
        FilmSimNumericParameter.EnlargerScale to "SCALE",
        FilmSimNumericParameter.EnlargerOffsetXPercent to "OFF X",
        FilmSimNumericParameter.EnlargerOffsetYPercent to "OFF Y",
        FilmSimNumericParameter.GrainAmount to "GRAIN",
        FilmSimNumericParameter.GrainSaturation to "SAT",
        FilmSimNumericParameter.GrainParticleAreaUm2 to "GRAIN SZ",
        FilmSimNumericParameter.GrainParticleScaleR to "SCALE R",
        FilmSimNumericParameter.GrainParticleScaleG to "SCALE G",
        FilmSimNumericParameter.GrainParticleScaleB to "SCALE B",
        FilmSimNumericParameter.GrainParticleScaleLayer0 to "COARSE",
        FilmSimNumericParameter.GrainParticleScaleLayer1 to "MID",
        FilmSimNumericParameter.GrainParticleScaleLayer2 to "FINE",
        FilmSimNumericParameter.GrainDensityMinR to "FLOOR R",
        FilmSimNumericParameter.GrainDensityMinG to "FLOOR G",
        FilmSimNumericParameter.GrainDensityMinB to "FLOOR B",
        FilmSimNumericParameter.GrainUniformityR to "UNIF R",
        FilmSimNumericParameter.GrainUniformityG to "UNIF G",
        FilmSimNumericParameter.GrainUniformityB to "UNIF B",
        FilmSimNumericParameter.GrainFinalBlurUm to "BLUR",
        FilmSimNumericParameter.GrainBlurDyeCloudsUm to "DYE CLD",
        FilmSimNumericParameter.GrainMicroStructureScale to "MICRO",
        FilmSimNumericParameter.GrainMicroStructureSigmaNm to "M-SIGMA",
        FilmSimNumericParameter.GrainSeed to "SEED",
        FilmSimNumericParameter.CameraUvCutNm to "UV CUT",
        FilmSimNumericParameter.CameraIrCutNm to "IR CUT",
        FilmSimNumericParameter.DirCouplersAmount to "AMOUNT",
        FilmSimNumericParameter.DirCouplersDiffusionUm to "SPREAD",
        FilmSimNumericParameter.DirCouplersDiffusionTailUm to "TAIL",
        FilmSimNumericParameter.DirCouplersDiffusionTailWeight to "TAIL WT",
        FilmSimNumericParameter.DirCouplersInhibitionSameLayer to "SAME INH",
        FilmSimNumericParameter.DirCouplersInhibitionInterlayer to "INTER INH",
        FilmSimNumericParameter.DirCouplersGammaSameLayerR to "R-R",
        FilmSimNumericParameter.DirCouplersGammaSameLayerG to "G-G",
        FilmSimNumericParameter.DirCouplersGammaSameLayerB to "B-B",
        FilmSimNumericParameter.DirCouplersGammaRToG to "R-G",
        FilmSimNumericParameter.DirCouplersGammaRToB to "R-B",
        FilmSimNumericParameter.DirCouplersGammaGToR to "G-R",
        FilmSimNumericParameter.DirCouplersGammaGToB to "G-B",
        FilmSimNumericParameter.DirCouplersGammaBToR to "B-R",
        FilmSimNumericParameter.DirCouplersGammaBToG to "B-G",
        FilmSimNumericParameter.ScannerWhiteLevel to "W-LEVEL",
        FilmSimNumericParameter.ScannerBlackLevel to "B-LEVEL",
        FilmSimNumericParameter.GlarePercent to "GLARE",
        FilmSimNumericParameter.GlareRoughness to "ROUGH",
        FilmSimNumericParameter.GlareBlur to "BLUR",
        FilmSimNumericParameter.ScannerMtf50LpMm to "MTF",
        FilmSimNumericParameter.ScannerUnsharpRadiusUm to "RADIUS",
        FilmSimNumericParameter.ScannerUnsharpAmount to "AMOUNT",
        FilmSimNumericParameter.ScatterAmount to "AMOUNT",
        FilmSimNumericParameter.ScatterScale to "SIZE",
        FilmSimNumericParameter.HalationAmount to "AMOUNT",
        FilmSimNumericParameter.HalationScale to "SIZE",
        FilmSimNumericParameter.HalationStrengthR to "STR R",
        FilmSimNumericParameter.HalationStrengthG to "STR G",
        FilmSimNumericParameter.HalationStrengthB to "STR B",
        FilmSimNumericParameter.HalationFirstSigmaUmR to "SIG R",
        FilmSimNumericParameter.HalationFirstSigmaUmG to "SIG G",
        FilmSimNumericParameter.HalationFirstSigmaUmB to "SIG B",
        FilmSimNumericParameter.HalationBoostEv to "BOOST",
        FilmSimNumericParameter.HalationBoostRange to "RANGE",
        FilmSimNumericParameter.HalationProtectEv to "PROTECT",
        FilmSimNumericParameter.CameraDiffusionStrength to "STRENGTH",
        FilmSimNumericParameter.CameraDiffusionSpatialScale to "SIZE",
        FilmSimNumericParameter.CameraDiffusionHaloWarmth to "WARMTH",
        FilmSimNumericParameter.CameraDiffusionCoreIntensity to "C-INT",
        FilmSimNumericParameter.CameraDiffusionCoreSize to "C-SIZE",
        FilmSimNumericParameter.CameraDiffusionHaloIntensity to "H-INT",
        FilmSimNumericParameter.CameraDiffusionHaloSize to "H-SIZE",
        FilmSimNumericParameter.CameraDiffusionBloomIntensity to "B-INT",
        FilmSimNumericParameter.CameraDiffusionBloomSize to "B-SIZE",
        FilmSimNumericParameter.PrintDiffusionStrength to "STRENGTH",
        FilmSimNumericParameter.PrintDiffusionSpatialScale to "SIZE",
        FilmSimNumericParameter.PrintDiffusionHaloWarmth to "WARMTH",
        FilmSimNumericParameter.PrintDiffusionCoreIntensity to "C-INT",
        FilmSimNumericParameter.PrintDiffusionCoreSize to "C-SIZE",
        FilmSimNumericParameter.PrintDiffusionHaloIntensity to "H-INT",
        FilmSimNumericParameter.PrintDiffusionHaloSize to "H-SIZE",
        FilmSimNumericParameter.PrintDiffusionBloomIntensity to "B-INT",
        FilmSimNumericParameter.PrintDiffusionBloomSize to "B-SIZE"
    )

    private val discreteShortLabels = mapOf(
        FilmSimDiscreteField.Film to "STOCK",
        FilmSimDiscreteField.Paper to "PAPER",
        FilmSimDiscreteField.FilmFormat to "FORMAT",
        FilmSimDiscreteField.Process to "PROCESS",
        FilmSimDiscreteField.SpectralMethod to "METHOD",
        FilmSimDiscreteField.PushPullMode to "PUSHMODE",
        FilmSimDiscreteField.GrainModel to "MODEL",
        FilmSimDiscreteField.GrainSubLayerCount to "LAYERS",
        FilmSimDiscreteField.InputColorSpace to "IN SPACE",
        FilmSimDiscreteField.OutputColorSpace to "OUT SPACE",
        FilmSimDiscreteField.CameraDiffusionFamily to "FAMILY",
        FilmSimDiscreteField.PrintDiffusionFamily to "P FAMILY"
    )

    private val flagShortLabels = mapOf(
        FilmSimFlag.PrinterLightsGang to "PL GANG",
        FilmSimFlag.PrinterLightCalibration to "PL CAL",
        FilmSimFlag.GrainEnabled to "GRAIN",
        FilmSimFlag.GrainSublayersEnabled to "SUBLAYERS",
        FilmSimFlag.GrainAnimate to "ANIMATE",
        FilmSimFlag.CameraUvEnabled to "UV",
        FilmSimFlag.CameraIrEnabled to "IR",
        FilmSimFlag.ScanNegativeInvert to "SCAN INV",
        FilmSimFlag.ScannerEnabled to "SCANNER",
        FilmSimFlag.ScannerWhiteCorrection to "WHITE CORR",
        FilmSimFlag.ScannerBlackCorrection to "BLACK CORR",
        FilmSimFlag.HalationEnabled to "HALATION",
        FilmSimFlag.CameraDiffusionEnabled to "DIFFUSION",
        FilmSimFlag.PrintDiffusionEnabled to "PRT DIFF"
    )
}

/**
 * Strip-visible options for a discrete field. Output space mirrors the
 * Settings selector: only preview-verified display encodings are offered
 * (log/linear spaces stall the phone panel). Input space is never offered
 * anywhere: the film buffer is always linear sRGB.
 */
fun FilmSimDiscreteField.options(): List<String> = when (this) {
    FilmSimDiscreteField.Film -> FilmStocks.films
    FilmSimDiscreteField.Paper -> FilmStocks.papers
    FilmSimDiscreteField.InputColorSpace -> emptyList()
    FilmSimDiscreteField.OutputColorSpace ->
        FilmStocks.previewSupportedOutputSpaces.sorted().map { FilmStocks.colorSpaces[it] }
    FilmSimDiscreteField.SpectralMethod -> FilmStocks.spectralMethods
    FilmSimDiscreteField.PushPullMode -> FilmStocks.pushPullModes
    FilmSimDiscreteField.FilmFormat -> FilmStocks.filmFormats
    FilmSimDiscreteField.GrainSubLayerCount -> (1..8).map { it.toString() }
    FilmSimDiscreteField.Process -> FilmStocks.processes
    FilmSimDiscreteField.CameraDiffusionFamily -> FilmStocks.diffusionFamilies
    FilmSimDiscreteField.PrintDiffusionFamily -> FilmStocks.diffusionFamilies
    FilmSimDiscreteField.GrainModel -> FilmStocks.grainModels
}

/** Position within [options] for the stored look value. */
fun FilmSimLook.discretePosition(field: FilmSimDiscreteField): Int = when (field) {
    FilmSimDiscreteField.Film -> film
    FilmSimDiscreteField.Paper -> paper
    FilmSimDiscreteField.InputColorSpace -> 0
    FilmSimDiscreteField.OutputColorSpace ->
        FilmStocks.previewSupportedOutputSpaces.sorted()
            .indexOf(outputColorSpace).takeIf { it >= 0 } ?: 0
    FilmSimDiscreteField.SpectralMethod -> rgbToRawMethod
    FilmSimDiscreteField.PushPullMode -> filmPushPullMode
    FilmSimDiscreteField.FilmFormat -> filmFormat
    FilmSimDiscreteField.GrainSubLayerCount -> grainSubLayerCount - 1
    FilmSimDiscreteField.Process -> process
    FilmSimDiscreteField.CameraDiffusionFamily -> cameraDiffusionFamily
    FilmSimDiscreteField.PrintDiffusionFamily -> printDiffusionFamily
    FilmSimDiscreteField.GrainModel -> grainModel
}

/** Stored look value for a strip/selector option position. */
fun FilmSimDiscreteField.storedValue(position: Int): Int = when (this) {
    FilmSimDiscreteField.OutputColorSpace ->
        FilmStocks.previewSupportedOutputSpaces.sorted().getOrElse(position) {
            FilmStocks.previewSupportedOutputSpaces.min()
        }
    // Sub-layer count stores the raw count (1-8), not a zero-based index.
    FilmSimDiscreteField.GrainSubLayerCount -> position + 1
    else -> position
}

fun FilmSimLook.flagValue(flag: FilmSimFlag): Boolean = when (flag) {
    FilmSimFlag.PrinterLightsGang -> printerLightsGang
    FilmSimFlag.PrinterLightCalibration -> printerLightCalibration
    FilmSimFlag.GrainEnabled -> grainEnabled
    FilmSimFlag.GrainSublayersEnabled -> grainSublayersEnabled
    FilmSimFlag.GrainAnimate -> grainAnimate
    FilmSimFlag.CameraUvEnabled -> cameraUvFilterEnabled
    FilmSimFlag.CameraIrEnabled -> cameraIrFilterEnabled
    FilmSimFlag.ScanNegativeInvert -> scanNegativeInvert
    FilmSimFlag.ScannerEnabled -> scannerEnabled
    FilmSimFlag.ScannerWhiteCorrection -> scannerWhiteCorrection
    FilmSimFlag.ScannerBlackCorrection -> scannerBlackCorrection
    FilmSimFlag.HalationEnabled -> halationEnabled
    FilmSimFlag.CameraDiffusionEnabled -> cameraDiffusionEnabled
    FilmSimFlag.PrintDiffusionEnabled -> printDiffusionEnabled
}

fun FilmSimLook.numericValue(param: FilmSimNumericParameter): Float = when (param) {
    FilmSimNumericParameter.FilmExposureEv -> filmExposureEv
    FilmSimNumericParameter.PrintExposureEv -> printExposureEv
    FilmSimNumericParameter.FilmPushPullStops -> filmPushPullStops
    FilmSimNumericParameter.FilmGamma -> filmGamma
    FilmSimNumericParameter.PrintPushPullStops -> printPushPullStops
    FilmSimNumericParameter.PrintGamma -> printGamma
    FilmSimNumericParameter.PrintShadowShape -> printShadowShape
    FilmSimNumericParameter.PrintHighlightShape -> printHighlightShape
    FilmSimNumericParameter.NegativeBleachBypassAmount -> negativeBleachBypassAmount
    FilmSimNumericParameter.NegativeLeucoCyanCoupling -> negativeLeucoCyanCoupling
    FilmSimNumericParameter.PrintBleachBypassAmount -> printBleachBypassAmount
    FilmSimNumericParameter.PreflashExposure -> preflashExposure
    FilmSimNumericParameter.PreflashMFilterShift -> preflashMFilterShift
    FilmSimNumericParameter.PreflashYFilterShift -> preflashYFilterShift
    FilmSimNumericParameter.FilterC -> filterC
    FilmSimNumericParameter.FilterMShift -> filterMShift
    FilmSimNumericParameter.FilterYShift -> filterYShift
    FilmSimNumericParameter.PrinterLightsR -> printerLightsR
    FilmSimNumericParameter.PrinterLightsG -> printerLightsG
    FilmSimNumericParameter.PrinterLightsB -> printerLightsB
    FilmSimNumericParameter.EnlargerScale -> enlargerScale
    FilmSimNumericParameter.EnlargerOffsetXPercent -> enlargerOffsetXPercent
    FilmSimNumericParameter.EnlargerOffsetYPercent -> enlargerOffsetYPercent
    FilmSimNumericParameter.GrainAmount -> grainAmount
    FilmSimNumericParameter.GrainSaturation -> grainSaturation
    FilmSimNumericParameter.GrainParticleAreaUm2 -> grainParticleAreaUm2
    FilmSimNumericParameter.GrainParticleScaleR -> grainParticleScaleR
    FilmSimNumericParameter.GrainParticleScaleG -> grainParticleScaleG
    FilmSimNumericParameter.GrainParticleScaleB -> grainParticleScaleB
    FilmSimNumericParameter.GrainParticleScaleLayer0 -> grainParticleScaleLayer0
    FilmSimNumericParameter.GrainParticleScaleLayer1 -> grainParticleScaleLayer1
    FilmSimNumericParameter.GrainParticleScaleLayer2 -> grainParticleScaleLayer2
    FilmSimNumericParameter.GrainDensityMinR -> grainDensityMinR
    FilmSimNumericParameter.GrainDensityMinG -> grainDensityMinG
    FilmSimNumericParameter.GrainDensityMinB -> grainDensityMinB
    FilmSimNumericParameter.GrainUniformityR -> grainUniformityR
    FilmSimNumericParameter.GrainUniformityG -> grainUniformityG
    FilmSimNumericParameter.GrainUniformityB -> grainUniformityB
    FilmSimNumericParameter.GrainFinalBlurUm -> grainFinalBlurUm
    FilmSimNumericParameter.GrainBlurDyeCloudsUm -> grainBlurDyeCloudsUm
    FilmSimNumericParameter.GrainMicroStructureScale -> grainMicroStructureScale
    FilmSimNumericParameter.GrainMicroStructureSigmaNm -> grainMicroStructureSigmaNm
    FilmSimNumericParameter.CameraUvCutNm -> cameraUvCutNm
    FilmSimNumericParameter.CameraIrCutNm -> cameraIrCutNm
    FilmSimNumericParameter.GrainSeed -> grainSeed.toFloat()
    FilmSimNumericParameter.DirCouplersAmount -> dirCouplersAmount
    FilmSimNumericParameter.DirCouplersDiffusionUm -> dirCouplersDiffusionUm
    FilmSimNumericParameter.DirCouplersDiffusionTailUm -> dirCouplersDiffusionTailUm
    FilmSimNumericParameter.DirCouplersDiffusionTailWeight -> dirCouplersDiffusionTailWeight
    FilmSimNumericParameter.DirCouplersInhibitionSameLayer -> dirCouplersInhibitionSameLayer
    FilmSimNumericParameter.DirCouplersInhibitionInterlayer -> dirCouplersInhibitionInterlayer
    FilmSimNumericParameter.DirCouplersGammaSameLayerR -> dirCouplersGammaSameLayerR
    FilmSimNumericParameter.DirCouplersGammaSameLayerG -> dirCouplersGammaSameLayerG
    FilmSimNumericParameter.DirCouplersGammaSameLayerB -> dirCouplersGammaSameLayerB
    FilmSimNumericParameter.DirCouplersGammaRToG -> dirCouplersGammaRToG
    FilmSimNumericParameter.DirCouplersGammaRToB -> dirCouplersGammaRToB
    FilmSimNumericParameter.DirCouplersGammaGToR -> dirCouplersGammaGToR
    FilmSimNumericParameter.DirCouplersGammaGToB -> dirCouplersGammaGToB
    FilmSimNumericParameter.DirCouplersGammaBToR -> dirCouplersGammaBToR
    FilmSimNumericParameter.DirCouplersGammaBToG -> dirCouplersGammaBToG
    FilmSimNumericParameter.ScannerWhiteLevel -> scannerWhiteLevel
    FilmSimNumericParameter.ScannerBlackLevel -> scannerBlackLevel
    FilmSimNumericParameter.GlarePercent -> glarePercent
    FilmSimNumericParameter.GlareRoughness -> glareRoughness
    FilmSimNumericParameter.GlareBlur -> glareBlur
    FilmSimNumericParameter.ScannerMtf50LpMm -> scannerMtf50LpMm
    FilmSimNumericParameter.ScannerUnsharpRadiusUm -> scannerUnsharpRadiusUm
    FilmSimNumericParameter.ScannerUnsharpAmount -> scannerUnsharpAmount
    FilmSimNumericParameter.ScatterAmount -> scatterAmount
    FilmSimNumericParameter.ScatterScale -> scatterScale
    FilmSimNumericParameter.HalationAmount -> halationAmount
    FilmSimNumericParameter.HalationScale -> halationScale
    FilmSimNumericParameter.HalationStrengthR -> halationStrengthR
    FilmSimNumericParameter.HalationStrengthG -> halationStrengthG
    FilmSimNumericParameter.HalationStrengthB -> halationStrengthB
    FilmSimNumericParameter.HalationFirstSigmaUmR -> halationFirstSigmaUmR
    FilmSimNumericParameter.HalationFirstSigmaUmG -> halationFirstSigmaUmG
    FilmSimNumericParameter.HalationFirstSigmaUmB -> halationFirstSigmaUmB
    FilmSimNumericParameter.HalationBoostEv -> halationBoostEv
    FilmSimNumericParameter.HalationBoostRange -> halationBoostRange
    FilmSimNumericParameter.HalationProtectEv -> halationProtectEv
    FilmSimNumericParameter.CameraDiffusionStrength -> cameraDiffusionStrength
    FilmSimNumericParameter.CameraDiffusionSpatialScale -> cameraDiffusionSpatialScale
    FilmSimNumericParameter.CameraDiffusionHaloWarmth -> cameraDiffusionHaloWarmth
    FilmSimNumericParameter.CameraDiffusionCoreIntensity -> cameraDiffusionCoreIntensity
    FilmSimNumericParameter.CameraDiffusionCoreSize -> cameraDiffusionCoreSize
    FilmSimNumericParameter.CameraDiffusionHaloIntensity -> cameraDiffusionHaloIntensity
    FilmSimNumericParameter.CameraDiffusionHaloSize -> cameraDiffusionHaloSize
    FilmSimNumericParameter.CameraDiffusionBloomIntensity -> cameraDiffusionBloomIntensity
    FilmSimNumericParameter.CameraDiffusionBloomSize -> cameraDiffusionBloomSize
    FilmSimNumericParameter.PrintDiffusionStrength -> printDiffusionStrength
    FilmSimNumericParameter.PrintDiffusionSpatialScale -> printDiffusionSpatialScale
    FilmSimNumericParameter.PrintDiffusionHaloWarmth -> printDiffusionHaloWarmth
    FilmSimNumericParameter.PrintDiffusionCoreIntensity -> printDiffusionCoreIntensity
    FilmSimNumericParameter.PrintDiffusionCoreSize -> printDiffusionCoreSize
    FilmSimNumericParameter.PrintDiffusionHaloIntensity -> printDiffusionHaloIntensity
    FilmSimNumericParameter.PrintDiffusionHaloSize -> printDiffusionHaloSize
    FilmSimNumericParameter.PrintDiffusionBloomIntensity -> printDiffusionBloomIntensity
    FilmSimNumericParameter.PrintDiffusionBloomSize -> printDiffusionBloomSize
}
