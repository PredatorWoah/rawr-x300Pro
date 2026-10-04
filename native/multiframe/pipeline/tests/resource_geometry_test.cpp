#include <rawr/raw_gpu_pipeline/ResourceGeometry.h>

#include <cassert>
using namespace rawr::raw_gpu_pipeline;
int main() {
    {
        const auto g = makeMultiframeGeometry(4080, 3064, 1.0f);
        assert(g.guide.width == 2040 && g.guide.height == 1532);
        assert(g.coarseToFine[0].image.width == 122 && g.coarseToFine[0].image.height == 90 &&
               g.coarseToFine[0].tileSize == 8);
        assert(g.coarseToFine[1].image.width == 505 && g.coarseToFine[1].image.height == 378 &&
               g.coarseToFine[1].tileSize == 16);
        assert(g.coarseToFine[2].image.width == 2036 && g.coarseToFine[2].image.height == 1528);
        assert(g.coarseToFine[3].image.width == 4080 && g.coarseToFine[3].image.height == 3064);
        assert(g.output.width == 4080 && g.output.height == 3064);
    }
    {
        const auto g = makeMultiframeGeometry(4096, 3072, 2.0f);
        assert(g.coarseToFine[0].image.width == 122 && g.coarseToFine[0].image.height == 90);
        assert(g.coarseToFine[1].image.width == 507 && g.coarseToFine[1].image.height == 379);
        assert(g.coarseToFine[2].image.width == 2044 && g.coarseToFine[2].image.height == 1532);
        assert(g.output.width == 8192 && g.output.height == 6144);
    }
    {
        const auto g = makeMultiframeGeometry(4080, 3064, 1.264911f);
        assert(g.output.width == 5158 && g.output.height == 3874);
        assert((g.output.width & 1u) == 0u && (g.output.height & 1u) == 0u);
    }
    {
        const auto g = makeMultiframeGeometry(4080, 3064, 1.385641f);
        assert(g.output.width == 5652 && g.output.height == 4244);
        assert((g.output.width & 1u) == 0u && (g.output.height & 1u) == 0u);
    }
}
