#pragma once
#include <cstdint>
#include <vector>

#include "image_scopes/types.h"

namespace image_scopes::cpu {

struct Rgba8ImageView {
    const std::uint8_t* data = nullptr;
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    std::uint32_t rowStrideBytes = 0;
};

struct WaveformData {
    SamplingMetadata sampling{};
    WaveformMode mode = WaveformMode::Luma;
    std::vector<std::uint32_t> density;
};

struct VectorscopeData {
    SamplingMetadata sampling{};
    std::vector<std::uint32_t> density;
};

std::uint8_t encodedLumaQ16(std::uint8_t r, std::uint8_t g, std::uint8_t b) noexcept;
void bt709DerivedCbCrQ16(std::uint8_t r, std::uint8_t g, std::uint8_t b, std::uint8_t& cb, std::uint8_t& cr) noexcept;
bool sampleSelected(std::uint32_t x, std::uint32_t y, SamplingMode mode) noexcept;

WaveformData measureDisplayWaveform(Rgba8ImageView image, WaveformMode mode, SamplingMode samplingMode,
                                    std::uint32_t horizontalBins = kDisplayWaveformColumns,
                                    std::uint32_t signalBins = kDisplayWaveformBins);
VectorscopeData measureVectorscope(Rgba8ImageView image, SamplingMode samplingMode,
                                   std::uint32_t bins = kVectorscopeBins);

}  // namespace image_scopes::cpu
