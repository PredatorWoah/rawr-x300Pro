#include <rawr/raw_gpu_pipeline/ReconstructionTiling.h>

#include <cassert>
#include <cstdint>
using namespace rawr::raw_gpu_pipeline;
int main() {
    auto t = makeReconstructionTiling({8160, 6128}, 256);
    assert(t.stripes.size() == 24u);
    assert(t.stripes.front().yBase == 0u && t.stripes.front().height == 256u);
    assert(t.stripes.back().yBase == 5888u && t.stripes.back().height == 240u);
    std::uint32_t y = 0;
    for (const auto& s : t.stripes) {
        assert(s.yBase == y);
        y += s.height;
    }
    assert(y == 6128u);
    const auto full = rgba32AccumulatorBytes({8160, 6128});
    const auto strip = rgba32AccumulatorBytes({8160, 256});
    assert(full > 1500000000ull);
    assert(strip < 70000000ull);
    return 0;
}
