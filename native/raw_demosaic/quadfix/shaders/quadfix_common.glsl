#ifndef RAWR_QUADFIX_COMMON_GLSL
#define RAWR_QUADFIX_COMMON_GLSL

#include "quadfix_params.glsl"

layout(std430, set = 0, binding = 0) readonly buffer BayerInput { float bayer[]; }
bayerIn;
layout(set = 0, binding = 1, r32f) uniform image2D guideImg;
layout(set = 0, binding = 2, r32f) uniform image2D energyImg;
// Coarse Gabor fields: 15 layers (QF_CIMG/COMP map), RGBA32F holds the 4 CFA
// phases per channel. MUST stay per-phase: a unified full-res demod cannot
// represent per-phase lattice offsets (measured: unified leaves 100% of the
// lattice energy, per-phase leaves 0.1%).
layout(set = 0, binding = 3, rgba32f) uniform image2DArray coarseArr;
layout(set = 0, binding = 4, rgba32f) uniform image2DArray coarseTmpArr;
layout(set = 0, binding = 5, r32f) uniform image2D maskTmpImg;
layout(std430, set = 0, binding = 6) writeonly buffer BayerOutput { float bayer[]; }
bayerOut;

layout(push_constant) uniform Push {
    uint width;   // full-res tile width incl. halo
    uint height;  // full-res tile height incl. halo
}
pc;

const float QF_PI = 3.14159265358979323846f;

// Clamp-to-edge load from the linear Bayer buffer (matches RCD clampRaw;
// the Python oracle uses reflect — the two agree outside the support
// radius of the tile border, which the host halo discards).
float bayerAt(ivec2 p) {
    p = clamp(p, ivec2(0), ivec2(int(pc.width) - 1, int(pc.height) - 1));
    return bayerIn.bayer[p.y * int(pc.width) + p.x];
}

// Phase-preserving neighborhood load: clamp in SAME-COLOR plane coords,
// then reconstruct. Clamping full-res coords directly can flip (x&1,y&1)
// at the border and pull a different-color pixel into the window.
ivec2 phaseClamp(ivec2 p, int dx, int dy) {
    int px = p.x & 1, py = p.y & 1;
    int pw = int(pc.width) >> 1, ph = int(pc.height) >> 1;
    int pj = clamp((p.x >> 1) + dx, 0, pw - 1);
    int pi = clamp((p.y >> 1) + dy, 0, ph - 1);
    return ivec2(2 * pj + px, 2 * pi + py);
}
float bayerPhaseAt(ivec2 p, int dx, int dy) {
    ivec2 q = phaseClamp(p, dx, dy);
    return bayerIn.bayer[q.y * int(pc.width) + q.x];
}
float guidePhaseAt(ivec2 p, int dx, int dy) {
    return imageLoad(guideImg, phaseClamp(p, dx, dy)).r;
}

// Same-color-plane coords of full-res pixel p for its own phase.
void planeCoords(ivec2 p, out int pi, out int pj) {
    pi = p.y >> 1;
    pj = p.x >> 1;
}

// Carrier phase for lattice point (qy,qx) at plane coords (pi,pj).
float carrierPhase(int qy, int qx, int pi, int pj) {
    return 2.0f * QF_PI * (float(qy) * float(pi) * 0.25f + float(qx) * float(pj) * 0.25f);
}

#endif
