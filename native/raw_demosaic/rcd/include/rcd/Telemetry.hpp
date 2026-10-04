#pragma once
#include <cstdint>
#include <string>
#include <vector>
namespace rcd {
struct GpuEvent { std::string name; double milliseconds=0.0; };
struct FrameTelemetry {
    uint64_t peakAllocatedBytes=0;
    uint64_t liveAllocatedBytes=0;
    uint64_t bufferAllocationsTotal=0;
    std::vector<GpuEvent> gpuEvents;
};
}
