package com.rawr.camera.renderer

import com.rawr.camera.settings.model.DenoiseConfig
import com.rawr.camera.settings.model.SettingsValues

/**
 * Tier classifier for the Renderer Develop tab.
 *
 * Tier A (live): tonemap/WB/film/JPEG and numeric post-stage sliders flow
 * straight through [RendererStore.updateDraft] and the proxy overview.
 *
 * Tier B/C (staged behind Apply): anything that needs an exact
 * re-render (distortion warp, HL method, defringe toggle, denoise, FCC steps) or a
 * full demosaic rebuild (algorithm, dual, quadfix, shading). Staging them
 * avoids a seconds-long prepare storm per tap on 200MP sources.
 */
internal object RendererDevelopStaging {
    /** Show live tone/color edits without prematurely applying expensive
     * Develop controls that are still waiting for Apply. */
    fun appliedPreview(applied: SettingsValues, editing: SettingsValues): SettingsValues = editing.copy(
        demosaicAlgorithm = applied.demosaicAlgorithm,
        dualAutoContrast = applied.dualAutoContrast,
        dualContrastPercent = applied.dualContrastPercent,
        quadfixEnabled = applied.quadfixEnabled,
        quadfixFastMedian = applied.quadfixFastMedian,
        photoFccSteps = applied.photoFccSteps,
        photoDefringeEnabled = applied.photoDefringeEnabled,
        photoDenoise = applied.photoDenoise,
        photoLensShadingEnabled = applied.photoLensShadingEnabled,
        distortionCorrectionEnabled = applied.distortionCorrectionEnabled,
        photoHighlightEnabled = applied.photoHighlightEnabled,
        photoHighlightMethod = applied.photoHighlightMethod,
    )
    /** True when [after] differs from [before] in any staged (Tier B/C) field. */
    fun isStagedChange(before: SettingsValues, after: SettingsValues): Boolean =
        stagedLabels(before, after).isNotEmpty()

    /** Short human labels of the staged differences, for the Apply bar. */
    fun stagedLabels(before: SettingsValues, after: SettingsValues): List<String> = buildList {
        if (before.demosaicAlgorithm != after.demosaicAlgorithm) add("Demosaic")
        if (before.dualAutoContrast != after.dualAutoContrast ||
            before.dualContrastPercent != after.dualContrastPercent
        ) add("Dual contrast")
        if (before.quadfixEnabled != after.quadfixEnabled ||
            before.quadfixFastMedian != after.quadfixFastMedian
        ) add("Quad filter")
        if (before.photoFccSteps != after.photoFccSteps) add("FCC")
        if (before.photoDefringeEnabled != after.photoDefringeEnabled) add("Defringe")
        // Wavelet vs galosh parts compare separately so the Apply bar keeps
        // its existing "Denoise" / "Galosh" labels.
        val beforeWavelet = (before.photoDenoise as? DenoiseConfig.Wavelet)?.config
        val afterWavelet = (after.photoDenoise as? DenoiseConfig.Wavelet)?.config
        if (beforeWavelet != afterWavelet) add("Denoise")
        val beforeGalosh = before.photoDenoise as? DenoiseConfig.Galosh
        val afterGalosh = after.photoDenoise as? DenoiseConfig.Galosh
        if (beforeGalosh != afterGalosh) add("Galosh")
        if (before.photoLensShadingEnabled != after.photoLensShadingEnabled) add("Shading")
        if (before.distortionCorrectionEnabled != after.distortionCorrectionEnabled) add("Distortion")
        if (before.photoHighlightEnabled != after.photoHighlightEnabled ||
            before.photoHighlightMethod != after.photoHighlightMethod
        ) add("Highlights")
    }
}
