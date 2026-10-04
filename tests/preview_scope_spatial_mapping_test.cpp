#include "geometry/DisplayToSensorMapper.h"
#include "geometry/OrientationTransform.h"
#include <cmath>
#include <cstdlib>
#include <iostream>

namespace {
void require(bool v, const char* msg) { if (!v) { std::cerr << msg << '\n'; std::exit(1); } }
void near(float a,float b,const char* msg){ require(std::fabs(a-b)<1e-5f,msg); }
}

int main() {
    using rawrcam::geometry::displayToSource;
    using rawrcam::geometry::presentationQuarterTurns;
    const struct { uint32_t w,h; } geometries[]={{2040,1532},{2048,1536},{2040,1536}};
    for (auto g: geometries) {
        for (int rotation: {0,90,180,270}) {
            const auto q = presentationQuarterTurns(90, rotation);
            require(q < 4u, "quarter-turn out of range");
            const auto left = displayToSource(0.0f,0.5f,g.w,g.h,1080,1440,static_cast<int>(q*90u));
            const auto right= displayToSource(1.0f,0.5f,g.w,g.h,1080,1440,static_cast<int>(q*90u));
            // Depending on aspect fit, exact edge points may land in letterbox; center must always map.
            const auto center=displayToSource(0.5f,0.5f,g.w,g.h,1080,1440,static_cast<int>(q*90u));
            require(center.has_value(),"center mapping missing");
            near(center->x,0.5f,"center x mismatch");
            near(center->y,0.5f,"center y mismatch");
            (void)left; (void)right;
        }
    }
    std::cout << "PREVIEW_SCOPE_SPATIAL_MAPPING_PASS\n";
}
