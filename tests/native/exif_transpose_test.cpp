// EXIF transpose lock: the JNI preview path applies orientation natively so
// Kotlin needs no second Bitmap. An asymmetric 4x3 fixture with distinct
// values catches any mirror/rotation confusion across all 8 orientations.
#include <algorithm>
#include <cstdio>
#include <vector>

#include "renderer/ExifTranspose.h"

namespace {
constexpr unsigned W = 4, H = 3;
std::vector<int> fixture() {
    std::vector<int> v(W * H);
    for (unsigned i = 0; i < W * H; ++i) v[i] = int(i);
    return v;
}
// src(x, y) on the fixture.
int at(unsigned x, unsigned y) { return int(y * W + x); }
int failures = 0;
void check(bool ok, const char* what, unsigned orientation) {
    if (!ok) {
        ++failures;
        std::printf("FAIL orientation %u: %s\n", orientation, what);
    }
}
}  // namespace

int main() {
    using rawrcam::renderer::transposeExif;
    using rawrcam::renderer::transposedDimensions;
    const auto src = fixture();
    for (unsigned o = 1; o <= 8; ++o) {
        const auto dims = transposedDimensions(W, H, o);
        const bool swapped = o >= 5;
        check(dims.first == (swapped ? H : W) && dims.second == (swapped ? W : H), "dims", o);
        const auto dst = transposeExif(src, W, H, o);
        check(dst.size() == size_t(dims.first) * dims.second, "size", o);
        // Bijection: every source pixel appears exactly once.
        auto sorted = dst;
        std::sort(sorted.begin(), sorted.end());
        check(sorted == src, "bijection", o);
    }
    // Hand-derived corners on the asymmetric fixture.
    check(transposeExif(src, W, H, 1)[0] == at(0, 0), "o1", 1);
    check(transposeExif(src, W, H, 2)[0] == at(3, 0), "o2 tl", 2);
    check(transposeExif(src, W, H, 2)[3 * W - 1] == at(0, 2), "o2 br", 2);
    check(transposeExif(src, W, H, 3)[0] == at(3, 2), "o3 tl", 3);
    check(transposeExif(src, W, H, 3)[3 * W - 1] == at(0, 0), "o3 br", 3);
    check(transposeExif(src, W, H, 4)[0] == at(0, 2), "o4 tl", 4);
    check(transposeExif(src, W, H, 4)[3 * W - 1] == at(3, 0), "o4 br", 4);
    // Orientations 5..8 yield 3x4 outputs.
    const auto t5 = transposeExif(src, W, H, 5);
    check(t5[0] == at(0, 0) && t5[3 * 4 - 1] == at(3, 2), "o5 corners", 5);
    const auto t6 = transposeExif(src, W, H, 6);
    check(t6[0] == at(0, 2) && t6[3 * 4 - 1] == at(3, 0), "o6 corners", 6);
    const auto t7 = transposeExif(src, W, H, 7);
    check(t7[0] == at(3, 2) && t7[3 * 4 - 1] == at(0, 0), "o7 corners", 7);
    const auto t8 = transposeExif(src, W, H, 8);
    check(t8[0] == at(3, 0) && t8[3 * 4 - 1] == at(0, 2), "o8 corners", 8);
    // Round trips: mirrors/transposes are involutions; 6 and 8 undo each other.
    for (unsigned o : {2u, 3u, 4u, 5u, 7u}) {
        const auto once = transposeExif(src, W, H, o);
        const auto dims = transposedDimensions(W, H, o);
        const auto twice = transposeExif(once, dims.first, dims.second, o);
        check(twice == src, "involution", o);
    }
    const auto r6 = transposeExif(src, W, H, 6);
    const auto d6 = transposedDimensions(W, H, 6);
    check(transposeExif(r6, d6.first, d6.second, 8) == src, "6+8", 6);
    const auto r8 = transposeExif(src, W, H, 8);
    const auto d8 = transposedDimensions(W, H, 8);
    check(transposeExif(r8, d8.first, d8.second, 6) == src, "8+6", 8);
    // Out-of-range orientations and short inputs pass through untouched.
    check(transposeExif(src, W, H, 0) == src, "o0", 0);
    check(transposeExif(src, W, H, 9) == src, "o9", 9);
    check(transposeExif(std::vector<int>{1, 2}, W, H, 6) == std::vector<int>{1, 2}, "short", 6);
    if (failures == 0) puts("EXIF_TRANSPOSE_PASS");
    return failures == 0 ? 0 : 1;
}
