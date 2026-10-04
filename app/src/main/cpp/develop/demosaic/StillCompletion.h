#pragma once

#include <cstdint>
#include <string>

namespace rawrcam::develop::demosaic {

// Shared completion record for the RCD/VNG4/Dual still adapters. The three
// per-adapter structs were field-identical; a single type lets generic code
// (and future adapters) handle completions uniformly.
struct StillCompletion {
    uint64_t requestId = 0;
    uint64_t timestampNs = 0;
    bool success = false;
    std::string error;
    double submitToFenceMs = 0.0;
    // Part of submitToFenceMs spent building the demosaic engine (pipeline
    // compile + workspace allocation) before any GPU work.
    double setupMs = 0.0;
    uint64_t persistentBytes = 0;
};

}  // namespace rawrcam::develop::demosaic
