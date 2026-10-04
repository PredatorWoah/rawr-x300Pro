// Shared single-frame RAW develop for standalone demosaic and fused tone.
// The caller defines the three descriptor bindings before including this file.
layout(binding = VIDEO_RAW_IMAGE_BINDING, r16ui) uniform readonly uimage2D rawImage;
layout(std430, binding = VIDEO_RAW_BUFFER_BINDING) readonly buffer RawWords { uint words[]; } rawBuffer;
layout(push_constant) uniform VideoRawParams {
    vec4 black;
    vec4 invRange;
    vec4 wb;
    uint width;
    uint height;
    uint outWidth;
    uint outHeight;
    uint cropX;
    uint cropY;
    uint pattern;
    uint stridePixels;
    uint bufferEnabled;
    uint reduceCfa;
    uint lscEnabled;
    uint lscWidth;
    uint lscHeight;
    uint monitorEnabled;
} pc;
#define LSC_BINDING VIDEO_LSC_BINDING
#include "lens_shading.glsl"
#include "cfa_common.glsl"

// Square output workgroup side. An N x N tile needs (N+4)^2 RAW samples at
// full resolution, or (2N+4)^2 for fused 2x reduction, including the 5x5 MHC
// halo. Larger tiles load each sample (and its lens-shading lookup) fewer
// times; the fused RAW-input tonemap keeps 8.
#ifndef VIDEO_TILE
#define VIDEO_TILE 8
#endif
const int kVideoTileMax = 2 * VIDEO_TILE + 4;
shared float cachedRaw[kVideoTileMax * kVideoTileMax];
#ifdef VIDEO_CLIP_STATE
// Only the standalone video stage needs the sensor codes. The fused fast
// path keeps its original shared-memory footprint.
shared uint cachedCode[kVideoTileMax * kVideoTileMax];
#endif

int videoTileSide() { return pc.reduceCfa != 0u ? 2 * VIDEO_TILE + 4 : VIDEO_TILE + 4; }
ivec2 videoTileBase() {
    return ivec2(pc.cropX, pc.cropY) + ivec2(gl_WorkGroupID.xy) *
           (pc.reduceCfa != 0u ? 2 * VIDEO_TILE : VIDEO_TILE);
}
float videoCachedAt(ivec2 q) {
    ivec2 local = q - videoTileBase() + ivec2(2);
    return cachedRaw[local.y * videoTileSide() + local.x];
}

int videoChannelAt(ivec2 q) { return rawrCfaChannelAt(pc.pattern, q); }

// Mirror about the edge pixel; unlike clamping this keeps the CFA phase, so
// the outer rows and columns (visible in Open Gate) demosaic correctly.
ivec2 videoMirrored(ivec2 q) {
    ivec2 last = ivec2(pc.width, pc.height) - 1;
    q = abs(q);
    return min(q, 2 * last - q);
}

uint videoCodeAt(ivec2 q) {
    q = videoMirrored(q);
    uint v;
    if (pc.bufferEnabled != 0u) {
        uint address = uint(q.y) * pc.stridePixels + uint(q.x);
        uint word = rawBuffer.words[address >> 1u];
        v = (address & 1u) == 0u ? (word & 65535u) : (word >> 16u);
    } else {
        v = imageLoad(rawImage, q).r;
    }
    return v;
}

float videoRawAt(ivec2 q, uint code) {
    q = videoMirrored(q);
    int c = videoChannelAt(q);
    float value = clamp((float(code) - pc.black[c]) * pc.invRange[c], 0.0, 1.0);
    return value * lensShadingGain(q, c == 1 || c == 2 ? 1 : c);
}

void videoLoadTile() {
    int side = videoTileSide();
    ivec2 base = videoTileBase();
    for (uint index = gl_LocalInvocationIndex; index < uint(side * side); index += uint(VIDEO_TILE * VIDEO_TILE)) {
        ivec2 q = base + ivec2(int(index % uint(side)) - 2,
                               int(index / uint(side)) - 2);
        uint code = videoCodeAt(q);
        cachedRaw[index] = videoRawAt(q, code);
#ifdef VIDEO_CLIP_STATE
        cachedCode[index] = code;
#endif
    }
    // Every invocation reaches this barrier, including output-edge threads.
    barrier();
}

vec3 videoFullRgb(ivec2 p) {
    int c = videoChannelAt(p);
    float center = videoCachedAt(p);
    float h1 = videoCachedAt(p + ivec2(-1, 0)) + videoCachedAt(p + ivec2(1, 0));
    float v1 = videoCachedAt(p + ivec2(0, -1)) + videoCachedAt(p + ivec2(0, 1));
    float h2 = videoCachedAt(p + ivec2(-2, 0)) + videoCachedAt(p + ivec2(2, 0));
    float v2 = videoCachedAt(p + ivec2(0, -2)) + videoCachedAt(p + ivec2(0, 2));
    float diagonals = videoCachedAt(p + ivec2(-1, -1)) + videoCachedAt(p + ivec2(1, -1)) +
                      videoCachedAt(p + ivec2(-1, 1)) + videoCachedAt(p + ivec2(1, 1));
    // MHC's Laplacian correction overshoots at high-contrast edges and
    // leaves a dotted zipper there. Each interpolated value is clamped to the
    // range of the same-colour samples it was interpolated from.
    if (c == 0 || c == 3) {
        float g = (4.0 * center + 2.0 * (h1 + v1) - (h2 + v2)) * 0.125;
        float opposite = (6.0 * center + 2.0 * diagonals - 1.5 * (h2 + v2)) * 0.125;
        float left = videoCachedAt(p + ivec2(-1, 0)), right = videoCachedAt(p + ivec2(1, 0));
        float up = videoCachedAt(p + ivec2(0, -1)), down = videoCachedAt(p + ivec2(0, 1));
        g = clamp(g, min(min(left, right), min(up, down)), max(max(left, right), max(up, down)));
        float d00 = videoCachedAt(p + ivec2(-1, -1)), d10 = videoCachedAt(p + ivec2(1, -1));
        float d01 = videoCachedAt(p + ivec2(-1, 1)), d11 = videoCachedAt(p + ivec2(1, 1));
        opposite = clamp(opposite, min(min(d00, d10), min(d01, d11)), max(max(d00, d10), max(d01, d11)));
        return max(c == 0 ? vec3(center, g, opposite) : vec3(opposite, g, center), vec3(0.0));
    }
    // H2: edge-directed chroma at green sites. MHC picks the estimate by
    // CFA phase; here the green gradient (from the 4 diagonal greens,
    // already loaded) decides the interpolation direction, and the
    // across-edge channel is predicted by adding the local chroma-vs-green
    // difference to center (zero extra taps: h1/h2/v1/v2 already held).
    float horizontal = (5.0 * center + 4.0 * h1 - diagonals - h2 + 0.5 * v2) * 0.125;
    float vertical = (5.0 * center + 4.0 * v1 - diagonals - v2 + 0.5 * h2) * 0.125;
    float left = videoCachedAt(p + ivec2(-1, 0)), right = videoCachedAt(p + ivec2(1, 0));
    float up = videoCachedAt(p + ivec2(0, -1)), down = videoCachedAt(p + ivec2(0, 1));
    float gL = 0.5 * (videoCachedAt(p + ivec2(-1, -1)) + videoCachedAt(p + ivec2(-1, 1)));
    float gR = 0.5 * (videoCachedAt(p + ivec2(1, -1)) + videoCachedAt(p + ivec2(1, 1)));
    float gU = 0.5 * (videoCachedAt(p + ivec2(-1, -1)) + videoCachedAt(p + ivec2(1, -1)));
    float gD = 0.5 * (videoCachedAt(p + ivec2(-1, 1)) + videoCachedAt(p + ivec2(1, 1)));
    bool interpH = abs(gL - gR) <= abs(gU - gD);
    // Along-axis estimates stay MHC; across-axis uses the G-guided form.
    float rowOut = interpH ? horizontal : center + 0.5 * (h1 - h2);
    float colOut = interpH ? center + 0.5 * (v1 - v2) : vertical;
    rowOut = clamp(rowOut, min(left, right), max(left, right));
    colOut = clamp(colOut, min(up, down), max(up, down));
    bool redHorizontal = videoChannelAt(p + ivec2(1, 0)) == 0;
    return max(redHorizontal ? vec3(rowOut, center, colOut)
                             : vec3(colOut, center, rowOut), vec3(0.0));
}

vec3 videoSensorAt(ivec2 outPixel) {
    ivec2 crop = ivec2(pc.cropX, pc.cropY);
    vec3 sensor;
    if (pc.reduceCfa != 0u) {
        ivec2 p = crop + outPixel * 2;
        sensor = 0.25 * (videoFullRgb(p) + videoFullRgb(p + ivec2(1, 0)) +
                         videoFullRgb(p + ivec2(0, 1)) + videoFullRgb(p + ivec2(1, 1)));
    } else {
        sensor = videoFullRgb(crop + outPixel);
    }
    return sensor;
}

vec3 videoCameraAt(ivec2 outPixel) {
    float greenWb = 0.5 * (pc.wb.y + pc.wb.z);
    return videoSensorAt(outPixel) * vec3(pc.wb.x, greenWb, pc.wb.w);
}
