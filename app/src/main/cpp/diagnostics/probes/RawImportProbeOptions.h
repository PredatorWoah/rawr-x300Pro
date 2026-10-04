#pragma once
#include <cstdint>

#include "vulkan/RawImportOptions.h"
namespace rawrcam::diagnostics {
inline vulkan::RawImportOptions rawImportProbeOptions(uint32_t mode) noexcept { return {mode == 15u}; }
}  // namespace rawrcam::diagnostics
