#include "rawr/zsl_container/ZslContainer.h"

#include <cstring>
#include <fstream>
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>

namespace rawr::zsl_container {
namespace {
#pragma pack(push, 1)
struct BundleHeader {
    char magic[8];
    std::uint32_t version;
    std::uint32_t headerBytes;
    std::uint32_t frameCount;
    std::uint32_t cfa;
    std::uint64_t indexOffset;
    std::uint64_t indexEntryBytes;
    std::uint64_t reserved[3];
};
struct FrameIndex {
    std::uint64_t frameId;
    std::uint64_t timestampNs;
    std::uint64_t metadataOffset;
    std::uint64_t metadataBytes;
    std::uint64_t packetOffset;
    std::uint64_t packetBytes;
    std::uint32_t width;
    std::uint32_t height;
};
struct PacketHeader {
    char magic[8];
    std::uint32_t version;
    std::uint32_t headerBytes;
    std::uint32_t width;
    std::uint32_t height;
    std::uint32_t tilesX;
    std::uint32_t tilesY;
    std::uint32_t streams;
    std::uint32_t reserved;
    std::uint64_t timestampNs;
    std::uint64_t payloadBytes;
};
#pragma pack(pop)
static_assert(sizeof(BundleHeader) == 64u);
static_assert(sizeof(FrameIndex) == 56u);
static_assert(sizeof(PacketHeader) == 56u);

void checkedWrite(std::ofstream& out, const void* data, std::size_t size, const char* what) {
    if (size == 0) return;
    out.write(static_cast<const char*>(data), static_cast<std::streamsize>(size));
    if (!out) throw std::runtime_error(std::string("zsl_container: write ") + what);
}

void checkedRead(std::ifstream& in, void* data, std::size_t size, const char* what) {
    if (size == 0) return;
    in.read(static_cast<char*>(data), static_cast<std::streamsize>(size));
    if (!in) throw std::runtime_error(std::string("zsl_container: read ") + what);
}

bool magicEquals(const char* actual, const char (&expected)[9]) {
    return std::memcmp(actual, expected, 8u) == 0;
}

std::vector<std::uint8_t> canonicalPacket(const PackedFrame& f) {
    if (f.streams == 0 || f.tableBytes != static_cast<std::uint64_t>(f.streams) * 4u)
        throw std::runtime_error("zsl_container: invalid stream table");
    const std::uint64_t required64 = f.tableBytes * 3u + f.payloadBytes;
    if (required64 > std::numeric_limits<std::size_t>::max() ||
        f.gpuPacket.size() != static_cast<std::size_t>(required64))
        throw std::runtime_error("zsl_container: invalid gpu packet size");
    const auto* base = f.gpuPacket.data();
    const auto* meta = reinterpret_cast<const std::uint32_t*>(base);
    const auto* sizes = reinterpret_cast<const std::uint32_t*>(base + f.tableBytes);
    const auto* offsets = reinterpret_cast<const std::uint32_t*>(base + f.tableBytes * 2u);
    const auto* payload = base + f.tableBytes * 3u;

    std::uint64_t payloadWords = 0;
    for (std::uint32_t i = 0; i < f.streams; ++i) payloadWords += sizes[i];
    if (payloadWords * 4u != f.payloadBytes) throw std::runtime_error("zsl_container: word count mismatch");

    const std::uint64_t out64 = sizeof(PacketHeader) + f.tableBytes * 2u + f.payloadBytes;
    if (out64 > std::numeric_limits<std::size_t>::max()) throw std::runtime_error("zsl_container: packet too large");
    std::vector<std::uint8_t> out(static_cast<std::size_t>(out64));
    PacketHeader h{{'R', 'Z', 'S', 'L', 'P', 'K', 'T', '1'},
                   1u,
                   sizeof(PacketHeader),
                   f.width,
                   f.height,
                   f.tilesX,
                   f.tilesY,
                   f.streams,
                   0u,
                   f.timestampNs,
                   f.payloadBytes};
    std::uint8_t* dst = out.data();
    std::memcpy(dst, &h, sizeof(h));
    dst += sizeof(h);
    std::memcpy(dst, meta, static_cast<std::size_t>(f.tableBytes));
    dst += f.tableBytes;
    std::memcpy(dst, sizes, static_cast<std::size_t>(f.tableBytes));
    dst += f.tableBytes;
    for (std::uint32_t i = 0; i < f.streams; ++i) {
        const std::uint64_t bytes = static_cast<std::uint64_t>(sizes[i]) * 4u;
        const std::uint64_t src = static_cast<std::uint64_t>(offsets[i]) * 4u;
        if (src + bytes > f.payloadBytes) throw std::runtime_error("zsl_container: payload offset out of range");
        if (bytes) {
            std::memcpy(dst, payload + src, static_cast<std::size_t>(bytes));
            dst += bytes;
        }
    }
    return out;
}
}  // namespace

BundleInfo writeBundleStreaming(const std::string& path, std::uint32_t cfa, const std::vector<PackedFrame>& frames,
                                const PacketReader& reader) {
    if (frames.empty()) throw std::invalid_argument("zsl_container: empty bundle");
    if (!reader) throw std::invalid_argument("zsl_container: missing packet reader");
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out) throw std::runtime_error("zsl_container: open output");
    BundleHeader header{{'R', 'Z', 'S', 'L', 'B', 'N', 'D', '1'},
                        1u,
                        sizeof(BundleHeader),
                        static_cast<std::uint32_t>(frames.size()),
                        cfa,
                        sizeof(BundleHeader),
                        sizeof(FrameIndex),
                        {0, 0, 0}};
    checkedWrite(out, &header, sizeof(header), "header");
    std::vector<FrameIndex> index(frames.size());
    const std::vector<std::uint8_t> zero(index.size() * sizeof(FrameIndex), 0);
    checkedWrite(out, zero.data(), zero.size(), "index placeholder");

    std::vector<std::uint8_t> gpuPacket;
    for (std::size_t i = 0; i < frames.size(); ++i) {
        const auto& descriptor = frames[i];
        gpuPacket.clear();
        if (!reader(descriptor.frameId, gpuPacket))
            throw std::runtime_error("zsl_container: packet read frameId=" + std::to_string(descriptor.frameId));
        PackedFrame f = descriptor;
        f.gpuPacket = std::move(gpuPacket);
        auto packet = canonicalPacket(f);
        FrameIndex ix{};
        ix.frameId = f.frameId;
        ix.timestampNs = f.timestampNs;
        ix.width = f.width;
        ix.height = f.height;
        ix.metadataOffset = static_cast<std::uint64_t>(out.tellp());
        ix.metadataBytes = f.metadata.size();
        checkedWrite(out, f.metadata.data(), f.metadata.size(), "metadata");
        ix.packetOffset = static_cast<std::uint64_t>(out.tellp());
        ix.packetBytes = packet.size();
        checkedWrite(out, packet.data(), packet.size(), "packet");
        index[i] = ix;
        gpuPacket = std::move(f.gpuPacket);
    }
    const std::uint64_t end = static_cast<std::uint64_t>(out.tellp());
    out.seekp(static_cast<std::streamoff>(header.indexOffset), std::ios::beg);
    checkedWrite(out, index.data(), index.size() * sizeof(FrameIndex), "index");
    out.flush();
    if (!out) throw std::runtime_error("zsl_container: flush");
    return BundleInfo{static_cast<std::uint32_t>(frames.size()), end};
}

BundleInfo writeBundle(const std::string& path, std::uint32_t cfa, const std::vector<PackedFrame>& frames) {
    return writeBundleStreaming(path, cfa, frames, [&frames](std::uint64_t frameId, std::vector<std::uint8_t>& out) {
        for (const auto& f : frames) {
            if (f.frameId == frameId) {
                out = f.gpuPacket;
                return true;
            }
        }
        return false;
    });
}

Bundle readBundle(const std::string& path) {
    std::ifstream in(path, std::ios::binary | std::ios::ate);
    if (!in) throw std::runtime_error("zsl_container: open input");
    const auto endPosition = in.tellg();
    if (endPosition < 0) throw std::runtime_error("zsl_container: input size");
    const std::uint64_t fileBytes = static_cast<std::uint64_t>(endPosition);
    if (fileBytes < sizeof(BundleHeader)) throw std::runtime_error("zsl_container: truncated bundle header");
    in.seekg(0, std::ios::beg);
    BundleHeader header{};
    checkedRead(in, &header, sizeof(header), "header");
    if (!magicEquals(header.magic, "RZSLBND1") || header.version != 1u || header.headerBytes != sizeof(BundleHeader) ||
        header.indexEntryBytes != sizeof(FrameIndex) || header.frameCount == 0u || header.frameCount > 1024u)
        throw std::runtime_error("zsl_container: invalid bundle header");
    const std::uint64_t indexBytes = static_cast<std::uint64_t>(header.frameCount) * sizeof(FrameIndex);
    if (header.indexOffset > fileBytes || indexBytes > fileBytes - header.indexOffset)
        throw std::runtime_error("zsl_container: invalid bundle index");
    in.seekg(static_cast<std::streamoff>(header.indexOffset), std::ios::beg);
    std::vector<FrameIndex> index(header.frameCount);
    checkedRead(in, index.data(), static_cast<std::size_t>(indexBytes), "index");

    Bundle bundle{};
    bundle.cfa = header.cfa;
    bundle.frames.reserve(index.size());
    for (const auto& ix : index) {
        if (ix.width == 0u || ix.height == 0u || ix.metadataOffset > fileBytes ||
            ix.metadataBytes > fileBytes - ix.metadataOffset || ix.packetOffset > fileBytes ||
            ix.packetBytes > fileBytes - ix.packetOffset || ix.packetBytes < sizeof(PacketHeader))
            throw std::runtime_error("zsl_container: invalid frame index");
        PackedFrame frame{};
        frame.frameId = ix.frameId;
        frame.timestampNs = ix.timestampNs;
        frame.width = ix.width;
        frame.height = ix.height;
        if (ix.metadataBytes > std::numeric_limits<std::size_t>::max())
            throw std::runtime_error("zsl_container: metadata too large");
        frame.metadata.resize(static_cast<std::size_t>(ix.metadataBytes));
        in.seekg(static_cast<std::streamoff>(ix.metadataOffset), std::ios::beg);
        checkedRead(in, frame.metadata.data(), frame.metadata.size(), "metadata");

        in.seekg(static_cast<std::streamoff>(ix.packetOffset), std::ios::beg);
        PacketHeader packet{};
        checkedRead(in, &packet, sizeof(packet), "packet header");
        if (!magicEquals(packet.magic, "RZSLPKT1") || packet.version != 1u ||
            packet.headerBytes != sizeof(PacketHeader) || packet.width != ix.width || packet.height != ix.height ||
            packet.timestampNs != ix.timestampNs || packet.streams == 0u)
            throw std::runtime_error("zsl_container: invalid packet header");
        frame.tilesX = packet.tilesX;
        frame.tilesY = packet.tilesY;
        frame.streams = packet.streams;
        frame.tableBytes = static_cast<std::uint64_t>(packet.streams) * sizeof(std::uint32_t);
        frame.payloadBytes = packet.payloadBytes;
        const std::uint64_t canonicalBytes = sizeof(PacketHeader) + frame.tableBytes * 2u + frame.payloadBytes;
        if (canonicalBytes != ix.packetBytes || frame.tableBytes > std::numeric_limits<std::size_t>::max() ||
            frame.payloadBytes > std::numeric_limits<std::size_t>::max() ||
            frame.tableBytes * 3u + frame.payloadBytes > std::numeric_limits<std::size_t>::max())
            throw std::runtime_error("zsl_container: invalid packet size");
        const std::size_t table = static_cast<std::size_t>(frame.tableBytes);
        const std::size_t payload = static_cast<std::size_t>(frame.payloadBytes);
        frame.gpuPacket.resize(table * 3u + payload);
        auto* base = frame.gpuPacket.data();
        checkedRead(in, base, table * 2u, "packet tables");
        const auto* sizes = reinterpret_cast<const std::uint32_t*>(base + table);
        auto* offsets = reinterpret_cast<std::uint32_t*>(base + table * 2u);
        std::uint64_t words = 0u;
        for (std::uint32_t stream = 0u; stream < frame.streams; ++stream) {
            if (words > std::numeric_limits<std::uint32_t>::max())
                throw std::runtime_error("zsl_container: packet offsets overflow");
            offsets[stream] = static_cast<std::uint32_t>(words);
            words += sizes[stream];
        }
        if (words * sizeof(std::uint32_t) != frame.payloadBytes)
            throw std::runtime_error("zsl_container: packet word count mismatch");
        checkedRead(in, base + table * 3u, payload, "packet payload");
        bundle.frames.push_back(std::move(frame));
    }
    return bundle;
}

}  // namespace rawr::zsl_container
