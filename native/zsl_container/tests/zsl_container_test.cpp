#include "rawr/zsl_container/ZslContainer.h"

#include <cassert>
#include <cstdint>
#include <filesystem>
#include <vector>

int main() {
    namespace z = rawr::zsl_container;
    z::PackedFrame frame{};
    frame.frameId = 7u;
    frame.timestampNs = 123456u;
    frame.width = 32u;
    frame.height = 32u;
    frame.tilesX = 1u;
    frame.tilesY = 1u;
    frame.streams = 2u;
    frame.tableBytes = 8u;
    frame.payloadBytes = 12u;
    frame.metadata = "fixture-metadata";
    const std::uint32_t words[] = {
        0x00010002u, 0x00020003u,  // meta
        1u, 2u,                    // sizes
        0u, 1u,                    // offsets
        11u, 22u, 33u,             // payload
    };
    const auto* first = reinterpret_cast<const std::uint8_t*>(words);
    frame.gpuPacket.assign(first, first + sizeof(words));

    const auto path = std::filesystem::temp_directory_path() / "rawr_zsl_container_roundtrip.rzsl";
    const auto written = z::writeBundle(path.string(), 0u, {frame});
    assert(written.frameCount == 1u);
    const auto loaded = z::readBundle(path.string());
    std::filesystem::remove(path);
    assert(loaded.cfa == 0u);
    assert(loaded.frames.size() == 1u);
    const auto& actual = loaded.frames.front();
    assert(actual.frameId == frame.frameId);
    assert(actual.timestampNs == frame.timestampNs);
    assert(actual.metadata == frame.metadata);
    assert(actual.gpuPacket == frame.gpuPacket);
    return 0;
}
