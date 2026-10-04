#pragma once

#include <algorithm>
#include <cstring>

#include "spektrafilm/SpektraFilm.h"
#include "spektrafilm/SpektraProfileCurves.h"

namespace spektrafilm_native::dir {

// The corrected density table is inverted by a binary search. Reversal
// stocks have falling density curves, so strong DIR can fold its exposure
// axis and make that search undefined. Scale the 0..1 control to the largest
// correction that retains at least 10% of every original exposure interval.
inline float effectiveAmount(const spektrafilm::ProfileCurveSet& film,
                             const FilmLook& look) noexcept {
    const float requested = std::clamp(look.dirCouplersAmount, 0.0f, 1.0f);
    if (requested == 0.0f || !film.type ||
        std::strcmp(film.type, "positive") != 0 ||
        film.exposureCount < 2u) {
        return requested;
    }

    const float same = std::max(look.dirCouplersInhibitionSameLayer, 0.0f);
    const float inter = std::max(look.dirCouplersInhibitionInterlayer, 0.0f);
    // Row = inhibitor-releasing layer, column = affected layer; this is the
    // same matrix order as fillDirFloats and SpektraDir.comp.
    const double gamma[3][3] = {
        {look.dirCouplersGammaSameLayerR * same, look.dirCouplersGammaRToG * inter,
         look.dirCouplersGammaRToB * inter},
        {look.dirCouplersGammaGToR * inter, look.dirCouplersGammaSameLayerG * same,
         look.dirCouplersGammaGToB * inter},
        {look.dirCouplersGammaBToR * inter, look.dirCouplersGammaBToG * inter,
         look.dirCouplersGammaSameLayerB * same},
    };
    double safeScale = 1.0;
    for (uint32_t i = 0; i + 1u < film.exposureCount; ++i) {
        const double step = double(film.logExposure[i + 1u]) - film.logExposure[i];
        if (!(step > 0.0)) return 0.0f;
        for (int receiver = 0; receiver < 3; ++receiver) {
            double inhibitorStep = 0.0;
            for (int source = 0; source < 3; ++source) {
                const double silverStep =
                    double(film.densityCurves[i * 3u + source]) -
                    film.densityCurves[(i + 1u) * 3u + source];
                inhibitorStep += gamma[source][receiver] * silverStep;
            }
            if (inhibitorStep > 0.0) {
                safeScale = std::min(safeScale, 0.9 * step / inhibitorStep);
            }
        }
    }
    return requested * static_cast<float>(std::clamp(safeScale, 0.0, 1.0));
}

}  // namespace spektrafilm_native::dir
