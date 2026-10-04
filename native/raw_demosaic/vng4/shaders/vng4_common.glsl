#ifndef RAWR_VNG4_COMMON_GLSL
#define RAWR_VNG4_COMMON_GLSL
layout(std430, set = 0, binding = 0) readonly buffer NormalizedInput { float bayer255[]; }
normalizedInput;
layout(set = 0, binding = 1, r16ui) uniform readonly uimage2D raw16Input;
layout(set = 0, binding = 2, r32f) uniform readonly image2D rawFloatInput;
layout(set = 0, binding = 3) uniform sampler2D packedInput;
layout(set = 0, binding = 4, rgba32f) uniform image2D working4Image;
layout(set = 0, binding = 5, r32f) uniform image2D greenImage;
layout(set = 0, binding = 6, rgba16f) uniform image2D outputImage;
layout(set = 0, binding = 8, r32f) uniform readonly image2D blendMaskImage;
layout(push_constant) uniform Push {
    uint width;
    uint height;
    uint pattern;
    uint inputMode;
    vec4 black;
    vec4 invRange;
    float outputFactor;
    float outputAlpha;
}
pc;
const int VNG4_BORDER = 3;
// librtprocess VNG4 four-color convention: R=0, G1=1, B=2, G2=3.
int vngColorAt(ivec2 p) {
    int x = p.x & 1, y = p.y & 1;
    if (pc.pattern == 0u) return y == 0 ? (x == 0 ? 0 : 1) : (x == 0 ? 3 : 2);  // RGGB
    if (pc.pattern == 1u) return y == 0 ? (x == 0 ? 1 : 0) : (x == 0 ? 2 : 3);  // GRBG
    if (pc.pattern == 2u) return y == 0 ? (x == 0 ? 3 : 2) : (x == 0 ? 0 : 1);  // GBRG
    return y == 0 ? (x == 0 ? 2 : 3) : (x == 0 ? 1 : 0);                        // BGGR
}
int packedComponentAt(ivec2 p) {
    // Rawr packed physical convention is {R,G1,G2,B} = {0,1,2,3}.
    int vc = vngColorAt(p);
    return vc == 0 ? 0 : (vc == 1 ? 1 : (vc == 2 ? 3 : 2));
}
float comp4(vec4 v, int c) { return c == 0 ? v.x : (c == 1 ? v.y : (c == 2 ? v.z : v.w)); }
ivec2 clampRaw(ivec2 p) { return clamp(p, ivec2(0), ivec2(int(pc.width) - 1, int(pc.height) - 1)); }
float cfa(ivec2 p) {
    p = clampRaw(p);
    int parity = ((p.y & 1) << 1) | (p.x & 1);
    if (pc.inputMode == 0u) return normalizedInput.bayer255[p.y * int(pc.width) + p.x] * (1.0 / 255.0);
    if (pc.inputMode == 1u) {
        float v = float(imageLoad(raw16Input, p).r);
        return (v - comp4(pc.black, parity)) * comp4(pc.invRange, parity);
    }
    if (pc.inputMode == 2u) {
        float v = imageLoad(rawFloatInput, p).r;
        return (v - comp4(pc.black, parity)) * comp4(pc.invRange, parity);
    }
    vec4 v = texelFetch(packedInput, p >> 1, 0);
    return comp4(v, packedComponentAt(p));
}
vec4 working4(ivec2 p) { return imageLoad(working4Image, p); }
float greenAt(ivec2 p) { return imageLoad(greenImage, p).r; }
bool inside(ivec2 p, int b) { return p.x >= b && p.y >= b && p.x < int(pc.width) - b && p.y < int(pc.height) - b; }
int rgbColorAt(ivec2 p) {
    int c = vngColorAt(p);
    return c == 0 ? 0 : (c == 2 ? 2 : 1);
}
vec3 librtprocessBayerBorderRgb(ivec2 p) {
    vec3 sum = vec3(0);
    ivec3 count = ivec3(0);
    for (int dy = -1; dy <= 1; ++dy)
        for (int dx = -1; dx <= 1; ++dx) {
            ivec2 q = p + ivec2(dx, dy);
            if (q.x < 0 || q.y < 0 || q.x >= int(pc.width) || q.y >= int(pc.height)) continue;
            int c = rgbColorAt(q);
            float v = cfa(q);
            sum[c] += v;
            count[c]++;
        }
    vec3 o = sum / vec3(count);
    int own = rgbColorAt(p);
    o[own] = cfa(p);
    return o;
}
const int VNG_TERMS[384] = int[384](
    -2, -2, 0, -1, 0, 1, -2, -2, 0, 0, 1, 1, -2, -1, -1, 0, 0, 1, -2, -1, 0, -1, 0, 2, -2, -1, 0, 0, 0, 3, -2, -1, 0, 1,
    1, 1, -2, 0, 0, -1, 0, 6, -2, 0, 0, 0, 1, 2, -2, 0, 0, 1, 0, 3, -2, 1, -1, 0, 0, 4, -2, 1, 0, -1, 1, 4, -2, 1, 0, 0,
    0, 6, -2, 1, 0, 1, 0, 2, -2, 2, 0, 0, 1, 4, -2, 2, 0, 1, 0, 4, -1, -2, -1, 0, 0, 128, -1, -2, 0, -1, 0, 1, -1, -2,
    1, -1, 0, 1, -1, -2, 1, 0, 1, 1, -1, -1, -1, 1, 0, 136, -1, -1, 1, -2, 0, 64, -1, -1, 1, -1, 0, 34, -1, -1, 1, 0, 0,
    51, -1, -1, 1, 1, 1, 17, -1, 0, -1, 2, 0, 8, -1, 0, 0, -1, 0, 68, -1, 0, 0, 1, 0, 17, -1, 0, 1, -2, 1, 64, -1, 0, 1,
    -1, 0, 102, -1, 0, 1, 0, 1, 34, -1, 0, 1, 1, 0, 51, -1, 0, 1, 2, 1, 16, -1, 1, 1, -1, 1, 68, -1, 1, 1, 0, 0, 102,
    -1, 1, 1, 1, 0, 34, -1, 1, 1, 2, 0, 16, -1, 2, 0, 1, 0, 4, -1, 2, 1, 0, 1, 4, -1, 2, 1, 1, 0, 4, 0, -2, 0, 0, 1,
    128, 0, -1, 0, 1, 1, 136, 0, -1, 1, -2, 0, 64, 0, -1, 1, 0, 0, 17, 0, -1, 2, -2, 0, 64, 0, -1, 2, -1, 0, 32, 0, -1,
    2, 0, 0, 48, 0, -1, 2, 1, 1, 16, 0, 0, 0, 2, 1, 8, 0, 0, 2, -2, 1, 64, 0, 0, 2, -1, 0, 96, 0, 0, 2, 0, 1, 32, 0, 0,
    2, 1, 0, 48, 0, 0, 2, 2, 1, 16, 0, 1, 1, 0, 0, 68, 0, 1, 1, 2, 0, 16, 0, 1, 2, -1, 1, 64, 0, 1, 2, 0, 0, 96, 0, 1,
    2, 1, 0, 32, 0, 1, 2, 2, 0, 16, 1, -2, 1, 0, 0, 128, 1, -1, 1, 1, 0, 136, 1, 0, 1, 2, 0, 8, 1, 0, 2, -1, 0, 64, 1,
    0, 2, 1, 0, 16);
const int VNG_CHOOD[16] = int[16](-1, -1, -1, 0, -1, 1, 0, 1, 1, 1, 1, 0, 1, -1, 0, -1);
#endif
