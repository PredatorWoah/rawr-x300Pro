#include "raw_sharpness/raw_sharpness_types.hpp"

#include <cmath>
#include <limits>

namespace raw_sharpness::reference {
namespace {

bool isGreen(std::uint32_t x, std::uint32_t y, BayerPattern pattern) {
    const bool odd = ((x + y) & 1u) != 0u;
    return (pattern == BayerPattern::GRBG || pattern == BayerPattern::GBRG) ? !odd : odd;
}

}  // namespace

float scoreCpu(const std::uint8_t* rawBytes, std::uint32_t width, std::uint32_t height,
               BayerPattern pattern, float whiteLevel) {
    if (!rawBytes || width < 8u || height < 8u) return 0.0f;
    const auto* px = reinterpret_cast<const std::uint16_t*>(rawBytes);
    const float white = std::isfinite(whiteLevel) && whiteLevel > 0.0f ? whiteLevel : 65535.0f;
    // Step 2 in both axes stays on the green lattice; the per-row x start is
    // aligned to a green photosite for the pattern (a fixed start with an
    // even stride would miss green entirely on RGGB/BGGR).
    constexpr std::uint32_t kStride = 2u;
    constexpr std::uint32_t kTap = 2u;  // stay on the same green lattice
    double sum = 0.0, sumSq = 0.0, sigSum = 0.0;
    std::uint64_t n = 0;
    for (std::uint32_t y = kTap; y + kTap < height; y += kStride) {
        std::uint32_t x0 = kTap;
        while (x0 + kTap < width && !isGreen(x0, y, pattern)) ++x0;
        for (std::uint32_t x = x0; x + kTap < width; x += kStride) {
            const float c = static_cast<float>(px[y * width + x]);
            const float l = static_cast<float>(px[y * width + (x - kTap)]);
            const float r = static_cast<float>(px[y * width + (x + kTap)]);
            const float u = static_cast<float>(px[(y - kTap) * width + x]);
            const float d = static_cast<float>(px[(y + kTap) * width + x]);
            if (c >= white || l >= white || r >= white || u >= white || d >= white) continue;
            const double lap = 4.0 * c - l - r - u - d;
            sum += lap;
            sumSq += lap * lap;
            sigSum += c;
            ++n;
        }
    }
    if (n < 1024u) return 0.0f;
    const double mean = sum / n;
    double var = sumSq / n - mean * mean;
    if (!(var > 0.0)) return 0.0f;
    // Mean-normalize by signal level so a brighter frame does not win
    // spuriously. The Laplacian is differential so black offset cancels.
    const double sigMean = sigSum / n;
    if (!(sigMean > 1.0)) return 0.0f;
    var /= (sigMean * sigMean);
    if (!std::isfinite(var) || var <= 0.0) return 0.0f;
    return static_cast<float>(var);
}

std::uint32_t selectSharpest(const std::vector<float>& scores, std::uint32_t middleIndex) {
    if (scores.empty()) return middleIndex;
    if (middleIndex >= scores.size()) middleIndex = static_cast<std::uint32_t>(scores.size() / 2u);
    float best = -std::numeric_limits<float>::infinity();
    for (float s : scores) {
        if (std::isfinite(s) && s > best) best = s;
    }
    if (!std::isfinite(best) || best <= 0.0f) return middleIndex;
    // Near-tie epsilon: prefer the frame closest to the middle for stability.
    const float eps = best * 1e-4f;
    std::uint32_t winner = middleIndex;
    float winnerScore = std::isfinite(scores[middleIndex]) ? scores[middleIndex] : -1.0f;
    for (std::uint32_t i = 0; i < scores.size(); ++i) {
        const float s = scores[i];
        if (!std::isfinite(s)) continue;
        if (s > winnerScore + eps) {
            winnerScore = s;
            winner = i;
        } else if (winnerScore >= 0.0f && s >= winnerScore - eps) {
            const std::uint32_t di = i > middleIndex ? i - middleIndex : middleIndex - i;
            const std::uint32_t dw = winner > middleIndex ? winner - middleIndex : middleIndex - winner;
            if (di < dw) winner = i;
        }
    }
    return winner;
}

}  // namespace raw_sharpness::reference
