#include <algorithm>
#include <array>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <random>
#include <vector>

#include "monitoring_overlays/cpu_reference.h"

using namespace monitoring_overlays;
using namespace monitoring_overlays::cpu_reference;

static void require(bool v, const char* msg) {
    if (!v) {
        std::cerr << "FAIL " << msg << "\n";
        std::exit(2);
    }
}
static bool near(float a, float b, float e = 1e-5f) { return std::fabs(a - b) <= e; }

static void testRawState() {
    require(logicalSeverityFromPhysicalNibble(0, 0) == 0, "raw empty");
    require(logicalSeverityFromPhysicalNibble(1u << 0, 0) == 1, "R severity");
    require(logicalSeverityFromPhysicalNibble(1u << 1, 0) == 1, "G1 severity");
    require(logicalSeverityFromPhysicalNibble(1u << 2, 0) == 1, "G2 severity");
    require(logicalSeverityFromPhysicalNibble((1u << 1) | (1u << 2), 0) == 1, "G1+G2 one logical G");
    require(logicalSeverityFromPhysicalNibble((1u << 0) | (1u << 1), 0) == 2, "R+G severity");
    require(logicalSeverityFromPhysicalNibble((1u << 0) | (1u << 3), 0) == 2, "R+B severity");
    require(logicalSeverityFromPhysicalNibble((1u << 0) | (1u << 1) | (1u << 2) | (1u << 3), 0) == 3, "RGB severity");
    require(logicalSeverityFromPhysicalNibble(uint16_t((1u << 8) | (1u << 9) | (1u << 10)), 8) == 2,
            "shadow R+G severity");
    require(logicalSeverityFromPhysicalNibble(uint16_t((1u << 13) | (1u << 14)), 12) == 1, "black G1+G2 severity");

    RawStateOverlayParams p{};
    auto h = rawStateOverlay(uint16_t(1u << 0), p);
    auto sc = rawStateOverlay(uint16_t(1u << 12), p);
    auto sw = rawStateOverlay(uint16_t(1u << 8), p);
    require(h.a > sc.a && sc.a > sw.a, "default raw priorities/colors reachable");
    auto both = rawStateOverlay(uint16_t((1u << 0) | (1u << 12) | (1u << 8)), p);
    require(near(both.r, h.r) && near(both.g, h.g) && near(both.b, h.b) && near(both.a, h.a),
            "highlight deterministic priority");
    std::cout << "RAW_STATE_CPU_PASS\n";
}

static std::array<float, 9> edge(float blur) {
    // Analytic vertical edge sampled at x=-1,0,1 after Gaussian-like logistic smoothing.
    std::array<float, 9> n{};
    const float k = 8.0f / (1.0f + blur * 5.0f);
    for (int y = 0; y < 3; y++)
        for (int x = 0; x < 3; x++) {
            const float xx = float(x - 1);
            n[size_t(y * 3 + x)] = 1.0f / (1.0f + std::exp(-k * (xx + 0.25f)));
        }
    return n;
}

static void testFocus() {
    FocusPeakingParams p{};
    std::array<float, 9> flat{};
    flat.fill(0.4f);
    require(focusScore3x3(flat, p) < 1e-6f, "flat rejection");
    const float s0 = focusScore3x3(edge(0.0f), p);
    const float s1 = focusScore3x3(edge(0.5f), p);
    const float s2 = focusScore3x3(edge(1.0f), p);
    const float s4 = focusScore3x3(edge(2.0f), p);
    require(s0 > s1 && s1 > s2 && s2 > s4, "blur ordering");
    // Display-referred sharp edge must comfortably exceed the default threshold.
    require(s0 > 1.0f, "sharp display edge strength");
    require(focusAlphaFromScore(s0, p) > 0.9f, "sharp edge opaque");

    std::mt19937 rng(1);
    std::normal_distribution<float> noise(0.0f, 0.004f);
    int falsePeaks = 0;
    for (int k = 0; k < 1000; k++) {
        std::array<float, 9> n{};
        for (float& v : n) v = std::max(0.0f, 0.02f + noise(rng));
        if (focusAlphaFromScore(focusScore3x3(n, p), p) > 0.01f) falsePeaks++;
    }
    require(falsePeaks < 10, "dark-noise false positive rejection");

    auto hi = edge(0.0f);
    for (float& v : hi) v = 0.1f + v * 0.8f;
    auto lo = edge(0.0f);
    for (float& v : lo) v = 0.4f + v * 0.2f;
    const float a = focusScore3x3(lo, p), b = focusScore3x3(hi, p);
    require(a > 0.0f && b > 0.0f && std::max(a, b) / std::min(a, b) < 5.0f, "contrast robustness");
    std::cout << "FOCUS_CPU_PASS scores=" << s0 << "," << s1 << "," << s2 << "," << s4 << " noise_fp=" << falsePeaks
              << "\n";
}

static void testFocusDilation() {
    const uint32_t w = 7, h = 7;
    std::vector<float> alphas(size_t(w) * h, 0.0f), out(size_t(w) * h, 0.0f);
    alphas[size_t(3) * w + 3] = 1.0f;
    dilateFocusAlpha(alphas.data(), out.data(), w, h);
    require(near(out[size_t(3) * w + 3], 1.0f, 1e-6f), "dilation core stays bright");
    for (int dy = -1; dy <= 1; dy++)
        for (int dx = -1; dx <= 1; dx++) {
            if (dx == 0 && dy == 0) continue;
            require(near(out[size_t(3 + dy) * w + (3 + dx)], 0.4f, 1e-6f), "dilation dim halo");
        }
    require(out[0] == 0.0f && out[size_t(h - 1) * w + (w - 1)] == 0.0f, "dilation locality");
    std::cout << "FOCUS_DILATION_CPU_PASS\n";
}

static void testTonemapShadow() {
    TonemapShadowParams p{};
    p.thresholdIre = 10.0f;
    require(tonemapShadowMask(0.02f, 0.02f, 0.02f, p), "crushed black zebra");
    require(!tonemapShadowMask(0.5f, 0.5f, 0.5f, p), "mid gray clean");
    require(!tonemapShadowMask(0.9f, 0.9f, 0.9f, p), "white clean");
    // Slider values must not move the mask by themselves: the tonemapped image
    // carries the slider effect, so the same pixels decide identically.
    // Darkening the image (what negative Shadows does) grows the zebra;
    // lifting it (positive Shadows) shrinks it.
    TonemapShadowParams lifted = p;
    lifted.shadowsUI = 80.0f;
    require(tonemapShadowMask(0.02f, 0.02f, 0.02f, lifted), "dark still zebra with lifted sliders");
    require(!tonemapShadowMask(0.5f, 0.5f, 0.5f, lifted), "no slider expansion into midtones");
    require(tonemapShadowMask(0.05f, 0.05f, 0.05f, p) && !tonemapShadowMask(0.15f, 0.15f, 0.15f, p),
            "threshold straddle");
    TonemapShadowParams off = p;
    off.enabled = false;
    require(!tonemapShadowMask(0.0f, 0.0f, 0.0f, off), "disabled shadow");
    // Zebra alternates with the expected period.
    p.stripePeriod = 8.0f;
    const auto z0 = tonemapShadowOverlay(0, 0, 0.0f, 0.0f, 0.0f, p);
    const auto z8 = tonemapShadowOverlay(8, 0, 0.0f, 0.0f, 0.0f, p);
    require(z0.a > 0.5f && z8.a > 0.5f, "zebra opaque");
    require((z0.r > 0.5f) != (z8.r > 0.5f), "zebra alternation");
    std::cout << "TONEMAP_SHADOW_CPU_PASS\n";
}

static void testFalseColor() {
    FalseColorParams p{};
    p.enabled = true;
    p.rangeCount = 3;
    p.ranges[0] = {0, 10, {1, 0, 0, 0.5f}};
    p.ranges[1] = {10, 50, {0, 1, 0, 0.5f}};
    p.ranges[2] = {50, 100, {0, 0, 1, 0.5f}};
    require(near(encodedSignalIre(.5f, .5f, .5f), 50.0f, 1e-4f), "gray IRE");
    require(falseColorRangeIndex(9.999f, p) == 0, "below boundary");
    require(falseColorRangeIndex(10.0f, p) == 1, "inclusive lower/exclusive upper");
    require(falseColorRangeIndex(50.0f, p) == 2, "second boundary");
    require(falseColorRangeIndex(100.0f, p) == 2, "final inclusive upper");
    require(falseColorRangeIndex(100.01f, p) == -1, "outside range");
    const float y = 0.3f;
    // Different chroma, same weighted encoded-signal luma by construction.
    const float r1 = 0.2f, g1 = 0.3f, b1 = (y - 0.2126f * r1 - 0.7152f * g1) / 0.0722f;
    const float r2 = 0.4f, g2 = 0.25f, b2 = (y - 0.2126f * r2 - 0.7152f * g2) / 0.0722f;
    require(std::fabs(encodedSignalIre(r1, g1, b1) - encodedSignalIre(r2, g2, b2)) < 1e-3f,
            "equal luma chroma patches");
    std::cout << "FALSE_COLOR_CPU_PASS\n";
}

static void testCombinedSemanticInvariance() {
    RawStateOverlayParams rp{};
    FocusPeakingParams fp{};
    FalseColorParams cp{};
    cp.enabled = true;
    cp.rangeCount = 1;
    cp.ranges[0] = {0, 100.001f, {0, 1, 1, 0.4f}};
    TonemapShadowParams sp{};
    sp.thresholdIre = 10.0f;
    const auto f = falseColorOverlay(.5f, .5f, .5f, cp);
    const auto q = focusOverlay(edge(0.0f), fp);
    const auto s = tonemapShadowOverlay(0, 0, .02f, .02f, .02f, sp);
    const auto r = rawStateOverlay(uint16_t(1u << 0), rp);
    const auto combined = over(r, over(s, over(q, f)));
    // Re-evaluate standalone classifications after composition: composition must not feed back.
    const auto f2 = falseColorOverlay(.5f, .5f, .5f, cp);
    const auto q2 = focusOverlay(edge(0.0f), fp);
    const auto r2 = rawStateOverlay(uint16_t(1u << 0), rp);
    require(near(f.a, f2.a) && near(q.a, q2.a) && near(r.a, r2.a), "combined does not alter per-mode classification");
    require(combined.a >= r.a, "premultiplied combined alpha");
    std::cout << "COMBINED_REFERENCE_CPU_PASS\n";
}

static void validateFocusTransferOrdering() {
    monitoring_overlays::FocusPeakingParams p{};
    p.sensitivity = 0.55f;
    const std::array<float, 5> scores{{8.0f, 3.5f, 2.5f, 1.5f, 0.05f}};
    std::array<float, 5> alpha{};
    for (size_t i = 0; i < scores.size(); ++i)
        alpha[i] = monitoring_overlays::cpu_reference::focusAlphaFromScore(scores[i], p);
    if (!(alpha[0] > alpha[1] && alpha[1] > alpha[2] && alpha[2] >= alpha[3] && alpha[3] >= alpha[4]))
        throw std::runtime_error("focus transfer ordering failed");
}

int main() {
    testRawState();
    testFocus();
    testFocusDilation();
    testTonemapShadow();
    validateFocusTransferOrdering();
    testFalseColor();
    testCombinedSemanticInvariance();
    std::cout << "CPU_VALIDATION_PASS\n";
}
