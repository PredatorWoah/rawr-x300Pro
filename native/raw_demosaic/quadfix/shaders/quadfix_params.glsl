#ifndef RAWR_QUADFIX_PARAMS_GLSL
#define RAWR_QUADFIX_PARAMS_GLSL

// Frozen adapt004 parameters. Single source of truth is
// tools/gen_params.py (scipy kernel semantics); this file and
// include/quadfix/QuadfixParams.h (for the host CPU test) are its
// checked-in outputs. DO NOT hand-edit.

// --- scalar thresholds (0..255 DN working domain) ---
const float QF_E_FLAT = 1.5f;
const float QF_E_TEX = 5.0f;
const float QF_E_RANGE = 3.5f;  // E_TEX - E_FLAT

// --- mask smooth: Gaussian sigma 6, radius 24 (scipy truncate int(4s+0.5)) ---
const int QF_MASK_R = 24;
const float QF_MASK_W[49] = float[49](
    2.230600796e-05f, 4.284692306e-05f, 8.004859079e-05f, 1.454534432e-04f, 2.570576816e-04f,
    4.418485466e-04f, 7.386735350e-04f, 1.201068875e-03f, 1.899413630e-03f, 2.921510504e-03f,
    4.370504970e-03f, 6.359047209e-03f, 8.998885879e-03f, 1.238573272e-02f, 1.658024439e-02f,
    2.158720501e-02f, 2.733620160e-02f, 3.366791218e-02f, 4.033020849e-02f, 4.698735656e-02f,
    5.324365040e-02f, 5.868010710e-02f, 6.289994082e-02f, 6.557613864e-02f, 6.649327259e-02f,
    6.557613864e-02f, 6.289994082e-02f, 5.868010710e-02f, 5.324365040e-02f, 4.698735656e-02f,
    4.033020849e-02f, 3.366791218e-02f, 2.733620160e-02f, 2.158720501e-02f, 1.658024439e-02f,
    1.238573272e-02f, 8.998885879e-03f, 6.359047209e-03f, 4.370504970e-03f, 2.921510504e-03f,
    1.899413630e-03f, 1.201068875e-03f, 7.386735350e-04f, 4.418485466e-04f, 2.570576816e-04f,
    1.454534432e-04f, 8.004859079e-05f, 4.284692306e-05f, 2.230600796e-05f);

// --- coarse Gabor lowpass: Gaussian sigma 39.7887/8 = 4.97359, radius 20 ---
const int QF_COARSE_R = 20;
const float QF_COARSE_W[41] = float[41](
    2.471193370e-05f, 5.435797383e-05f, 1.148320308e-04f, 2.329732644e-04f, 4.539336518e-04f,
    8.494189182e-04f, 1.526492707e-03f, 2.634576375e-03f, 4.366868431e-03f, 6.951406351e-03f,
    1.062719135e-02f, 1.560298098e-02f, 2.200087293e-02f, 2.979308675e-02f, 3.874666648e-02f,
    4.839454854e-02f, 5.804994276e-02f, 6.687293798e-02f, 7.398475954e-02f, 7.860992544e-02f,
    8.021503123e-02f, 7.860992544e-02f, 7.398475954e-02f, 6.687293798e-02f, 5.804994276e-02f,
    4.839454854e-02f, 3.874666648e-02f, 2.979308675e-02f, 2.200087293e-02f, 1.560298098e-02f,
    1.062719135e-02f, 6.951406351e-03f, 4.366868431e-03f, 2.634576375e-03f, 1.526492707e-03f,
    8.494189182e-04f, 4.539336518e-04f, 2.329732644e-04f, 1.148320308e-04f, 5.435797383e-05f,
    2.471193370e-05f);

// --- Gabor carrier table: 15 real channels over the 4x4 lattice ----------
// Channels 0..11: one entry per conjugate-pair half (Re then Im), factor 2.
// Channels 12..14: self-conjugate Nyquist carriers (real only), factor 1.
// Storage (see demod/blend): coarse layer k holds channel k; the RGBA
// components hold the 4 CFA phases (x=py*2+px), estimated independently.
const int QF_NCHAN = 15;
const int QF_CQY[15] = int[15](0, 0, 1, 1, 1, 1, 1, 1, 1, 1, 2, 2, 0, 2, 2);
const int QF_CQX[15] = int[15](1, 1, 0, 0, 1, 1, 3, 3, 2, 2, 1, 1, 2, 0, 2);
const int QF_CSIM[15] = int[15](0, 1, 0, 1, 0, 1, 0, 1, 0, 1, 0, 1, 0, 0, 0);
const float QF_CFAC[15] =
    float[15](2.0f, 2.0f, 2.0f, 2.0f, 2.0f, 2.0f, 2.0f, 2.0f, 2.0f, 2.0f, 2.0f, 2.0f, 1.0f, 1.0f, 1.0f);

// 8x8 demod block in plane coords; coarse grid is (W/16)x(H/16).
const int QF_DS = 8;

#endif
