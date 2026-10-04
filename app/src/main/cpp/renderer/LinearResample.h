#pragma once
#include <array>
#include <cstdint>
#include <functional>
namespace rawrcam::renderer {
void resampleLinear(const _Float16* source, uint32_t sw, uint32_t sh, _Float16* destination, uint32_t dw, uint32_t dh,
                    std::array<uint32_t, 4> crop, const std::function<void(uint32_t)>& rowDone);
}
