#pragma once
#include <cstddef>
#include <cstdint>

namespace rawr::highlight {
// Caller-owned SPIR-V: the library never embeds or loads shaders itself.
struct SpirvWords {
    const uint32_t* words = nullptr;
    size_t count = 0;
};
}  // namespace rawr::highlight
