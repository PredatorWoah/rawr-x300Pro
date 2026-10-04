#pragma once
#include <cstdint>
#include <string>
#include <vector>
namespace vng4 {
struct GpuEvent { std::string name; double milliseconds=0.0; };
struct FrameTelemetry { uint64_t peakAllocatedBytes=0, liveAllocatedBytes=0, bufferAllocationsTotal=0; std::vector<GpuEvent> gpuEvents; };
}
