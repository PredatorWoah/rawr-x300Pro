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
    // H5 = H1 directional green + H3 soft limits. Baseline phase-selected
    // chroma at green sites.
    if (c == 0 || c == 3) {
        float left = videoCachedAt(p + ivec2(-1, 0)), right = videoCachedAt(p + ivec2(1, 0));
        float up = videoCachedAt(p + ivec2(0, -1)), down = videoCachedAt(p + ivec2(0, 1));
        float gh = 0.5 * (h1) + 0.25 * (2.0 * center - h2);
        float gv = 0.5 * (v1) + 0.25 * (2.0 * center - v2);
        float dh = abs(left - right) + abs(2.0 * center - h2);
        float dv = abs(up - down) + abs(2.0 * center - v2);
        float wsum = dh + dv;
        float gIso = wsum > 1e-6 ? (dv * gh + dh * gv) / wsum : 0.5 * (gh + gv);
        float gMean = 0.25 * (h1 + v1);
        float gAllow = 0.5 * (max(max(left, right), max(up, down)) -
                              min(min(left, right), min(up, down))) + 1e-4;
        float g = gMean + clamp(gIso - gMean, -gAllow, gAllow);
        float oppIso = (6.0 * center + 2.0 * diagonals - 1.5 * (h2 + v2)) * 0.125;
        float oppMean = 0.25 * diagonals;
        float d00 = videoCachedAt(p + ivec2(-1, -1)), d10 = videoCachedAt(p + ivec2(1, -1));
        float d01 = videoCachedAt(p + ivec2(-1, 1)), d11 = videoCachedAt(p + ivec2(1, 1));
        float oppAllow = 0.5 * (max(max(d00, d10), max(d01, d11)) -
                                min(min(d00, d10), min(d01, d11))) + 1e-4;
        float opposite = oppMean + clamp(oppIso - oppMean, -oppAllow, oppAllow);
        return max(c == 0 ? vec3(center, g, opposite) : vec3(opposite, g, center), vec3(0.0));
    }
    float horIso = (5.0 * center + 4.0 * h1 - diagonals - h2 + 0.5 * v2) * 0.125;
    float verIso = (5.0 * center + 4.0 * v1 - diagonals - v2 + 0.5 * h2) * 0.125;
    float left = videoCachedAt(p + ivec2(-1, 0)), right = videoCachedAt(p + ivec2(1, 0));
    float up = videoCachedAt(p + ivec2(0, -1)), down = videoCachedAt(p + ivec2(0, 1));
    float horMean = 0.5 * (left + right);
    float verMean = 0.5 * (up + down);
    float horizontal = horMean + clamp(horIso - horMean, -0.5 * abs(left - right) - 1e-4,
                                       0.5 * abs(left - right) + 1e-4);
    float vertical = verMean + clamp(verIso - verMean, -0.5 * abs(up - down) - 1e-4,
                                     0.5 * abs(up - down) + 1e-4);
    bool redHorizontal = videoChannelAt(p + ivec2(1, 0)) == 0;
    return max(redHorizontal ? vec3(horizontal, center, vertical)
                             : vec3(vertical, center, horizontal), vec3(0.0));
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
