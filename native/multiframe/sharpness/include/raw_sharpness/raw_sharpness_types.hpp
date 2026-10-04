#pragma once

#include <cstdint>
#include <vector>

namespace raw_sharpness {

// Bayer pattern codes match raw_stats::BayerPattern and the Camera2 color
// filter arrangement order (0=RGGB, 1=GRBG, 2=GBRG, 3=BGGR).
enum class BayerPattern : std::uint32_t { RGGB = 0, GRBG = 1, GBRG = 2, BGGR = 3 };

namespace reference {

// Mean-normalized variance of a green-channel Laplacian over a packed
// little-endian RAW16 payload (rowStride == width * 2). Host reference for
// the GPU scorer; same taps, green lattice and saturation skip. Used by
// host-side validation and offline analysis, never on the shutter path.
float scoreCpu(const std::uint8_t* rawBytes, std::uint32_t width, std::uint32_t height,
               BayerPattern pattern, float whiteLevel);

// Argmax over per-frame scores with a middle bias on near-ties. Returns
// `middleIndex` on degenerate input (empty, non-finite, all non-positive).
std::uint32_t selectSharpest(const std::vector<float>& scores, std::uint32_t middleIndex);

}  // namespace reference
}  // namespace raw_sharpness
