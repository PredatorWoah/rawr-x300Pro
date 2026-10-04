#pragma once
#include "Types.hpp"
#include <cstdint>
namespace vng4 {
enum class InputMode : uint32_t { NormalizedFloatBuffer=0, RawR16UintImage=1, RawR32FloatImage=2, PackedCfaRgba16fImage=3 };
struct PipelineConfig {
    uint32_t width=0,height=0; BayerPattern pattern=BayerPattern::BGGR; InputMode inputMode=InputMode::RawR16UintImage;
    // Same RCD scaling convention. Internal VNG4 uses nominal 0..1.
    float outputScale=1.0f/255.0f; float outputAlpha=1.0f; bool telemetry=false;
    uint32_t linearWorkgroupX=16, linearWorkgroupY=16;
    // Logical output coverage per green dispatch group. The frozen pair-x shader uses
    // a 16x16 local size and emits two horizontal pixels per invocation => 32x16 coverage.
    uint32_t greenWorkgroupX=32, greenWorkgroupY=16;
    uint32_t exportWorkgroupX=16, exportWorkgroupY=16;
};
struct PipelineAssets {};
}
