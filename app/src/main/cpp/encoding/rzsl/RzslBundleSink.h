#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include "metadata/FrameMetadataSnapshot.h"

namespace rawrcam::encoding::rzsl {

// Canonical per-frame TSV serialization + atomic .tmp -> .rzsl + .ready
// bundle publish. Extracted from FrameSubmitCoordinator's anonymous-namespace
// writeCanonicalFrameMetadataTsv / writePostShutterRzsl / persistZslRingOnShutter
// which duplicated the same protocol twice.
class RzslBundleSink {
   public:
    struct Frame {
        std::uint64_t frameId = 0;
        std::uint64_t timestampNs = 0;
        std::uint32_t width = 0;
        std::uint32_t height = 0;
        std::uint32_t tilesX = 0;
        std::uint32_t tilesY = 0;
        std::uint32_t streams = 0;
        std::uint32_t tableBytes = 0;
        std::uint32_t payloadBytes = 0;
    };

    // Serialize one frame's metadata to the canonical TSV form.
    static std::string serializeMetadata(std::uint64_t zslFrameId,
                                         const rawrcam::metadata::FrameMetadataSnapshot& metadata);

    // Write a bundle from pre-assembled frames. readPacket(frameId, out)
    // supplies payload bytes; returns {frameCount, bytesWritten}.
    // Throws std::runtime_error on failure (caller maps to .failed marker).
    struct BundleInfo {
        std::uint32_t frameCount = 0;
        std::uint64_t bytesWritten = 0;
    };
    static BundleInfo writeBundle(const std::string& filesDir, std::uint32_t cfa, const std::vector<Frame>& frames,
                                  const std::vector<std::string>& serializedMetadata,
                                  std::function<bool(std::uint64_t, std::vector<std::uint8_t>&)> readPacket);
};
}  // namespace rawrcam::encoding::rzsl
