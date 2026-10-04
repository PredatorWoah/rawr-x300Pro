#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

namespace rawrcam::video {
// Post-encode signalling only: unspecified gamut/transfer (2/2), BT.709
// YCbCr matrix (1), limited range. Throws on malformed/unsupported SPS.
std::vector<uint8_t> logHevcSps(const uint8_t* nal, size_t size);
// Handles Android Annex B CSD/access units and four-byte length-prefixed
// samples. All SPSs (including repeated headers) are rewritten; other NALs
// remain byte-identical. requireSps is true for encoder codec configuration.
std::vector<uint8_t> logHevcAccessUnit(const uint8_t* data, size_t size, bool requireSps = false);
}  // namespace rawrcam::video
