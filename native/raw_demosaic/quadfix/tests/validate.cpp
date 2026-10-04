// Host validation for the quadfix CPU reference (tests/QuadfixCpu.h) against
// the checked-in golden vectors (tests/data, see tests/golden_gen.py).
// No Vulkan needed. Fails nonzero on tolerance breach.
// Build: clang++ -std=c++17 -O2 -I ../include tests/validate.cpp -o /tmp/qf-validate
// Run:   /tmp/qf-validate tests/data
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

#include "QuadfixCpu.h"

namespace {

std::vector<float> loadF32(const std::string& path, size_t expect) {
    FILE* f = std::fopen(path.c_str(), "rb");
    if (!f) {
        std::printf("MISSING %s\n", path.c_str());
        std::exit(2);
    }
    std::vector<float> v(expect);
    size_t n = std::fread(v.data(), sizeof(float), expect, f);
    std::fclose(f);
    if (n != expect) {
        std::printf("SHORT %s (%zu/%zu)\n", path.c_str(), n, expect);
        std::exit(2);
    }
    return v;
}

int checkField(const std::string& name, const std::vector<float>& got, const std::vector<float>& want, float tol) {
    double ss = 0, mx = 0;
    for (size_t i = 0; i < got.size(); ++i) {
        double d = std::fabs(double(got[i]) - double(want[i]));
        ss += d * d;
        mx = std::max(mx, d);
    }
    double rms = std::sqrt(ss / got.size());
    bool ok = mx <= tol;
    std::printf("  %-18s rms=%.2e max=%.2e tol=%.0e %s\n", name.c_str(), rms, mx, double(tol),
                ok ? "OK" : "FAIL");
    return ok ? 0 : 1;
}

}  // namespace

int main(int argc, char** argv) {
    if (argc != 2) {
        std::printf("usage: validate <data-dir>\n");
        return 2;
    }
    const std::string dir = argv[1];
    const int N = 128;
    int fails = 0;
    for (const char* cs : {"lattice", "step", "sky"})
        for (const char* tag : {"true", "fast"}) {
            std::printf("%s/%s:\n", cs, tag);
            auto in = loadF32(dir + "/in_" + cs + ".f32", size_t(N) * N);
            quadfix::cpu::Image img;
            img.w = img.h = N;
            img.px = in;
            quadfix::cpu::Result r = quadfix::cpu::filter(img, std::string(tag) == "fast");
            fails += checkField("guide", r.guide.px, loadF32(dir + "/guide_" + cs + "_" + tag + ".f32", size_t(N) * N),
                                1e-6f);
            fails += checkField("mask", r.mask.px, loadF32(dir + "/mask_" + cs + "_" + tag + ".f32", size_t(N) * N),
                                2e-3f);
            fails += checkField("out", r.out.px, loadF32(dir + "/out_" + cs + "_" + tag + ".f32", size_t(N) * N),
                                5e-2f);
        }
    std::printf(fails ? "VALIDATE FAIL (%d)\n" : "VALIDATE PASS\n", fails);
    return fails ? 1 : 0;
}
