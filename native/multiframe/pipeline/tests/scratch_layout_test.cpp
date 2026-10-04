#include <rawr/raw_gpu_pipeline/ResourceGeometry.h>
#include <rawr/raw_gpu_pipeline/ScratchLayout.h>

#include <cassert>
#include <string>
using namespace rawr::raw_gpu_pipeline;
int main() {
    const auto g = makeMultiframeGeometry(4080, 3064, 1.0f);
    const auto full = makeMultiframeScratchLayout(g);
    const auto striped = makeMultiframeScratchLayout(g, 256u);
    bool hasScale = false, hasRefMean = false, hasFullWarp = false, f32Sum = false, f32Weight = false;
    std::uint32_t sumH = 0;
    for (const auto& i : striped.images) {
        hasScale |= i.name == "robust_scale_tiles";
        hasRefMean |= i.name == "reference_mean_lr";
        hasFullWarp |= i.name == "reference_mean_full";
        if (i.name == "rgb_sum") {
            sumH = i.extent.height;
            f32Sum = i.storage == PixelStorage::RGBA32Float;
        }
        if (i.name == "rgb_weight") f32Weight = i.storage == PixelStorage::RGBA32Float;
    }
    assert(hasScale && hasRefMean && !hasFullWarp);
    assert(f32Sum && f32Weight);
    assert(striped.logicalImageBytes > 0);
    assert(striped.logicalBufferBytes > 0);
    assert(sumH == 256u);
    assert(striped.logicalImageBytes < full.logicalImageBytes);
}
