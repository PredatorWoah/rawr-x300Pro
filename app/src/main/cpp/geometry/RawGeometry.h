#pragma once

#include <cstdint>
#include <optional>
#include <vector>

namespace rawrcam::geometry {

// Sensor RAW layouts the camera can deliver. RAW16 (Camera2 RAW_SENSOR) is one
// uint16 per pixel; RAW10 packs four pixels into five bytes (MIPI RAW10).
enum class RawPixelFormat : uint8_t { Raw16 = 0, Raw10 = 1 };

[[nodiscard]] constexpr const char* rawPixelFormatName(RawPixelFormat format) noexcept {
    return format == RawPixelFormat::Raw10 ? "RAW10" : "RAW16";
}

struct RawGeometryLimits {
    // Bayer quads need even sides; the multiframe pyramid needs ~1k per side.
    uint32_t minDimension = 1024;
    // Conservative maxImageDimension2D (Adreno and Mali report 16384).
    uint32_t maxDimension = 16384;
};

// Single owner of the RAW geometry the camera session and all still demosaic
// adapters accept. Any even size within limits works; the pipeline's
// dispatches are ceil-div with bounds checks.
[[nodiscard]] constexpr bool isSupportedRawGeometry(uint32_t width, uint32_t height,
                                                    const RawGeometryLimits& limits = {}) noexcept {
    return (width & 1u) == 0u && (height & 1u) == 0u && width >= limits.minDimension && height >= limits.minDimension &&
           width <= limits.maxDimension && height <= limits.maxDimension;
}

struct RawStreamOption {
    RawPixelFormat format = RawPixelFormat::Raw16;
    uint32_t width = 0;
    uint32_t height = 0;
    friend constexpr bool operator==(const RawStreamOption& a, const RawStreamOption& b) noexcept {
        return a.format == b.format && a.width == b.width && a.height == b.height;
    }
};

// Width/height of 0 means "largest supported" for the preferred format.
struct RawStreamPreference {
    RawPixelFormat format = RawPixelFormat::Raw16;
    uint32_t width = 0;
    uint32_t height = 0;
};

// Picks the RAW output stream for a camera:
//   1. the exact preferred {format, size} when the device lists it;
//   2. otherwise the largest supported size in the preferred format;
//   3. otherwise the largest supported size in the other format.
// RAW10 needs a width divisible by 4 for its 5-byte groups.
[[nodiscard]] inline std::optional<RawStreamOption> negotiateRawStream(const std::vector<RawStreamOption>& available,
                                                                       const RawStreamPreference& preference,
                                                                       const RawGeometryLimits& limits = {}) {
    const auto usable = [&](const RawStreamOption& o) {
        return isSupportedRawGeometry(o.width, o.height, limits) &&
               (o.format != RawPixelFormat::Raw10 || o.width % 4u == 0u);
    };
    if (preference.width && preference.height) {
        for (const auto& o : available)
            if (o.format == preference.format && o.width == preference.width && o.height == preference.height &&
                usable(o))
                return o;
    }
    const auto largest = [&](RawPixelFormat format) -> std::optional<RawStreamOption> {
        std::optional<RawStreamOption> best;
        for (const auto& o : available) {
            if (o.format != format || !usable(o)) continue;
            const uint64_t area = static_cast<uint64_t>(o.width) * o.height;
            if (!best || area > static_cast<uint64_t>(best->width) * best->height) best = o;
        }
        return best;
    };
    if (auto preferred = largest(preference.format)) return preferred;
    return largest(preference.format == RawPixelFormat::Raw16 ? RawPixelFormat::Raw10 : RawPixelFormat::Raw16);
}

}  // namespace rawrcam::geometry
