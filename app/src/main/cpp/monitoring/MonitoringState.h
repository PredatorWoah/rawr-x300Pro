#pragma once
#include <array>
#include <cstdint>

namespace rawrcam::monitoring {

enum class ScopeType : uint32_t { None = 0, Waveform = 1, Vectorscope = 2 };

struct ScopePlacement {
    ScopeType type = ScopeType::None;
    float x = 0.0f;
    float y = 0.0f;
    float width = 0.0f;
    float height = 0.0f;
    float cornerRadius = 0.0f;  // Destination-local normalized radius (0..0.5).
    uint32_t variant = 0;       // Waveform: 0 luma, 1 RGB overlay. Vectorscope: unused.
    uint32_t presentationQuarterTurns =
        0;  // Instrument-only presentation rotation; measurement orientation is separate.
};

struct ScopePresentationState {
    std::array<ScopePlacement, 3> placements{};
};

}  // namespace rawrcam::monitoring
