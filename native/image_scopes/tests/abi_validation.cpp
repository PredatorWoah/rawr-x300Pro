#include <cstdint>
#include <iostream>
#include <type_traits>

#include "image_scopes/types.h"
using namespace image_scopes;
static_assert(kDisplayWaveformColumns == 512 && kDisplayWaveformBins == 256);
static_assert(kVectorscopeBins == 256);
static_assert(kMaxFramesInFlight == 8);
static_assert(sizeof(WaveformRenderParams) == 24);
static_assert(offsetof(WaveformRenderParams, mode) == 0);
static_assert(offsetof(WaveformRenderParams, densityGain) == 4);
static_assert(offsetof(WaveformRenderParams, opacity) == 8);
static_assert(offsetof(WaveformRenderParams, densityScale) == 12);
static_assert(offsetof(WaveformRenderParams, pointSpreadBins) == 16);
static_assert(kRawWaveformColumns == 256 && kRawWaveformBins == 64);
static_assert(sizeof(SamplingMetadata) == 24);
static_assert(std::is_standard_layout_v<SamplingMetadata>);
#if IMAGE_SCOPES_HAS_VULKAN
static_assert(std::is_standard_layout_v<DisplayWaveformGpuView>);
static_assert(std::is_standard_layout_v<VectorscopeGpuView>);
#endif
int main() {
    std::cout << "IMAGE_SCOPES_ABI_VALIDATION_PASS\n";
}
