#pragma once
// CPU-side table builders for the core-only path. Logic ports the upstream
// helpers in spektrafilm-ofx/src/SpektraVulkanRenderer.cpp verbatim (see
// NOTICE.md); only namespacing and error reporting are adapted.
#include <array>
#include <cstdint>
#include <vector>

namespace spektrafilm {
struct ProfileCurveSet;
struct HanatosSpectraLutInfo;
}  // namespace spektrafilm

namespace spektrafilm_native {
struct FilmLook;
struct CameraFilters;
}  // namespace spektrafilm_native

namespace spektrafilm_native::tables {

// All blobs needed by the Exposure / CurveDevelop / PrintScan core passes,
// for one fixed film+paper pair. Vectors own their storage; raw pointers
// below alias either owned storage or the linked generated tables.
struct StaticTables {
    // Film.
    std::vector<float> packedFilmCurveExposure;  // exposureCount*2
    std::vector<float> mallettRawMatrix;         // 9
    std::vector<float> hanatosRawResponse;       // W*H*8
    std::vector<float> packedFilmSpectralDensity;  // wave*4
    std::vector<float> filmDensityMaximum;         // 3
    // Paper.
    std::vector<float> packedPaperCurveExposure;  // pexp*2
    std::vector<float> paperSensitivityLinear;    // wave*3
    std::vector<float> packedPaperSpectralDensity;  // wave*4
    std::vector<float> scanProducts;                // wave*16+4
    std::vector<float> paperDensityMaximum;         // 3
    std::vector<float> paperHanatosResponse;        // W*H*8
    std::vector<float> preflashPaperHanatosResponse;  // W*H*8
    // Global encode pack: [encodeLuts 26*4096][gamut 26*18][params 26].
    std::vector<float> colorEncodeAndGamut;

    uint32_t exposureCount = 0;
    uint32_t paperExposureCount = 0;
    uint32_t wavelengthCount = 0;
    uint32_t filmPositive = 0;
};

// Builds every computed blob for (filmIndex, paperIndex). hanatosSpectra is
// the raw .f32 asset (W*H*waveLength floats); gamut is the 26*18-float asset.
// Returns false + reason on missing data.
bool buildStaticTables(int32_t filmIndex, int32_t paperIndex, int32_t rgbToRawMethod,
                       const CameraFilters& camera,
                       const float* hanatosSpectra, size_t hanatosFloats,
                       const float* gamut, size_t gamutFloats,
                       StaticTables& out, const char** reason) noexcept;

// Camera band-pass on linear sensitivity (upstream applyCameraBandPass).
// Returns the input unchanged when both filters are off.
std::vector<float> applyCameraBandPass(const spektrafilm::ProfileCurveSet& filmCurves,
                                       const std::vector<float>& linearSensitivity,
                                       const CameraFilters& camera);

// Paper Hanatos pairs. FilteredEnlarger weights consume live filtration
// (frame constants carry it on PrintSimulation, but ProcessNegative reads
// these tables, so they must be rebuilt when filtration/timing change).
// APD weights normalize academyPrinterDensityData; returns zero weights when
// academy data is absent (caller must reject printTiming==1 then).
struct PaperWeights {
    std::vector<float> paperHanatos;
    std::vector<float> preflashHanatos;
};
PaperWeights rebuildPaperWeights(
    const spektrafilm::ProfileCurveSet& filmCurves,
    const spektrafilm::ProfileCurveSet& paperCurves, int32_t filmIndex,
    int32_t paperIndex, const std::vector<float>& hanatosSpectra,
    const spektrafilm::HanatosSpectraLutInfo& hanatos);
// Live-filtration variant for ProcessNegative (upstream
// makeProcessNegativePaperWeights 1015-1054): printTiming 0=FilteredEnlarger
// (c/m/y shifts), 1=APD (academy normalize, filtration ignored).
PaperWeights rebuildProcessNegativeWeights(
    const spektrafilm::ProfileCurveSet& filmCurves,
    const spektrafilm::ProfileCurveSet& paperCurves, int32_t filmIndex,
    int32_t paperIndex, int32_t printTiming, float filterC, float filterMShift,
    float filterYShift, float preflashExposure, float preflashMShift,
    float preflashYShift, const std::vector<float>& hanatosSpectra,
    const spektrafilm::HanatosSpectraLutInfo& hanatos);
// True when generated academy tables carry real (non-zero) data.
bool academyPrinterDensityAvailable() noexcept;
// Upstream colorAdaptationFlags (SpektraParameters.h:306-316).
uint32_t colorAdaptationFlags(const FilmLook& look) noexcept;

// Rebuilds film spectral data for live camera filtration.
struct FilmSpectral {
    std::vector<float> hanatosPair;  // empty (stub) for Mallett
    std::array<float, 9> mallett;
};
FilmSpectral rebuildFilmSpectral(const spektrafilm::ProfileCurveSet& filmCurves,
                                 int32_t rgbToRawMethod,
                                 const CameraFilters& camera,
                                 const std::vector<float>& hanatosSpectra,
                                 const spektrafilm::HanatosSpectraLutInfo& hanatos,
                                 const char** reason);

// Auto-exposure metering lives in the public SpektraFilm.h
// (spektrafilm_native::tables::BayerMeterInput/autoExposureEvFromBayer) so
// app code can meter without reaching into src/.

// Development gamma helpers (upstream filmPushPullGamma/printPushPullGamma).
float filmPushPullGamma(float stops) noexcept;
float printPushPullGamma(float stops) noexcept;
float effectiveFilmGamma(int32_t pushPullMode, float filmGamma,
                         float pushPullStops) noexcept;
float effectivePrintGamma(float printGamma, float pushPullStops) noexcept;

// Fills the 105-float / 29-uint frame arrays for one frame from a validated
// look. Indices follow upstream renderCoreBootstrap frame fill (5244+):
// synthesis [78..90]/[22..26] (5357-5407), HDR [72..74]/[1,2,20,21] + adapt
// [27] (5351-5353,5375-5408), APD timing [3]. time drives grain animation.
float filmFormatLongEdgeMm(int32_t format) noexcept;

void fillFrameArrays(const spektrafilm::ProfileCurveSet& filmCurves,
                     const StaticTables& tables, const FilmLook& look,
                     int32_t filmIndex, int32_t paperIndex, float pixelSizeUm,
                     float longEdgeMm, double time,
                     float* frameFloats105,
                     uint32_t* frameInts29) noexcept;

}  // namespace spektrafilm_native::tables
