package com.rawr.camera.model

/**
 * Single owner of ToneParameter <-> tone-state field mappings.
 *
 * Previously triplicated across CaptureViewModel (persist + apply) and
 * NativeCaptureScreenController (scrub read); the legacy ImageToneState
 * property names (blacks/midtones/whites) are isolated
 * here so the mapping reads correctly at every call site.
 */
fun ImageToneState.withValue(parameter: ToneParameter, value: Float): ImageToneState =
    when (parameter) {
        ToneParameter.Blacks -> copy(blacks = value)
        ToneParameter.Shadows -> copy(shadows = value)
        ToneParameter.Contrast -> copy(contrast = value)
        ToneParameter.Midtones -> copy(midtones = value)
        ToneParameter.Highlights -> copy(highlights = value)
        ToneParameter.Whites -> copy(whites = value)
        ToneParameter.Saturation -> copy(saturation = value)
        ToneParameter.Vibrance -> copy(vibrance = value)
    }

/** Reads the field bound to [parameter]. */
fun ImageToneState.valueFor(parameter: ToneParameter): Float =
    when (parameter) {
        ToneParameter.Blacks -> blacks
        ToneParameter.Shadows -> shadows
        ToneParameter.Contrast -> contrast
        ToneParameter.Midtones -> midtones
        ToneParameter.Highlights -> highlights
        ToneParameter.Whites -> whites
        ToneParameter.Saturation -> saturation
        ToneParameter.Vibrance -> vibrance
    }

/** Catalog-driven read: null [TonemapParam.param] is render exposure. */
fun ImageToneState.valueFor(descriptor: TonemapParam): Float =
    descriptor.param?.let { valueFor(it) } ?: renderExposure

/** Catalog-driven write: null [TonemapParam.param] is render exposure. */
fun ImageToneState.withValue(descriptor: TonemapParam, value: Float): ImageToneState =
    descriptor.param?.let { withValue(it, value) } ?: copy(renderExposure = value)

/** Catalog-driven read from a per-profile tone set. */
fun com.rawr.camera.settings.model.ProfileTone.valueFor(descriptor: TonemapParam): Float =
    descriptor.param?.let { valueFor(it) } ?: renderExposure

fun CaptureUiState.toneValueFor(parameter: ToneParameter): Int =
    when (parameter) {
        ToneParameter.Blacks -> blacks
        ToneParameter.Shadows -> shadows
        ToneParameter.Contrast -> contrast
        ToneParameter.Midtones -> midtones
        ToneParameter.Highlights -> highlights
        ToneParameter.Whites -> whites
        ToneParameter.Saturation -> saturation
        ToneParameter.Vibrance -> vibrance
    }

/** Capture-drawer scrub target applied to the active profile's [ProfileTone]. */
fun com.rawr.camera.settings.model.ProfileTone.withToneParameter(
    parameter: ToneParameter,
    value: Float
): com.rawr.camera.settings.model.ProfileTone =
    when (parameter) {
        ToneParameter.Blacks -> copy(blacks = value)
        ToneParameter.Shadows -> copy(shadows = value)
        ToneParameter.Contrast -> copy(contrast = value)
        ToneParameter.Midtones -> copy(midtones = value)
        ToneParameter.Highlights -> copy(highlights = value)
        ToneParameter.Whites -> copy(whites = value)
        ToneParameter.Saturation -> copy(saturation = value)
        ToneParameter.Vibrance -> copy(vibrance = value)
    }

/** Value of [parameter] stored in this [com.rawr.camera.settings.model.ProfileTone]. */
fun com.rawr.camera.settings.model.ProfileTone.valueFor(parameter: ToneParameter): Float =
    when (parameter) {
        ToneParameter.Blacks -> blacks
        ToneParameter.Shadows -> shadows
        ToneParameter.Contrast -> contrast
        ToneParameter.Midtones -> midtones
        ToneParameter.Highlights -> highlights
        ToneParameter.Whites -> whites
        ToneParameter.Saturation -> saturation
        ToneParameter.Vibrance -> vibrance
    }
