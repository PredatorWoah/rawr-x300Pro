#pragma once

#include <iomanip>
#include <sstream>
#include <string>

#include "tonemap/TonemapMath.h"

namespace rawrcam::encoding::jpeg {

// Human-readable capture/render settings block stamped into JPEG
// ImageDescription/XMP. Single owner shared by the still capture path and
// the offline Renderer export path so re-renders report identically.
// Film path bypasses the color-render profile / tonemap LUT entirely, so a
// film render reports the film block instead of a profile that never
// touched the pixels.
inline std::string buildPublicDescription(const tonemap::TonemapParams& p, const std::string& rendererDisplayName,
                                          bool filmRendered, const std::string& filmDescription,
                                          float denoiseStrength = 0.0f, float denoiseDetail = 1.0f,
                                          float denoiseNoiseA = 0.0f, float denoiseNoiseB = 0.0f,
                                          // GALOSH blind denoisers (0 off, 1 full, 2 chroma-only).
                                          // Default off keeps legacy output byte-identical.
                                          int galoshYuvMode = 0, float galoshYuvStrengthY = 1.0f,
                                          float galoshYuvStrengthC = 1.0f, int galoshRawMode = 0,
                                          float galoshStrength = 1.0f, float galoshLuma = 1.0f,
                                          float galoshChroma = 1.0f) {
    std::ostringstream out;
    out << std::fixed << std::setprecision(2) << "Captured with Rawr\n\n";
    if (filmRendered) {
        if (!filmDescription.empty()) {
            out << filmDescription;
        } else {
            out << "Film";
        }
        return out.str();
    }
    out << "Renderer Profile\n" << (rendererDisplayName.empty() ? "RAWR NTRL" : rendererDisplayName) << "\n\n";
    out << "Tonemap\n"
        << "Exposure: " << std::showpos << p.exposureEV << std::noshowpos << " EV\n"
        << "Post Gain: " << p.aePostGain << "x\n"
        << "Contrast: " << p.contrast << "\n"
        << "Highlights: " << p.highlightBiasEV << "\n"
        << "Shadows: " << p.shadowLiftEV << "\n"
        << "Midtones: " << p.midtoneLiftEV << "\n"
        << "Whites: " << p.whitePointEV << "\n"
        << "Blacks: " << p.blackPointEV << "\n"
        << "Saturation: " << p.saturation << "\n"
        << "Vibrance: " << p.vibrance;
    // Profiled wavelet denoise (pre-WB linear). Omitted when off so legacy
    // output stays byte-identical.
    if (denoiseStrength > 0.0f) {
        out << "\n\nDenoise\n"
            << "Strength: " << denoiseStrength << "\n"
            << "Detail: " << denoiseDetail << "\n"
            << "Noise A: " << std::setprecision(9) << denoiseNoiseA << "\n"
            << "Noise B: " << std::setprecision(9) << denoiseNoiseB;
    }
    // GALOSH blind denoisers. Same omit-when-off contract as wavelet above.
    // Labels match the TIMINGS stage names ("Denoise (galosh-yuv)").
    if (galoshYuvMode == 1 || galoshYuvMode == 2) {
        out << "\n\nDenoise (galosh-yuv)\n"
            << "Mode: " << (galoshYuvMode == 1 ? "full" : "chroma-only") << "\n"
            << std::setprecision(2) << "Strength Y: " << galoshYuvStrengthY << "\n"
            << "Strength C: " << galoshYuvStrengthC;
    }
    if (galoshRawMode == 1 || galoshRawMode == 2) {
        out << "\n\nDenoise (galosh-raw)\n"
            << "Mode: " << (galoshRawMode == 1 ? "full" : "chroma-only") << "\n"
            << std::setprecision(2) << "Strength: " << galoshStrength << "\n"
            << "Luma: " << galoshLuma << "\n"
            << "Chroma: " << galoshChroma;
    }
    return out.str();
}

}  // namespace rawrcam::encoding::jpeg
