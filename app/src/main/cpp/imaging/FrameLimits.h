#pragma once

#include <cstddef>
#include <cstdint>

namespace rawrcam::imaging {

// Application-wide realtime buffering contract. Every per-frame-slot subsystem
// must size and validate its resources from this single owner so preview,
// statistics, monitoring, diagnostics, and presentation cannot drift apart.
inline constexpr std::uint32_t kRealtimeFramesInFlight = 3;

inline constexpr std::size_t kRawReaderMaxImages = 6;
inline constexpr std::size_t kMaxIngressImages = 1;
inline constexpr std::size_t kMaxIngressMetadata = 8;
inline constexpr std::size_t kMaxPairerImages = 1;
inline constexpr std::size_t kMaxPairerMetadata = 3;
static_assert(kRealtimeFramesInFlight + kMaxIngressImages + kMaxPairerImages < kRawReaderMaxImages,
              "RAW transport must retain one reader lease of headroom");

}  // namespace rawrcam::imaging
