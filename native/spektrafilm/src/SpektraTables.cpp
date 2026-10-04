// Table builders ported from spektrafilm-ofx/src/SpektraVulkanRenderer.cpp.
// Upstream line refs are for that file; logic is kept identical. See NOTICE.md.
#include "SpektraTables.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstring>

#include "spektrafilm/SpektraFilm.h"
#include "spektrafilm/SpektraParameters.h"
#include "spektrafilm/SpektraProfileCurves.h"

namespace spektrafilm_native::tables {
namespace {

using spektrafilm::HanatosSpectraLutInfo;
using spektrafilm::ProfileCurveSet;
using spektrafilm::RgbToRawMethod;

// --- upstream: smoothErfEdge (481) ---
float smoothErfEdge(float wavelength, float center, float width) {
    return std::erf((wavelength - center) / width) * 0.5f + 0.5f;
}

// --- upstream: makeLinearSensitivity (469) ---
std::vector<float> makeLinearSensitivity(const float* logSensitivity,
                                         uint32_t wavelengthCount) {
    std::vector<float> linear(static_cast<size_t>(wavelengthCount) * 3u, 0.0f);
    if (!logSensitivity) {
        return linear;
    }
    for (uint32_t i = 0; i < wavelengthCount * 3u; ++i) {
        const float value = std::pow(10.0f, logSensitivity[i]);
        linear[i] = std::isfinite(value) ? value : 0.0f;
    }
    return linear;
}

// --- upstream: makeMallettRawMatrix (535) ---
std::array<float, 9> makeMallettRawMatrix(
    const ProfileCurveSet& filmCurves,
    const std::vector<float>& linearSensitivity) {
    std::array<float, 9> matrix{};
    const uint32_t wavelengthCount = filmCurves.wavelengthCount;
    if (!filmCurves.mallettBasisIlluminant ||
        linearSensitivity.size() < static_cast<size_t>(wavelengthCount) * 3u) {
        return matrix;
    }
    for (uint32_t wavelength = 0; wavelength < wavelengthCount; ++wavelength) {
        const uint32_t offset = wavelength * 3u;
        for (uint32_t outChannel = 0; outChannel < 3u; ++outChannel) {
            for (uint32_t inChannel = 0; inChannel < 3u; ++inChannel) {
                matrix[outChannel * 3u + inChannel] +=
                    linearSensitivity[offset + outChannel] *
                    filmCurves.mallettBasisIlluminant[offset + inChannel];
            }
        }
    }
    const float normalization =
        std::max(filmCurves.mallettRawMidgrayGreen, 1.0e-10f);
    for (float& value : matrix) {
        value /= normalization;
    }
    return matrix;
}

// --- upstream: densityCurveMaximums (562) ---
std::array<float, 3> densityCurveMaximums(const ProfileCurveSet& curves) {
    std::array<float, 3> maximums = {0.0f, 0.0f, 0.0f};
    if (!curves.densityCurves || curves.exposureCount == 0u) {
        return maximums;
    }
    for (uint32_t index = 0; index < curves.exposureCount; ++index) {
        const float* density =
            curves.densityCurves + static_cast<size_t>(index) * 3u;
        maximums[0] = std::max(maximums[0], density[0]);
        maximums[1] = std::max(maximums[1], density[1]);
        maximums[2] = std::max(maximums[2], density[2]);
    }
    return maximums;
}

// --- upstream: makePackedCurveExposure (576) ---
std::vector<float> makePackedCurveExposure(const float* logExposure,
                                           uint32_t exposureCount) {
    std::vector<float> packed(static_cast<size_t>(exposureCount) * 2u, 0.0f);
    for (uint32_t index = 0u; index < exposureCount; ++index) {
        const size_t offset = static_cast<size_t>(index) * 2u;
        packed[offset] = logExposure[index];
        if (index + 1u < exposureCount) {
            packed[offset + 1u] =
                1.0f / std::max(logExposure[index + 1u] - logExposure[index],
                                1.0e-9f);
        }
    }
    return packed;
}

// --- upstream: makePackedSpectralDensity (588) ---
std::vector<float> makePackedSpectralDensity(const float* channelDensity,
                                             const float* baseDensity,
                                             uint32_t wavelengthCount) {
    std::vector<float> packed(static_cast<size_t>(wavelengthCount) * 4u, 0.0f);
    for (uint32_t wavelength = 0u; wavelength < wavelengthCount;
         ++wavelength) {
        const size_t sourceOffset = static_cast<size_t>(wavelength) * 3u;
        const size_t offset = static_cast<size_t>(wavelength) * 4u;
        packed[offset] = channelDensity[sourceOffset];
        packed[offset + 1u] = channelDensity[sourceOffset + 1u];
        packed[offset + 2u] = channelDensity[sourceOffset + 2u];
        packed[offset + 3u] = baseDensity[wavelength];
    }
    return packed;
}

// --- upstream: makeScanProducts (605) ---
std::vector<float> makeScanProducts(const float* filmIlluminant,
                                    const float* paperIlluminant,
                                    const float* filmBaseDensity,
                                    const float* paperBaseDensity,
                                    const float* cmfs, uint32_t wavelengthCount) {
    const size_t packedFloatCount = static_cast<size_t>(wavelengthCount) * 8u;
    const size_t legacyFloatCount = static_cast<size_t>(wavelengthCount) * 8u;
    std::vector<float> products(packedFloatCount + legacyFloatCount + 4u, 0.0f);
    const float* illuminants[] = {filmIlluminant, paperIlluminant};
    const float* baseDensities[] = {filmBaseDensity, paperBaseDensity};
    for (uint32_t stage = 0u; stage < 2u; ++stage) {
        float normalization = 0.0f;
        const size_t packedStageOffset =
            static_cast<size_t>(stage) * wavelengthCount * 4u;
        const size_t legacyStageOffset =
            packedFloatCount + static_cast<size_t>(stage) * wavelengthCount * 4u;
        for (uint32_t wavelength = 0; wavelength < wavelengthCount;
             ++wavelength) {
            const size_t channelOffset = static_cast<size_t>(wavelength) * 3u;
            const size_t packedOffset =
                packedStageOffset + static_cast<size_t>(wavelength) * 4u;
            const size_t legacyOffset =
                legacyStageOffset + static_cast<size_t>(wavelength) * 4u;
            const float illuminant = illuminants[stage][wavelength];
            const float baseTransmittanceRaw =
                std::pow(10.0f, -baseDensities[stage][wavelength]);
            const float baseTransmittance =
                std::isfinite(baseTransmittanceRaw) ? baseTransmittanceRaw : 0.0f;
            for (uint32_t channel = 0; channel < 3u; ++channel) {
                const float product = illuminant * cmfs[channelOffset + channel];
                products[packedOffset + channel] = baseTransmittance * product;
                products[legacyOffset + channel] = product;
            }
            normalization += illuminant * cmfs[channelOffset + 1u];
        }
        products[packedFloatCount + legacyFloatCount + stage] =
            1.0f / std::max(normalization, 1.0e-10f);
    }
    return products;
}

// --- upstream: makeHanatosRawResponse (642). method: 0=Hanatos2025
// (bandpass weights), 2=Hanatos2026 (adaptation window). Mallett (1) never
// calls this (shader takes the matrix path); kept explicit for review.
std::vector<float> makeHanatosRawResponse(
    const ProfileCurveSet& filmCurves,
    const std::vector<float>& linearSensitivity,
    const std::vector<float>& hanatosSpectra, const HanatosSpectraLutInfo& hanatos,
    int method, bool* missingBandpass) {
    const size_t responseCount = static_cast<size_t>(hanatos.width) *
                                 static_cast<size_t>(hanatos.height) * 3u;
    std::vector<float> response(responseCount, 0.0f);
    const size_t expectedSpectra =
        static_cast<size_t>(hanatos.width) * static_cast<size_t>(hanatos.height) *
        static_cast<size_t>(hanatos.wavelengthCount);
    if (hanatos.width == 0u || hanatos.height == 0u ||
        hanatos.wavelengthCount == 0u ||
        hanatosSpectra.size() < expectedSpectra ||
        linearSensitivity.size() < static_cast<size_t>(hanatos.wavelengthCount) * 3u) {
        return response;
    }
    const bool use2026 =
        method == 2 && filmCurves.hanatos2026WindowParams &&
        filmCurves.referenceIlluminantSpectrum;
    if (!use2026 && !filmCurves.bandpassHanatos2025) {
        if (missingBandpass) {
            *missingBandpass = true;
        }
        return response;
    }
    std::vector<float> window(hanatos.wavelengthCount, 1.0f);
    std::array<float, 3> normalization = {1.0f, 1.0f, 1.0f};
    if (use2026) {
        constexpr float kSqrt2 = 1.4142135623730951f;
        const float cUv = filmCurves.hanatos2026WindowParams[0];
        const float sigmaUv = filmCurves.hanatos2026WindowParams[1];
        const float cIr = filmCurves.hanatos2026WindowParams[2];
        const float sigmaIr = filmCurves.hanatos2026WindowParams[3];
        if (sigmaUv > 0.0f && sigmaIr > 0.0f) {
            std::array<float, 3> numerator = {0.0f, 0.0f, 0.0f};
            std::array<float, 3> denominator = {0.0f, 0.0f, 0.0f};
            for (uint32_t wavelength = 0; wavelength < hanatos.wavelengthCount;
                 ++wavelength) {
                const float wl = filmCurves.wavelengths
                                     ? filmCurves.wavelengths[wavelength]
                                     : 0.0f;
                const float edgeUv = smoothErfEdge(wl, cUv, sigmaUv * kSqrt2);
                const float edgeIr = smoothErfEdge(wl, cIr, -sigmaIr * kSqrt2);
                const float w = edgeUv * edgeIr;
                window[wavelength] = w;
                const float illuminant =
                    filmCurves.referenceIlluminantSpectrum[wavelength];
                const uint32_t sensitivityOffset = wavelength * 3u;
                for (uint32_t channel = 0; channel < 3u; ++channel) {
                    const float referenceResponse =
                        linearSensitivity[sensitivityOffset + channel] *
                        illuminant;
                    denominator[channel] += referenceResponse;
                    numerator[channel] += referenceResponse * w;
                }
            }
            for (uint32_t channel = 0; channel < 3u; ++channel) {
                normalization[channel] = numerator[channel] /
                                         std::max(denominator[channel], 1.0e-10f);
                normalization[channel] =
                    std::max(normalization[channel], 1.0e-10f);
            }
        }
    }
    for (uint32_t x = 0; x < hanatos.width; ++x) {
        for (uint32_t y = 0; y < hanatos.height; ++y) {
            std::array<float, 3> raw = {0.0f, 0.0f, 0.0f};
            const size_t spectraOffset =
                (static_cast<size_t>(x) * static_cast<size_t>(hanatos.height) +
                 static_cast<size_t>(y)) *
                static_cast<size_t>(hanatos.wavelengthCount);
            for (uint32_t wavelength = 0; wavelength < hanatos.wavelengthCount;
                 ++wavelength) {
                const float spectrum = hanatosSpectra[spectraOffset + wavelength];
                const uint32_t sensitivityOffset = wavelength * 3u;
                if (use2026) {
                    const float w = window[wavelength];
                    raw[0] += spectrum * linearSensitivity[sensitivityOffset] * w /
                              normalization[0];
                    raw[1] += spectrum * linearSensitivity[sensitivityOffset + 1u] *
                              w / normalization[1];
                    raw[2] += spectrum * linearSensitivity[sensitivityOffset + 2u] *
                              w / normalization[2];
                } else {
                    // Hanatos2025 bandpass weights (upstream 717-720).
                    raw[0] += spectrum * linearSensitivity[sensitivityOffset] *
                              filmCurves.bandpassHanatos2025[sensitivityOffset];
                    raw[1] += spectrum * linearSensitivity[sensitivityOffset + 1u] *
                              filmCurves.bandpassHanatos2025[sensitivityOffset + 1u];
                    raw[2] += spectrum * linearSensitivity[sensitivityOffset + 2u] *
                              filmCurves.bandpassHanatos2025[sensitivityOffset + 2u];
                }
            }
            const size_t responseOffset =
                (static_cast<size_t>(x) * static_cast<size_t>(hanatos.height) +
                 static_cast<size_t>(y)) *
                3u;
            response[responseOffset] = raw[0];
            response[responseOffset + 1u] = raw[1];
            response[responseOffset + 2u] = raw[2];
        }
    }
    return response;
}

// --- upstream: reinhardKnee (735) ---
float reinhardKnee(float value, float threshold, float limit, float power) {
    if (!std::isfinite(value) || value <= threshold) {
        return value;
    }
    const float scale = std::max(limit - threshold, 1.0e-12f);
    const float x = (value - threshold) / scale;
    const float y = x / std::pow(1.0f + std::pow(x, power), 1.0f / power);
    return threshold + scale * y;
}

// --- upstream: filmReferenceIlluminantXy (745) ---
std::array<float, 2> filmReferenceIlluminantXy(const ProfileCurveSet& filmCurves) {
    const float* cmfs = spektrafilm::standardObserverCmfs();
    if (!filmCurves.referenceIlluminantSpectrum || !cmfs ||
        filmCurves.wavelengthCount == 0u) {
        return {1.0f / 3.0f, 1.0f / 3.0f};
    }
    std::array<float, 3> xyz = {0.0f, 0.0f, 0.0f};
    for (uint32_t wavelength = 0; wavelength < filmCurves.wavelengthCount;
         ++wavelength) {
        const float illuminant = filmCurves.referenceIlluminantSpectrum[wavelength];
        const uint32_t offset = wavelength * 3u;
        xyz[0] += illuminant * cmfs[offset];
        xyz[1] += illuminant * cmfs[offset + 1u];
        xyz[2] += illuminant * cmfs[offset + 2u];
    }
    const float sum = xyz[0] + xyz[1] + xyz[2];
    if (!(sum > 1.0e-12f) || !std::isfinite(sum)) {
        return {1.0f / 3.0f, 1.0f / 3.0f};
    }
    return {xyz[0] / sum, xyz[1] / sum};
}

// --- upstream: spectralLocusXy (765) ---
std::vector<std::array<float, 2>> spectralLocusXy(
    const ProfileCurveSet& filmCurves) {
    std::vector<std::array<float, 2>> locus;
    const float* cmfs = spektrafilm::standardObserverCmfs();
    if (!filmCurves.wavelengths || !cmfs) {
        return locus;
    }
    for (uint32_t wavelength = 0; wavelength < filmCurves.wavelengthCount;
         ++wavelength) {
        if (filmCurves.wavelengths[wavelength] > 700.0f + 1.0e-4f) {
            continue;
        }
        const uint32_t offset = wavelength * 3u;
        const float x = cmfs[offset];
        const float y = cmfs[offset + 1u];
        const float z = cmfs[offset + 2u];
        const float sum = x + y + z;
        if (sum > 1.0e-12f && std::isfinite(sum)) {
            locus.push_back({x / sum, y / sum});
        }
    }
    if (!locus.empty()) {
        locus.push_back(locus.front());
    }
    return locus;
}

// --- upstream: rayPolygonDistance (790) ---
float rayPolygonDistance(std::array<float, 2> origin,
                         std::array<float, 2> direction,
                         const std::vector<std::array<float, 2>>& polygon) {
    float tMin = INFINITY;
    for (size_t edgeIndex = 0; edgeIndex + 1u < polygon.size(); ++edgeIndex) {
        const float ax = polygon[edgeIndex][0];
        const float ay = polygon[edgeIndex][1];
        const float ex = polygon[edgeIndex + 1u][0] - ax;
        const float ey = polygon[edgeIndex + 1u][1] - ay;
        const float denom = direction[0] * ey - direction[1] * ex;
        if (std::abs(denom) <= 1.0e-12f) {
            continue;
        }
        const float ox = origin[0] - ax;
        const float oy = origin[1] - ay;
        const float t = (-ox * ey + oy * ex) / denom;
        const float s = (-ox * direction[1] + oy * direction[0]) / denom;
        if (t > 1.0e-9f && s >= 0.0f && s <= 1.0f && t < tMin) {
            tMin = t;
        }
    }
    return tMin;
}

// --- upstream: compressXyRadial (816) ---
std::array<float, 2> compressXyRadial(
    std::array<float, 2> xy, std::array<float, 2> whiteXy,
    const std::vector<std::array<float, 2>>& locus) {
    const float dx = xy[0] - whiteXy[0];
    const float dy = xy[1] - whiteXy[1];
    const float distance = std::sqrt(dx * dx + dy * dy);
    if (!(distance > 1.0e-9f) || locus.size() < 4u) {
        return xy;
    }
    const std::array<float, 2> direction = {dx / distance, dy / distance};
    const float boundary = rayPolygonDistance(whiteXy, direction, locus);
    if (!(boundary > 1.0e-12f) || !std::isfinite(boundary)) {
        return xy;
    }
    const float normalized = distance / boundary;
    const float compressed = reinhardKnee(normalized, 0.0f, 1.0f, 6.0f);
    const float newDistance = compressed * boundary;
    return {whiteXy[0] + direction[0] * newDistance,
            whiteXy[1] + direction[1] * newDistance};
}

// --- upstream: sampleHanatosResponseBilinear (838) ---
std::array<float, 3> sampleHanatosResponseBilinear(
    const std::vector<float>& response, const HanatosSpectraLutInfo& hanatos,
    float x, float y) {
    x = std::clamp(x, 0.0f, static_cast<float>(hanatos.width - 1u));
    y = std::clamp(y, 0.0f, static_cast<float>(hanatos.height - 1u));
    const uint32_t x0 = static_cast<uint32_t>(std::floor(x));
    const uint32_t y0 = static_cast<uint32_t>(std::floor(y));
    const uint32_t x1 = std::min(x0 + 1u, hanatos.width - 1u);
    const uint32_t y1 = std::min(y0 + 1u, hanatos.height - 1u);
    const float tx = x - static_cast<float>(x0);
    const float ty = y - static_cast<float>(y0);
    const auto valueAt = [&](uint32_t xi, uint32_t yi) -> std::array<float, 3> {
        const size_t offset =
            (static_cast<size_t>(xi) * hanatos.height + yi) * 3u;
        return {response[offset], response[offset + 1u], response[offset + 2u]};
    };
    const std::array<float, 3> v00 = valueAt(x0, y0);
    const std::array<float, 3> v10 = valueAt(x1, y0);
    const std::array<float, 3> v01 = valueAt(x0, y1);
    const std::array<float, 3> v11 = valueAt(x1, y1);
    std::array<float, 3> out{};
    for (uint32_t channel = 0; channel < 3u; ++channel) {
        const float a = v00[channel] + (v10[channel] - v00[channel]) * tx;
        const float b = v01[channel] + (v11[channel] - v01[channel]) * tx;
        out[channel] = a + (b - a) * ty;
    }
    return out;
}

// --- upstream: remapHanatosResponseForInputGamutCompression (869) ---
std::vector<float> remapHanatosResponseForInputGamutCompression(
    const ProfileCurveSet& filmCurves, const std::vector<float>& baseResponse,
    const HanatosSpectraLutInfo& hanatos) {
    std::vector<float> remapped(baseResponse.size(), 0.0f);
    if (hanatos.width < 2u || hanatos.height < 2u ||
        baseResponse.size() <
            static_cast<size_t>(hanatos.width) * hanatos.height * 3u) {
        return remapped;
    }
    const std::array<float, 2> whiteXy = filmReferenceIlluminantXy(filmCurves);
    const std::vector<std::array<float, 2>> locus = spectralLocusXy(filmCurves);
    if (locus.size() < 4u) {
        return baseResponse;
    }
    for (uint32_t x = 0; x < hanatos.width; ++x) {
        const float tx = static_cast<float>(x) / static_cast<float>(hanatos.width - 1u);
        const float rootTx = std::sqrt(std::max(tx, 0.0f));
        for (uint32_t y = 0; y < hanatos.height; ++y) {
            const float ty =
                static_cast<float>(y) / static_cast<float>(hanatos.height - 1u);
            const std::array<float, 2> xy = {1.0f - rootTx, ty * rootTx};
            const std::array<float, 2> compressedXy =
                compressXyRadial(xy, whiteXy, locus);
            const float oneMinusX =
                std::max(1.0f - compressedXy[0], 1.0e-10f);
            const float sampleTx =
                std::clamp(oneMinusX * oneMinusX, 0.0f, 1.0f);
            const float sampleTy =
                std::clamp(compressedXy[1] / oneMinusX, 0.0f, 1.0f);
            const std::array<float, 3> sampled = sampleHanatosResponseBilinear(
                baseResponse, hanatos,
                sampleTx * static_cast<float>(hanatos.width - 1u),
                sampleTy * static_cast<float>(hanatos.height - 1u));
            const size_t offset =
                (static_cast<size_t>(x) * hanatos.height + y) * 3u;
            remapped[offset] = sampled[0];
            remapped[offset + 1u] = sampled[1];
            remapped[offset + 2u] = sampled[2];
        }
    }
    return remapped;
}

// Packs [response, compressed] as vec4-padded pairs (upstream 908).
std::vector<float> packHanatosPair(const std::vector<float>& response,
                                   const std::vector<float>& compressed) {
    std::vector<float> packed;
    packed.reserve((response.size() + compressed.size()) / 3u * 4u);
    const auto appendPacked = [&](const std::vector<float>& source) {
        for (size_t offset = 0u; offset + 2u < source.size(); offset += 3u) {
            packed.push_back(source[offset]);
            packed.push_back(source[offset + 1u]);
            packed.push_back(source[offset + 2u]);
            packed.push_back(0.0f);
        }
    };
    appendPacked(response);
    appendPacked(compressed);
    return packed;
}

// --- upstream: makeHanatosResponsePairFromWeights (932) ---
std::vector<float> makeHanatosResponsePairFromWeights(
    const ProfileCurveSet& referenceCurves,
    const std::vector<float>& spectralWeights,
    const std::vector<float>& hanatosSpectra,
    const HanatosSpectraLutInfo& hanatos) {
    const size_t responseCount = static_cast<size_t>(hanatos.width) *
                                 static_cast<size_t>(hanatos.height) * 3u;
    std::vector<float> response(responseCount, 0.0f);
    const size_t expectedSpectra =
        static_cast<size_t>(hanatos.width) * static_cast<size_t>(hanatos.height) *
        static_cast<size_t>(hanatos.wavelengthCount);
    if (hanatos.width == 0u || hanatos.height == 0u ||
        hanatos.wavelengthCount == 0u ||
        hanatosSpectra.size() < expectedSpectra ||
        spectralWeights.size() <
            static_cast<size_t>(hanatos.wavelengthCount) * 3u) {
        return response;
    }
    for (uint32_t x = 0; x < hanatos.width; ++x) {
        for (uint32_t y = 0; y < hanatos.height; ++y) {
            float raw[3] = {0.0f, 0.0f, 0.0f};
            const size_t spectraOffset =
                (static_cast<size_t>(x) * hanatos.height + y) *
                hanatos.wavelengthCount;
            for (uint32_t wavelength = 0; wavelength < hanatos.wavelengthCount;
                 ++wavelength) {
                const float spectrum = hanatosSpectra[spectraOffset + wavelength];
                const uint32_t weightOffset = wavelength * 3u;
                raw[0] += spectrum * spectralWeights[weightOffset];
                raw[1] += spectrum * spectralWeights[weightOffset + 1u];
                raw[2] += spectrum * spectralWeights[weightOffset + 2u];
            }
            const size_t responseOffset =
                (static_cast<size_t>(x) * hanatos.height + y) * 3u;
            response[responseOffset] = raw[0];
            response[responseOffset + 1u] = raw[1];
            response[responseOffset + 2u] = raw[2];
        }
    }
    std::vector<float> compressed =
        remapHanatosResponseForInputGamutCompression(referenceCurves, response,
                                                     hanatos);
    return packHanatosPair(response, compressed);
}

// --- upstream: filteredEnlargerIlluminantCpu (981); APD branch omitted ---
// Core-only spike forces FilteredEnlarger (APD data absent in this repo).
float filteredEnlargerIlluminantCpu(uint32_t wavelength, int32_t film,
                                    int32_t paper, float cFilter,
                                    float mFilterShift, float yFilterShift) {
    const uint32_t f = static_cast<uint32_t>(std::max(film, 0));
    const uint32_t p = static_cast<uint32_t>(std::max(paper, 0));
    const uint32_t neutralOffset =
        (p * spektrafilm::kSpektraFilmCount + f) * 3u;
    const float* neutral = spektrafilm::neutralPrintFilters();
    const float cc0 = std::max(neutral[neutralOffset] + cFilter, 0.0f);
    const float cc1 = std::max(neutral[neutralOffset + 1u] + mFilterShift, 0.0f);
    const float cc2 = std::max(neutral[neutralOffset + 2u] + yFilterShift, 0.0f);
    const float wheel0 = std::pow(10.0f, -cc0 / 100.0f);
    const float wheel1 = std::pow(10.0f, -cc1 / 100.0f);
    const float wheel2 = std::pow(10.0f, -cc2 / 100.0f);
    const uint32_t offset = wavelength * 3u;
    const float* filters = spektrafilm::customEnlargerFilters();
    const float f0 = std::clamp(filters[offset], 0.0f, 1.0f);
    const float f1 = std::clamp(filters[offset + 1u], 0.0f, 1.0f);
    const float f2 = std::clamp(filters[offset + 2u], 0.0f, 1.0f);
    const float dim0 = 1.0f - (1.0f - f0) * (1.0f - wheel0);
    const float dim1 = 1.0f - (1.0f - f1) * (1.0f - wheel1);
    const float dim2 = 1.0f - (1.0f - f2) * (1.0f - wheel2);
    return spektrafilm::thKg3Illuminant()[wavelength] * dim0 * dim1 * dim2;
}

// Paper weights, both branches (upstream makeProcessNegativePaperWeights
// 1015-1054). printTiming 0=FilteredEnlarger (c/m/y + TH-KG3 + custom +
// neutral), 1=APD (academy normalize, filtration ignored). preflash selects
// zero-C + preflash shifts + preflashExposure scale.
std::vector<float> makePaperWeights(
    const ProfileCurveSet& paperCurves,
    const std::vector<float>& paperSensitivityLinear, int32_t film,
    int32_t paper, int32_t printTiming, bool preflash, float preflashExposure,
    float cFilter, float mFilterShift, float yFilterShift,
    float preflashMShift, float preflashYShift) {
    std::vector<float> weights(
        static_cast<size_t>(paperCurves.wavelengthCount) * 3u, 0.0f);
    if (paperSensitivityLinear.size() < weights.size()) {
        return weights;
    }
    if (printTiming == 1) {
        const float* apd = spektrafilm::academyPrinterDensityData();
        if (!apd) {
            return weights;
        }
        float normalization[3] = {0.0f, 0.0f, 0.0f};
        for (uint32_t wavelength = 0; wavelength < paperCurves.wavelengthCount;
             ++wavelength) {
            const uint32_t offset = wavelength * 3u;
            normalization[0] += std::max(apd[offset], 0.0f);
            normalization[1] += std::max(apd[offset + 1u], 0.0f);
            normalization[2] += std::max(apd[offset + 2u], 0.0f);
        }
        const float preflashScale =
            preflash ? std::max(preflashExposure, 0.0f) : 1.0f;
        for (uint32_t wavelength = 0; wavelength < paperCurves.wavelengthCount;
             ++wavelength) {
            const uint32_t offset = wavelength * 3u;
            weights[offset] = preflashScale *
                              std::max(apd[offset], 0.0f) /
                              std::max(normalization[0], 1.0e-10f);
            weights[offset + 1u] = preflashScale *
                                   std::max(apd[offset + 1u], 0.0f) /
                                   std::max(normalization[1], 1.0e-10f);
            weights[offset + 2u] = preflashScale *
                                   std::max(apd[offset + 2u], 0.0f) /
                                   std::max(normalization[2], 1.0e-10f);
        }
        return weights;
    }
    const float c = preflash ? 0.0f : cFilter;
    const float m = preflash ? preflashMShift : mFilterShift;
    const float y = preflash ? preflashYShift : yFilterShift;
    const float preflashScale =
        preflash ? std::max(preflashExposure, 0.0f) : 1.0f;
    for (uint32_t wavelength = 0; wavelength < paperCurves.wavelengthCount;
         ++wavelength) {
        const uint32_t offset = wavelength * 3u;
        const float illuminant =
            filteredEnlargerIlluminantCpu(wavelength, film, paper, c, m, y);
        weights[offset] = preflashScale * illuminant * paperSensitivityLinear[offset];
        weights[offset + 1u] =
            preflashScale * illuminant * paperSensitivityLinear[offset + 1u];
        weights[offset + 2u] =
            preflashScale * illuminant * paperSensitivityLinear[offset + 2u];
    }
    return weights;
}

bool fail(const char* message, const char** reason) noexcept {
    if (reason) {
        *reason = message;
    }
    return false;
}

}  // namespace

// --- upstream: applyCameraBandPass (485) ---
std::vector<float> applyCameraBandPass(
    const spektrafilm::ProfileCurveSet& filmCurves,
    const std::vector<float>& linearSensitivity, const CameraFilters& camera) {
    std::vector<float> filtered = linearSensitivity;
    if (!filmCurves.wavelengths ||
        filtered.size() < static_cast<size_t>(filmCurves.wavelengthCount) * 3u ||
        (!camera.uvEnabled && !camera.irEnabled)) {
        return filtered;
    }
    constexpr float kUvTransitionNm = 8.0f;
    constexpr float kIrTransitionNm = 15.0f;
    std::array<float, 3> numerator = {0.0f, 0.0f, 0.0f};
    std::array<float, 3> denominator = {0.0f, 0.0f, 0.0f};
    std::vector<float> transmissionByWavelength(filmCurves.wavelengthCount, 1.0f);
    for (uint32_t wavelength = 0; wavelength < filmCurves.wavelengthCount;
         ++wavelength) {
        const float wl = filmCurves.wavelengths[wavelength];
        const float uvTransmission = camera.uvEnabled
                                         ? smoothErfEdge(wl, camera.uvCutNm,
                                                         kUvTransitionNm)
                                         : 1.0f;
        const float irTransmission = camera.irEnabled
                                         ? smoothErfEdge(wl, camera.irCutNm,
                                                         -kIrTransitionNm)
                                         : 1.0f;
        const float transmission = uvTransmission * irTransmission;
        transmissionByWavelength[wavelength] = transmission;
        const uint32_t offset = wavelength * 3u;
        const float illuminant = filmCurves.referenceIlluminantSpectrum
                                     ? filmCurves.referenceIlluminantSpectrum[wavelength]
                                     : 1.0f;
        for (uint32_t channel = 0; channel < 3u; ++channel) {
            const float response =
                linearSensitivity[offset + channel] * illuminant;
            denominator[channel] += response;
            numerator[channel] += response * transmission;
        }
    }
    std::array<float, 3> normalization = {1.0f, 1.0f, 1.0f};
    for (uint32_t channel = 0; channel < 3u; ++channel) {
        normalization[channel] =
            numerator[channel] / std::max(denominator[channel], 1.0e-10f);
        normalization[channel] = std::max(normalization[channel], 1.0e-10f);
    }
    for (uint32_t wavelength = 0; wavelength < filmCurves.wavelengthCount;
         ++wavelength) {
        const uint32_t offset = wavelength * 3u;
        for (uint32_t channel = 0; channel < 3u; ++channel) {
            filtered[offset + channel] *= transmissionByWavelength[wavelength] /
                                           normalization[channel];
        }
    }
    return filtered;
}

PaperWeights rebuildPaperWeights(
    const spektrafilm::ProfileCurveSet& filmCurves,
    const spektrafilm::ProfileCurveSet& paperCurves, int32_t filmIndex,
    int32_t paperIndex, const std::vector<float>& hanatosSpectra,
    const spektrafilm::HanatosSpectraLutInfo& hanatos) {
    PaperWeights weightsOut;
    const std::vector<float> paperLinear =
        makeLinearSensitivity(paperCurves.logSensitivity,
                              paperCurves.wavelengthCount);
    // Zero filtration: correct on the PrintSimulation path (frame constants
    // carry filtration there); ProcessNegative uses rebuildProcessNegativeWeights.
    const std::vector<float> weights = makePaperWeights(
        paperCurves, paperLinear, filmIndex, paperIndex, 0, false, 0.0f, 0.0f,
        0.0f, 0.0f, 0.0f, 0.0f);
    const std::vector<float> preflashWeights = makePaperWeights(
        paperCurves, paperLinear, filmIndex, paperIndex, 0, true, 0.0f, 0.0f,
        0.0f, 0.0f, 0.0f, 0.0f);
    weightsOut.paperHanatos =
        makeHanatosResponsePairFromWeights(filmCurves, weights, hanatosSpectra,
                                           hanatos);
    weightsOut.preflashHanatos = makeHanatosResponsePairFromWeights(
        filmCurves, preflashWeights, hanatosSpectra, hanatos);
    return weightsOut;
}

PaperWeights rebuildProcessNegativeWeights(
    const spektrafilm::ProfileCurveSet& filmCurves,
    const spektrafilm::ProfileCurveSet& paperCurves, int32_t filmIndex,
    int32_t paperIndex, int32_t printTiming, float filterC, float filterMShift,
    float filterYShift, float preflashExposure, float preflashMShift,
    float preflashYShift, const std::vector<float>& hanatosSpectra,
    const spektrafilm::HanatosSpectraLutInfo& hanatos) {
    PaperWeights weightsOut;
    const std::vector<float> paperLinear =
        makeLinearSensitivity(paperCurves.logSensitivity,
                              paperCurves.wavelengthCount);
    const std::vector<float> weights = makePaperWeights(
        paperCurves, paperLinear, filmIndex, paperIndex, printTiming, false,
        preflashExposure, filterC, filterMShift, filterYShift, preflashMShift,
        preflashYShift);
    const std::vector<float> preflashWeights = makePaperWeights(
        paperCurves, paperLinear, filmIndex, paperIndex, printTiming, true,
        preflashExposure, filterC, filterMShift, filterYShift, preflashMShift,
        preflashYShift);
    weightsOut.paperHanatos =
        makeHanatosResponsePairFromWeights(filmCurves, weights, hanatosSpectra,
                                           hanatos);
    weightsOut.preflashHanatos = makeHanatosResponsePairFromWeights(
        filmCurves, preflashWeights, hanatosSpectra, hanatos);
    return weightsOut;
}

// --- upstream: measureAutoExposureEv (2137) + autoExposureMeterY (2104) +
// autoExposurePreviewShape (2125), CPU port for Bayer snapshots. The reference
// meters GPU-decoded RGB; here each 2x2 quad yields one luminance sample
// (linearize + WB + Rec.709 luma, the meterMatrices fallback branch).
float autoExposureEvFromBayer(const BayerMeterInput& in,
                              int32_t method) noexcept {
    if (!in.pixels || in.width == 0u || in.height == 0u) {
        return 0.0f;
    }
    const uint32_t stride =
        in.rowStridePixels != 0u ? in.rowStridePixels : in.width;
    if (stride < in.width) {
        return 0.0f;
    }
    // Quad grid at ~256px long edge (reference kPreviewMaxSize on pixels;
    // quads sample the same scene fraction at half the count).
    const uint32_t quadW = (in.width + 1u) / 2u;
    const uint32_t quadH = (in.height + 1u) / 2u;
    const uint32_t longEdge = std::max(quadW, quadH);
    const uint32_t qstep = std::max(1u, (longEdge + 255u) / 256u);
    const float denomBase =
        std::max(in.whiteLevel - (in.blackRggb[0] + in.blackRggb[1] +
                                  in.blackRggb[2] + in.blackRggb[3]) * 0.25f,
                 1.0e-3f);
    std::vector<float> luminance;
    luminance.reserve(((quadW + qstep - 1u) / qstep) *
                      ((quadH + qstep - 1u) / qstep));
    const uint32_t pattern =
        static_cast<uint32_t>(std::clamp(in.cfaPattern, 0, 3));
    for (uint32_t qy = 0; qy < quadH; qy += qstep) {
        const uint32_t py = std::min(2u * qy, in.height - 2u);
        for (uint32_t qx = 0; qx < quadW; qx += qstep) {
            const uint32_t px = std::min(2u * qx, in.width - 2u);
            const size_t o = static_cast<size_t>(py) * stride + px;
            const float p00 = static_cast<float>(in.pixels[o]);
            const float p10 = static_cast<float>(in.pixels[o + 1u]);
            const float p01 = static_cast<float>(in.pixels[o + stride]);
            const float p11 =
                static_cast<float>(in.pixels[o + stride + 1u]);
            // Channel order per 2x2 origin for each CFA pattern.
            float r = 0.0f, g0 = 0.0f, g1 = 0.0f, b = 0.0f;
            switch (pattern) {
                case 0:  // RGGB
                    r = p00;
                    g0 = p10;
                    g1 = p01;
                    b = p11;
                    break;
                case 1:  // GRBG
                    g0 = p00;
                    r = p10;
                    b = p01;
                    g1 = p11;
                    break;
                case 2:  // GBRG
                    g0 = p00;
                    b = p10;
                    r = p01;
                    g1 = p11;
                    break;
                default:  // BGGR
                    b = p00;
                    g0 = p10;
                    g1 = p01;
                    r = p11;
                    break;
            }
            const float rl =
                std::max((r - in.blackRggb[0]) / denomBase, 0.0f) *
                in.wbRggb[0];
            const float gl = std::max(
                                 ((g0 - in.blackRggb[1]) +
                                  (g1 - in.blackRggb[2])) *
                                     0.5f / denomBase,
                                 0.0f) *
                             (in.wbRggb[1] + in.wbRggb[2]) * 0.5f;
            const float bl =
                std::max((b - in.blackRggb[3]) / denomBase, 0.0f) *
                in.wbRggb[3];
            luminance.push_back(0.2126f * rl + 0.7152f * gl + 0.0722f * bl);
        }
    }
    if (luminance.empty()) {
        return 0.0f;
    }
    double meteredY = 0.0;
    if (method == 1) {
        const size_t mid = luminance.size() / 2u;
        std::nth_element(luminance.begin(),
                         luminance.begin() + static_cast<std::ptrdiff_t>(mid),
                         luminance.end());
        meteredY = luminance[mid];
        if ((luminance.size() & 1u) == 0u) {
            const float upper = luminance[mid];
            std::nth_element(luminance.begin(),
                             luminance.begin() +
                                 static_cast<std::ptrdiff_t>(mid - 1u),
                             luminance.begin() +
                                 static_cast<std::ptrdiff_t>(mid));
            meteredY =
                0.5 * (static_cast<double>(luminance[mid - 1u]) + upper);
        }
    } else {
        const uint32_t gridW =
            (quadW + qstep - 1u) / qstep;
        const uint32_t gridH =
            (quadH + qstep - 1u) / qstep;
        const double normX =
            static_cast<double>(gridW) /
            static_cast<double>(std::max(gridW, gridH));
        const double normY =
            static_cast<double>(gridH) /
            static_cast<double>(std::max(gridW, gridH));
        constexpr double kSigma = 0.2;
        double weightedSum = 0.0;
        double weightSum = 0.0;
        size_t index = 0;
        for (uint32_t y = 0; y < gridH; ++y) {
            const double yf =
                (static_cast<double>(y) / static_cast<double>(gridH) - 0.5) *
                normY;
            for (uint32_t x = 0; x < gridW; ++x, ++index) {
                const double xf =
                    (static_cast<double>(x) / static_cast<double>(gridW) -
                     0.5) *
                    normX;
                const double weight =
                    std::exp(-(xf * xf + yf * yf) / (2.0 * kSigma * kSigma));
                weightedSum += static_cast<double>(luminance[index]) * weight;
                weightSum += weight;
            }
        }
        meteredY = weightedSum / std::max(weightSum, 1.0e-30);
    }
    const double exposure = meteredY / 0.184;
    if (!(exposure > 0.0) || !std::isfinite(exposure)) {
        return 0.0f;
    }
    const double ev = -std::log2(exposure);
    return std::isfinite(ev) ? static_cast<float>(ev) : 0.0f;
}

bool academyPrinterDensityAvailable() noexcept {
    if (!spektrafilm::kSpektraAcademyPrinterDensityEnabled) {
        return false;
    }
    const float* apd = spektrafilm::academyPrinterDensityData();
    if (!apd) {
        return false;
    }
    const size_t count =
        (static_cast<size_t>(spektrafilm::kSpektraFilmCount) *
             spektrafilm::kSpektraPaperCount +
         81u) *
        3u;
    double sum = 0.0;
    for (size_t i = 0; i < count; ++i) {
        const float v = apd[i];
        if (std::isfinite(v)) {
            sum += std::fabs(v);
        }
        if (sum > 1.0e-6) {
            return true;
        }
    }
    return false;
}

uint32_t colorAdaptationFlags(const FilmLook& look) noexcept {
    if (!look.colorAdaptation) {
        return 0u;
    }
    uint32_t flags = 0u;
    flags |= look.colorAdaptationInputCompression ? (1u << 0u) : 0u;
    flags |= look.colorAdaptationCurveSmoothing ? (1u << 1u) : 0u;
    flags |= look.colorAdaptationOutputLightnessCompression ? (1u << 2u) : 0u;
    flags |= look.colorAdaptationOutputChromaCompression ? (1u << 3u) : 0u;
    return flags;
}

FilmSpectral rebuildFilmSpectral(
    const spektrafilm::ProfileCurveSet& filmCurves, int32_t rgbToRawMethod,
    const CameraFilters& camera, const std::vector<float>& hanatosSpectra,
    const spektrafilm::HanatosSpectraLutInfo& hanatos, const char** reason) {
    FilmSpectral spectral;
    // Upstream requires adaptation data for Hanatos2026 (4040-4044); no
    // silent fallback to bandpass.
    if (rgbToRawMethod == 2 &&
        (!filmCurves.hanatos2026WindowParams ||
         !filmCurves.referenceIlluminantSpectrum || !filmCurves.wavelengths)) {
        if (reason) {
            *reason = "spektrafilm_native: film lacks Hanatos2026 data";
        }
        return spectral;
    }
    const std::vector<float> baseLinear =
        makeLinearSensitivity(filmCurves.logSensitivity,
                              filmCurves.wavelengthCount);
    const std::vector<float> filmLinear =
        applyCameraBandPass(filmCurves, baseLinear, camera);
    spectral.mallett = makeMallettRawMatrix(filmCurves, filmLinear);
    if (rgbToRawMethod == 1) {
        spectral.hanatosPair.assign(8u, 0.0f);
        return spectral;
    }
    bool missingBandpass = false;
    const std::vector<float> response = makeHanatosRawResponse(
        filmCurves, filmLinear, hanatosSpectra, hanatos, rgbToRawMethod,
        &missingBandpass);
    if (missingBandpass) {
        if (reason) {
            *reason = "spektrafilm_native: film lacks Hanatos2025 bandpass";
        }
        return spectral;
    }
    const std::vector<float> compressed =
        remapHanatosResponseForInputGamutCompression(filmCurves, response,
                                                     hanatos);
    spectral.hanatosPair = packHanatosPair(response, compressed);
    return spectral;
}

bool buildStaticTables(int32_t filmIndex, int32_t paperIndex, int32_t rgbToRawMethod,
                       const CameraFilters& camera,
                       const float* hanatosSpectra, size_t hanatosFloats,
                       const float* gamut, size_t gamutFloats,
                       StaticTables& out, const char** reason) noexcept {
    try {
        const spektrafilm::ProfileCurveSet* film =
            spektrafilm::filmProfileCurves(filmIndex);
        const spektrafilm::ProfileCurveSet* paper =
            spektrafilm::paperProfileCurves(paperIndex);
        if (!film) {
            return fail("spektrafilm: unknown film index", reason);
        }
        if (!paper) {
            return fail("spektrafilm: unknown paper index", reason);
        }
        if (film->wavelengthCount != paper->wavelengthCount) {
            return fail("spektrafilm: film/paper wavelength mismatch", reason);
        }
        if (!film->logExposure || !film->densityCurves || !film->logSensitivity ||
            !film->channelDensity || !film->baseDensity || !film->scanIlluminant ||
            !film->inputToReferenceXyz || !film->inputToSrgb ||
            !film->mallettBasisIlluminant || !film->wavelengths ||
            !film->referenceIlluminantSpectrum || !film->hanatos2026WindowParams ||
            !film->densityCurveMinimum) {
            return fail("spektrafilm: film profile missing tables", reason);
        }
        if (!paper->logExposure || !paper->densityCurves ||
            !paper->logSensitivity || !paper->channelDensity ||
            !paper->baseDensity || !paper->scanIlluminant ||
            !paper->scanToOutputRgb) {
            return fail("spektrafilm: paper profile missing tables", reason);
        }
        if (!spektrafilm::colorDecodeLuts() || !spektrafilm::colorEncodeLuts() ||
            !spektrafilm::colorTransferKinds() ||
            !spektrafilm::colorTransferParams() ||
            !spektrafilm::standardObserverCmfs() ||
            !spektrafilm::thKg3Illuminant() ||
            !spektrafilm::customEnlargerFilters() ||
            !spektrafilm::neutralPrintFilters() ||
            !spektrafilm::academyPrinterDensityData()) {
            return fail("spektrafilm: global color tables missing", reason);
        }
        const spektrafilm::HanatosSpectraLutInfo& hanatos =
            spektrafilm::hanatosSpectraLutInfo();
        const size_t expectedSpectra = static_cast<size_t>(hanatos.width) *
                                       hanatos.height *
                                       hanatos.wavelengthCount;
        if (!hanatosSpectra || hanatosFloats < expectedSpectra) {
            return fail("spektrafilm: Hanatos spectra asset short", reason);
        }
        constexpr size_t kGamutFloats =
            26u * 18u;  // kSpektraColorSpaceCount * stride
        if (!gamut || gamutFloats < kGamutFloats) {
            return fail("spektrafilm: gamut compression asset short", reason);
        }

        const std::vector<float> spectra(
            hanatosSpectra, hanatosSpectra + expectedSpectra);

        out.exposureCount = film->exposureCount;
        out.paperExposureCount = paper->exposureCount;
        out.wavelengthCount = film->wavelengthCount;
        out.filmPositive =
            (film->type && std::strcmp(film->type, "positive") == 0) ? 1u : 0u;

        out.packedFilmCurveExposure =
            makePackedCurveExposure(film->logExposure, film->exposureCount);
        const FilmSpectral filmSpectral = rebuildFilmSpectral(
            *film, rgbToRawMethod, camera, spectra, hanatos, reason);
        if (rgbToRawMethod != 1 && filmSpectral.hanatosPair.empty()) {
            return false;  // reason set by rebuildFilmSpectral
        }
        out.mallettRawMatrix.assign(filmSpectral.mallett.begin(),
                                    filmSpectral.mallett.end());
        out.hanatosRawResponse = filmSpectral.hanatosPair;

        const std::array<float, 3> filmMax = densityCurveMaximums(*film);
        const std::array<float, 3> paperMax = densityCurveMaximums(*paper);
        out.filmDensityMaximum.assign(filmMax.begin(), filmMax.end());
        out.paperDensityMaximum.assign(paperMax.begin(), paperMax.end());

        out.packedPaperCurveExposure =
            makePackedCurveExposure(paper->logExposure, paper->exposureCount);
        out.paperSensitivityLinear = makeLinearSensitivity(
            paper->logSensitivity, paper->wavelengthCount);
        out.packedFilmSpectralDensity = makePackedSpectralDensity(
            film->channelDensity, film->baseDensity, film->wavelengthCount);
        out.packedPaperSpectralDensity = makePackedSpectralDensity(
            paper->channelDensity, paper->baseDensity, paper->wavelengthCount);
        out.scanProducts = makeScanProducts(
            film->scanIlluminant, paper->scanIlluminant, film->baseDensity,
            paper->baseDensity, spektrafilm::standardObserverCmfs(),
            film->wavelengthCount);

        const PaperWeights paperWeights = rebuildPaperWeights(
            *film, *paper, filmIndex, paperIndex, spectra, hanatos);
        out.paperHanatosResponse = paperWeights.paperHanatos;
        out.preflashPaperHanatosResponse = paperWeights.preflashHanatos;

        out.colorEncodeAndGamut.reserve(
            static_cast<size_t>(spektrafilm::kSpektraColorSpaceCount) *
                spektrafilm::kSpektraColorTransferLutSize +
            kGamutFloats + spektrafilm::kSpektraColorSpaceCount);
        out.colorEncodeAndGamut.insert(
            out.colorEncodeAndGamut.end(), spektrafilm::colorEncodeLuts(),
            spektrafilm::colorEncodeLuts() +
                static_cast<size_t>(spektrafilm::kSpektraColorSpaceCount) *
                    spektrafilm::kSpektraColorTransferLutSize);
        out.colorEncodeAndGamut.insert(out.colorEncodeAndGamut.end(), gamut,
                                       gamut + kGamutFloats);
        out.colorEncodeAndGamut.insert(
            out.colorEncodeAndGamut.end(), spektrafilm::colorTransferParams(),
            spektrafilm::colorTransferParams() +
                spektrafilm::kSpektraColorSpaceCount);
        if (std::getenv("SPEKTRA_FILM_DEBUG_TABLES")) {
            const auto stats = [](const char* name, const std::vector<float>& v) {
                double sum = 0;
                float mn = 1e30f, mx = -1e30f;
                for (float x : v) {
                    sum += x;
                    mn = std::min(mn, x);
                    mx = std::max(mx, x);
                }
                std::fprintf(stderr,
                             "tables: %-24s n=%zu mean=%.6f min=%.6f max=%.6f\n",
                             name, v.size(), sum / std::max<size_t>(v.size(), 1),
                             mn, mx);
            };
            std::fprintf(stderr, "tables: exposureCount=%u paperExposureCount=%u wave=%u\n",
                         out.exposureCount, out.paperExposureCount,
                         out.wavelengthCount);
            stats("packedFilmExposure", out.packedFilmCurveExposure);
            stats("hanatosRawResponse", out.hanatosRawResponse);
            stats("scanProducts", out.scanProducts);
            stats("paperHanatos", out.paperHanatosResponse);
            stats("preflashHanatos", out.preflashPaperHanatosResponse);
            stats("filmSpectral", out.packedFilmSpectralDensity);
            stats("paperWeights?",
                  out.paperSensitivityLinear);
            stats("encodeAndGamut", out.colorEncodeAndGamut);
        }
        return true;
    } catch (...) {
        return fail("spektrafilm: table build threw", reason);
    }
}

float filmPushPullGamma(float stops) noexcept {
    const float clampedStops = std::clamp(stops, -2.0f, 2.0f);
    constexpr float kNormalSeconds = 180.0f;
    constexpr float kPull1Seconds = 150.0f;
    constexpr float kPush1Seconds = 220.0f;
    constexpr float kPush2Seconds = 280.0f;
    if (clampedStops < 0.0f) {
        return std::pow(kPull1Seconds / kNormalSeconds, -clampedStops);
    }
    if (clampedStops <= 1.0f) {
        return std::exp(std::log(kPush1Seconds / kNormalSeconds) * clampedStops);
    }
    const float segment = clampedStops - 1.0f;
    return std::exp(std::log(kPush1Seconds / kNormalSeconds) +
                    std::log(kPush2Seconds / kPush1Seconds) * segment);
}

float printPushPullGamma(float stops) noexcept {
    const float clampedStops = std::clamp(stops, -2.0f, 2.0f);
    return std::exp2(clampedStops * 0.25f);
}

float effectiveFilmGamma(int32_t pushPullMode, float filmGamma,
                         float pushPullStops) noexcept {
    // Upstream: experimental mode folds development change into the warped
    // lookup instead (5024); standard mode scales gamma.
    return filmGamma * (pushPullMode == 0 ? filmPushPullGamma(pushPullStops) : 1.0f);
}

float effectivePrintGamma(float printGamma, float pushPullStops) noexcept {
    return printGamma * printPushPullGamma(pushPullStops);
}

float filmFormatLongEdgeMm(int32_t format) noexcept {
    switch (format) {
        case 0:
            return 4.8f;  // Standard8
        case 1:
            return 5.79f;  // Super8
        case 2:
            return 10.26f;  // Standard16
        case 3:
            return 12.52f;  // Super16
        case 5:
            return 24.89f;  // Super35
        case 6:
            return 52.48f;  // Standard65
        case 7:
            return 70.41f;  // Imax70
        case 4:
        default:
            return 35.0f;  // Standard35
    }
}

void fillFrameArrays(const spektrafilm::ProfileCurveSet& filmCurves,
                     const StaticTables& tables, const FilmLook& look,
                     int32_t filmIndex, int32_t paperIndex, float pixelSizeUm,
                     float longEdgeMm, double time,
                     float* frameFloats105,
                     uint32_t* frameInts29) noexcept {
    (void)time;
    std::fill(frameFloats105, frameFloats105 + 105, 0.0f);
    std::fill(frameInts29, frameInts29 + 29, 0u);
    // Upstream renderCoreBootstrap frame fill (5244+), core-read subset.
    frameFloats105[0] = look.filterC;
    frameFloats105[1] = look.filterMShift;
    frameFloats105[2] = look.filterYShift;
    frameFloats105[3] = look.printExposureEv;
    frameFloats105[4] =
        effectivePrintGamma(look.printGamma, look.printPushPullStops);
    frameFloats105[5] = look.printShadowShape;
    frameFloats105[6] = look.printHighlightShape;
    frameFloats105[7] = look.negativeBleachBypassAmount;
    frameFloats105[8] = look.negativeLeucoCyanCoupling;
    frameFloats105[9] = look.printBleachBypassAmount;
    frameFloats105[10] = look.preflashExposure;
    frameFloats105[11] = look.preflashMFilterShift;
    frameFloats105[12] = look.preflashYFilterShift;
    frameFloats105[13] = look.printerLightsR;
    frameFloats105[14] = look.printerLightsG;
    frameFloats105[15] = look.printerLightsB;
    frameFloats105[16] = tables.filmDensityMaximum[0];
    frameFloats105[17] = tables.filmDensityMaximum[1];
    frameFloats105[18] = tables.filmDensityMaximum[2];
    frameFloats105[19] = tables.paperDensityMaximum[0];
    frameFloats105[20] = tables.paperDensityMaximum[1];
    frameFloats105[21] = tables.paperDensityMaximum[2];
    frameFloats105[22] = spektrafilm::colorEncodeLutMin();
    frameFloats105[23] = spektrafilm::colorEncodeLutMax();
    // [24,25] scanner levels: defaults (scanner off, unread).
    frameFloats105[24] = 0.98f;
    frameFloats105[25] = 0.01f;
    if (filmCurves.densityCurveMinimum) {
        frameFloats105[26] = filmCurves.densityCurveMinimum[0];
        frameFloats105[27] = filmCurves.densityCurveMinimum[1];
        frameFloats105[28] = filmCurves.densityCurveMinimum[2];
    }
    // [29..39] halation (upstream 5286+): stock preset wins when the user
    // leaves strengths at the OFX default; first-sigma is always
    // stock-driven when the profile carries it.
    float halationStrengthR = look.halationStrengthR;
    float halationStrengthG = look.halationStrengthG;
    float halationStrengthB = look.halationStrengthB;
    if (filmCurves.halationStrength &&
        std::fabs(look.halationStrengthR - 0.05f) <= 1.0e-6f &&
        std::fabs(look.halationStrengthG - 0.015f) <= 1.0e-6f &&
        std::fabs(look.halationStrengthB) <= 1.0e-6f) {
        halationStrengthR = filmCurves.halationStrength[0];
        halationStrengthG = filmCurves.halationStrength[1];
        halationStrengthB = filmCurves.halationStrength[2];
    }
    float halationFirstSigmaR = look.halationFirstSigmaUmR;
    float halationFirstSigmaG = look.halationFirstSigmaUmG;
    float halationFirstSigmaB = look.halationFirstSigmaUmB;
    if (filmCurves.halationFirstSigmaUm) {
        halationFirstSigmaR = filmCurves.halationFirstSigmaUm[0];
        halationFirstSigmaG = filmCurves.halationFirstSigmaUm[1];
        halationFirstSigmaB = filmCurves.halationFirstSigmaUm[2];
    }
    frameFloats105[29] = pixelSizeUm;
    frameFloats105[30] = look.scatterAmount;
    frameFloats105[31] = look.scatterScale;
    frameFloats105[32] = look.halationAmount;
    frameFloats105[33] = look.halationScale;
    frameFloats105[34] = halationStrengthR;
    frameFloats105[35] = halationStrengthG;
    frameFloats105[36] = halationStrengthB;
    frameFloats105[37] = halationFirstSigmaR;
    frameFloats105[38] = halationFirstSigmaG;
    frameFloats105[39] = halationFirstSigmaB;
    // [40..48] scanner: record() overwrites when the scanner pass runs.
    // Grain preview block [49..67] (upstream 5323+).
    frameFloats105[49] = look.grainAmount;
    frameFloats105[50] = look.grainSaturation;
    frameFloats105[51] = look.grainParticleAreaUm2;
    frameFloats105[52] = look.grainParticleScaleR;
    frameFloats105[53] = look.grainParticleScaleG;
    frameFloats105[54] = look.grainParticleScaleB;
    frameFloats105[55] = look.grainParticleScaleLayer0;
    frameFloats105[56] = look.grainParticleScaleLayer1;
    frameFloats105[57] = look.grainParticleScaleLayer2;
    frameFloats105[58] = look.grainDensityMinR;
    frameFloats105[59] = look.grainDensityMinG;
    frameFloats105[60] = look.grainDensityMinB;
    frameFloats105[61] = look.grainUniformityR;
    frameFloats105[62] = look.grainUniformityG;
    frameFloats105[63] = look.grainUniformityB;
    {
        const float grainFormatScale = std::pow(
            std::max(longEdgeMm / 35.0f, 1.0e-6f), 0.62f);
        frameFloats105[64] = std::max(look.grainFinalBlurUm, 0.0f) *
                              grainFormatScale /
                              std::max(pixelSizeUm, 1.0e-6f);
    }
    frameFloats105[65] = look.grainBlurDyeCloudsUm;
    frameFloats105[66] = look.grainMicroStructureScale;
    frameFloats105[67] = look.grainMicroStructureSigmaNm * 0.001f /
                          std::max(pixelSizeUm, 1.0e-6f);
    frameFloats105[68] = std::clamp(look.enlargerScale, 1.0f, 32.0f);
    frameFloats105[69] = look.enlargerOffsetXPercent;
    frameFloats105[70] = look.enlargerOffsetYPercent;
    frameFloats105[71] = pixelSizeUm;
    // [72..74] HDR (upstream 5351-5353).
    frameFloats105[72] = look.hdrReferenceWhiteNits;
    frameFloats105[73] = look.hdrPeakNits;
    frameFloats105[74] = look.hdrExposureEv;
    // [75..77] halation boost (upstream 5354+).
    frameFloats105[75] = look.halationBoostEv;
    frameFloats105[76] = look.halationBoostRange;
    frameFloats105[77] = look.halationProtectEv;
    // [78..90] synthesis (upstream 5357-5372).
    {
        const float quality =
            std::clamp(look.grainSynthesisQuality, 0.25f, 4.0f);
        const float size =
            std::clamp(look.grainSynthesisSize, 0.25f, 4.0f);
        const float sharpness =
            std::max(look.grainSynthesisSharpness, 0.25f);
        frameFloats105[78] =
            std::clamp(look.grainSynthesisAmount, 0.0f, 3.0f);
        frameFloats105[79] = look.grainSynthesisMeanRadiusUm * size;
        frameFloats105[80] = look.grainSynthesisRadiusStdDevRatio;
        frameFloats105[81] =
            look.grainSynthesisObservationSigmaUm / sharpness;
        frameFloats105[82] = look.grainSynthesisCellSizeRatio;
        frameFloats105[83] = look.grainSynthesisMaxRadiusQuantile;
        frameFloats105[84] = look.grainSynthesisCoverageEpsilon;
        frameFloats105[85] = look.grainSynthesisRadiusScaleR;
        frameFloats105[86] = look.grainSynthesisRadiusScaleG;
        frameFloats105[87] = look.grainSynthesisRadiusScaleB;
        frameFloats105[88] = look.grainSynthesisLayerScale0;
        frameFloats105[89] = look.grainSynthesisLayerScale1;
        frameFloats105[90] = look.grainSynthesisLayerScale2;
        (void)quality;
    }
    // [91..104] GPU-written by PrintScan op3.

    frameInts29[0] = static_cast<uint32_t>(look.process);
    // [1] outputColorSpace, [2] outputRole set by the caller.
    frameInts29[3] = static_cast<uint32_t>(std::clamp(look.printTiming, 0, 1));
    frameInts29[4] = static_cast<uint32_t>(std::max(filmIndex, 0));
    frameInts29[5] = static_cast<uint32_t>(std::max(paperIndex, 0));
    frameInts29[6] = spektrafilm::kSpektraFilmCount;
    frameInts29[7] = spektrafilm::kSpektraPaperCount;
    frameInts29[8] = tables.wavelengthCount;
    frameInts29[9] = tables.paperExposureCount;
    frameInts29[10] = tables.filmPositive;
    frameInts29[11] = look.printerLightsGang ? 1u : 0u;
    frameInts29[12] = look.printerLightCalibration ? 1u : 0u;
    // [13..15] scanner off.
    frameInts29[16] = look.grainSeed;
    frameInts29[17] = look.grainSublayersEnabled ? 1u : 0u;
    frameInts29[18] = static_cast<uint32_t>(std::max(look.grainSubLayerCount, 1));
    frameInts29[19] =
        look.grainAnimate
            ? static_cast<uint32_t>(
                  std::max(0.0, std::floor(time * 24.0 + 0.5)))
            : 0u;
    // [20,21] HDR transfer/mapping. [22..26] synthesis (upstream 5399-5407).
    frameInts29[20] = static_cast<uint32_t>(std::clamp(look.hdrTransfer, 0, 1));
    frameInts29[21] =
        static_cast<uint32_t>(std::clamp(look.hdrToneMapping, 0, 1));
    {
        const float quality =
            std::clamp(look.grainSynthesisQuality, 0.25f, 4.0f);
        const long samples = std::lround(static_cast<double>(look.grainSynthesisSamples) *
                                         static_cast<double>(quality));
        frameInts29[22] = static_cast<uint32_t>(
            std::clamp(samples, 1L, 1024L));
        frameInts29[23] = static_cast<uint32_t>(
            std::clamp(look.grainSynthesisMaxGrainsPerCell, 1, 128));
        frameInts29[24] = look.grainSynthesisLayered ? 1u : 0u;
        frameInts29[25] =
            (look.grainEnabled && look.grainModel == 2) ? 1u : 0u;
    }
    frameInts29[26] = 1u;  // grain blur recurrence (upstream default true)
    frameInts29[27] = colorAdaptationFlags(look);
    frameInts29[28] = look.scanNegativeInvert ? 1u : 0u;
}

}  // namespace spektrafilm_native::tables
