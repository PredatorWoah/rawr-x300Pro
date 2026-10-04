#pragma once
#define TONEMAP_ENGINE_VERSION_MAJOR 1
#define TONEMAP_ENGINE_VERSION_MINOR 0
#define TONEMAP_ENGINE_VERSION_PATCH 0
#define TONEMAP_ENGINE_VERSION_STRING "1.0.0"
namespace tonemap {
inline constexpr const char* version() noexcept { return TONEMAP_ENGINE_VERSION_STRING; }
}  // namespace tonemap
