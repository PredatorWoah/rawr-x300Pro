#pragma once

#include <android/hardware_buffer.h>
#include <media/NdkImage.h>

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace rawrcam::diagnostics {

#ifndef NDEBUG
bool raw16SourceParityProbeEnabled() noexcept;

// Debug-only, one-shot capture diagnostic. Compares three views of the same RAW16 frame:
// AImage plane data, a fresh CPU mapping of the AHardwareBuffer, and the already-produced
// packed still snapshot. Returns the fence that Vulkan must wait on after this extra CPU read.
// -2 means the diagnostic unlock failed and the frame must not be submitted to Vulkan.
int runRaw16SourceParityProbe(AImage* image, AHardwareBuffer* ahb, int acquireFenceFd, uint32_t width, uint32_t height,
                              const std::vector<uint8_t>& packed,
                              const std::function<void(const std::string&)>& diagnostic);
#else
inline bool raw16SourceParityProbeEnabled() noexcept { return false; }
#endif

}  // namespace rawrcam::diagnostics
