#pragma once

#include "galosh/GaloshCommon.hpp"

// App-side aggregator: builds the GaloshShaderMap from the SPIR-V blobs
// embedded by app/.../CMakeLists.txt (GALOSH_RAW_KERNELS). Single owner so
// the taps (SharedHighlightRuntime, StillImageRenderer) never list
// kernels themselves.
namespace rawrcam::develop::galosh {

::galosh::GaloshShaderMap rawShaderMap();

// YUV engine set (P1b linear entry): yuv_* + app-authored
// galosh_yuv_bridge_*; the 5 shared o32_* helpers ride along.
::galosh::GaloshShaderMap yuvShaderMap();

}  // namespace rawrcam::develop::galosh
