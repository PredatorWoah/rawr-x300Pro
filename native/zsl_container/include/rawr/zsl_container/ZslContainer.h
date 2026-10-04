#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace rawr::zsl_container {

struct PackedFrame {
    std::uint64_t frameId = 0;
    std::uint64_t timestampNs = 0;
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    std::uint32_t tilesX = 0;
    std::uint32_t tilesY = 0;
    std::uint32_t streams = 0;
    std::uint64_t tableBytes = 0;
    std::uint64_t payloadBytes = 0;
    // GPU-ring readback layout: meta[], sizes[], offsets[], payload[]
    std::vector<std::uint8_t> gpuPacket;
    // Opaque serialization of RawrCam's canonical FrameMetadataSnapshot.
    std::string metadata;
};

struct BundleInfo {
    std::uint32_t frameCount = 0;
    std::uint64_t bytesWritten = 0;
};

struct Bundle {
    std::uint32_t cfa = 0;
    std::vector<PackedFrame> frames;
};

// Writes one deterministic multi-frame .rzsl bundle. Per-frame GPU allocator
// payload is canonicalized here, at the persistence boundary only.
BundleInfo writeBundle(const std::string& path, std::uint32_t cfa, const std::vector<PackedFrame>& frames);

// Bounded-memory persistence path. The reader is invoked once per frame and
// only one GPU packet plus one canonical packet exists in CPU memory at a time.
using PacketReader = std::function<bool(std::uint64_t frameId, std::vector<std::uint8_t>& out)>;
BundleInfo writeBundleStreaming(const std::string& path, std::uint32_t cfa, const std::vector<PackedFrame>& frames,
                                const PacketReader& reader);

// Reads the deterministic persistence format and reconstructs each decoder-ready
// GPU packet as [meta][sizes][offsets][payload]. Intended for replay and golden tests.
Bundle readBundle(const std::string& path);

}  // namespace rawr::zsl_container
