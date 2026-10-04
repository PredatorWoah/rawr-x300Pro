#include "develop/HotPixelConceal.h"

#include <algorithm>
#include <array>

namespace rawrcam::develop {

std::size_t concealHotPixelsInPackedRaw16(std::vector<uint8_t>& raw16, uint32_t width, uint32_t height,
                                          const std::vector<int32_t>& hotPixelMap) noexcept {
    if (width == 0 || height == 0 || hotPixelMap.size() < 2) return 0;
    const std::size_t expectBytes = static_cast<std::size_t>(width) * height * sizeof(uint16_t);
    if (raw16.size() != expectBytes) return 0;
    auto* pixels = reinterpret_cast<uint16_t*>(raw16.data());

    std::size_t concealed = 0;
    const int32_t w = static_cast<int32_t>(width);
    const int32_t h = static_cast<int32_t>(height);
    for (std::size_t i = 0; i + 1 < hotPixelMap.size(); i += 2) {
        const int32_t x = hotPixelMap[i];
        const int32_t y = hotPixelMap[i + 1];
        if (x < 0 || y < 0 || x >= w || y >= h) continue;
        // Same Bayer-phase neighbors at distance 2 so color plane is preserved.
        std::array<uint16_t, 4> candidates{};
        std::size_t n = 0;
        if (x - 2 >= 0) candidates[n++] = pixels[static_cast<std::size_t>(y) * width + (x - 2)];
        if (x + 2 < w) candidates[n++] = pixels[static_cast<std::size_t>(y) * width + (x + 2)];
        if (y - 2 >= 0) candidates[n++] = pixels[static_cast<std::size_t>(y - 2) * width + x];
        if (y + 2 < h) candidates[n++] = pixels[static_cast<std::size_t>(y + 2) * width + x];
        if (n == 0) continue;
        std::sort(candidates.begin(), candidates.begin() + n);
        const uint16_t median = (n % 2 == 1)
                                    ? candidates[n / 2]
                                    : static_cast<uint16_t>((candidates[n / 2 - 1] + candidates[n / 2] + 1) / 2);
        pixels[static_cast<std::size_t>(y) * width + x] = median;
        ++concealed;
    }
    return concealed;
}

}  // namespace rawrcam::develop
