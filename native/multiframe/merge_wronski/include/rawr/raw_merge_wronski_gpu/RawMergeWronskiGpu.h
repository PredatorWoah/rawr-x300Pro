#pragma once
#include <array>
#include <cmath>
#include <cstdint>
#include <optional>
#include <vector>
namespace rawr::raw_merge_wronski_gpu {
enum class CfaPattern : std::uint32_t { RGGB = 0, GRBG = 1, GBRG = 2, BGGR = 3 };
// Camera2 SENSOR_NOISE_PROFILE pairs in row-major 2x2 CFA-site order.
// Variance in normalized RAW space is slope * signal + offset.
struct CfaNoiseProfile {
    std::array<float, 4> slopeBySite{};
    std::array<float, 4> offsetBySite{};
};
struct Config {
    CfaPattern cfa = CfaPattern::RGGB;
    float noiseAlpha = 1.80710882e-4f, noiseBeta = 3.1937599182128e-6f;
    std::optional<CfaNoiseProfile> sensorNoiseProfile{};
    std::optional<CfaNoiseProfile> highSignalNoiseProfile{};
    std::array<float, 4> noiseKneeBySite{};  // 0 selects sensorNoiseProfile over the full range.
    float kDetail = .20f, kDenoise = 3.f, dThreshold = .71f, dTransition = 1.f, kStretch = 4.f, kShrink = 2.f;
    // Detail/denoise decoupling + coverage-aware bandwidth.
    // flatSigma < 0 selects legacy kDetail*kDenoise so existing oracles are
    // bit-identical by default. >= 0 sets the flat-region kernel sigma in
    // input pixels independently of kDetail.
    float flatSigma = -1.f;
    // Minimum edge kernel sigma in input pixels (applied to k1/k2 after
    // stretch/shrink blending). 0 preserves legacy behavior, including the
    // extremely narrow sigma=kDetail/kShrink limit.
    float detailFloorSigma = 0.f;
    // Coverage fallback gates (previously hardcoded in the shader).
    float coverageNeffLo = 1.f, coverageNeffHi = 3.f;
    float coverageMassLo = .001f, coverageMassHi = .03f;
    // Scale-aware bandwidth: when > 0, the detail floor grows with output
    // scale (floor * (1 + gain*(scale-1))) so 2x queries between sparse CFA
    // samples cannot use the same narrow kernel as 1x unless coverage
    // supports it. 0 preserves legacy scale-agnostic kernels.
    float scaleBandwidthGain = 0.f;
    float robustnessT = .09f, robustnessS1 = 1.5f, robustnessS2 = 9.f, motionThreshold = .65f;
    float affineDeadzoneSigma = 2.f, affineSoftnessSigma = 1.f;
    // Judge a tile's deviation from the global affine motion only along the
    // directions its reference gradients can resolve (residual^T A residual /
    // lambda_max). Edge-only tiles (long slats, stripes) have unconstrained
    // motion along the edge; counting it as subject motion gated out valid
    // companions there. false = legacy isotropic residual.
    bool affineApertureAware = true;
    // Replace the configured noise profile, per run, with one fitted from the
    // burst itself (reference vs neighbouring frame, flat static blocks).
    // Falls back to the configured profile when the fit is not usable.
    bool estimateNoiseFromBurst = false;
    // Support-aware chroma smoothing at finalize (fallback_chroma.comp):
    // pixels merged from fewer effective frames than the burst get luma-guided
    // chroma averaging so motion/fallback areas do not keep single-frame
    // chroma noise. 0 disables (plain a11 finalize); sigma capped in pixels.
    float fallbackChromaGain = 0.f;
    float fallbackChromaMaxSigma = 8.f;
    // Same support-aware smoothing for luma (0 keeps merged luma untouched).
    float fallbackLumaGain = 0.f;
    // Sensor-reported hot/defective pixels, flat [x0, y0, x1, y1, ...] in RAW
    // pixels (Camera2 STATISTICS_HOT_PIXEL_MAP). Concealed in every frame
    // right after normalization; empty disables the pass.
    std::vector<std::int32_t> hotPixels{};
    float normalizedSupportThreshold = .995f;
    bool noiseDomainClamp = true;
    float outputScale = 1.f;
    std::uint32_t workgroupX = 16, workgroupY = 16;
};
inline bool valid(const CfaNoiseProfile& n) {
    for (std::size_t i = 0; i < 4; ++i)
        if (!(std::isfinite(n.slopeBySite[i]) && n.slopeBySite[i] > 0.0f && std::isfinite(n.offsetBySite[i]) &&
              n.offsetBySite[i] >= 0.0f))
            return false;
    return true;
}
inline bool valid(const Config& c) {
    if (!(c.noiseAlpha > 0 && (!c.sensorNoiseProfile || valid(*c.sensorNoiseProfile)) &&
          (!c.highSignalNoiseProfile || c.sensorNoiseProfile) &&
          (!c.highSignalNoiseProfile || valid(*c.highSignalNoiseProfile))))
        return false;
    for (float knee : c.noiseKneeBySite)
        if (!(std::isfinite(knee) && knee >= 0.0f && knee < 1.0f)) return false;
    if (!(std::isfinite(c.flatSigma) && std::isfinite(c.detailFloorSigma) && c.detailFloorSigma >= 0.f &&
          std::isfinite(c.coverageNeffLo) && std::isfinite(c.coverageNeffHi) && c.coverageNeffLo >= 0.f &&
          c.coverageNeffHi >= c.coverageNeffLo && std::isfinite(c.coverageMassLo) &&
          std::isfinite(c.coverageMassHi) && c.coverageMassLo >= 0.f && c.coverageMassHi >= c.coverageMassLo &&
          std::isfinite(c.scaleBandwidthGain) && c.scaleBandwidthGain >= 0.f))
        return false;
    return c.dTransition > 0 && c.kShrink > 0 && c.normalizedSupportThreshold >= 0 &&
           c.normalizedSupportThreshold <= 1 && c.outputScale >= 1 && c.outputScale <= 2 && c.workgroupX &&
           c.workgroupY;
}
}  // namespace rawr::raw_merge_wronski_gpu
