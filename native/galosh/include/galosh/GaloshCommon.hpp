#pragma once

#include <cstddef>
#include <cstdint>
#include <map>
#include <string>

namespace galosh {

// Single SPIR-V module blob (app embeds via embed_shader(), fills the map).
struct GaloshShader {
    const uint32_t* words = nullptr;
    size_t wordCount = 0;
};

// Keyed by kernel name, see native/galosh/shaders/SHADER_LIST
// (o32_* = RAW engine, yuv_* = YUV engine). Missing entries are
// validated in the P1 constructors (throws).
using GaloshShaderMap = std::map<std::string, GaloshShader>;

}  // namespace galosh
