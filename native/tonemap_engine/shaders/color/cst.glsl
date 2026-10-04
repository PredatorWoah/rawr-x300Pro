#ifndef RAWR_TONEMAP_CST_GLSL
#define RAWR_TONEMAP_CST_GLSL

const uint RAWR_GAMUT_SRGB_REC709 = 0u;
const uint RAWR_GAMUT_ACESCG_AP1 = 1u;
const uint RAWR_GAMUT_DWG = 2u;
const uint RAWR_GAMUT_REC2020 = 3u;
const uint RAWR_GAMUT_ARRI_WG3 = 4u;
const uint RAWR_GAMUT_SONY_SGAMUT3CINE = 5u;
const uint RAWR_GAMUT_PANASONIC_VGAMUT = 6u;
const uint RAWR_GAMUT_FUJIFILM_FGAMUT_C = 7u;
const uint RAWR_TF_LINEAR = 0u;
const uint RAWR_TF_SRGB = 1u;
const uint RAWR_TF_DAVINCI_INTERMEDIATE = 2u;
const uint RAWR_TF_REC2020 = 3u;
const uint RAWR_TF_GAMMA22 = 4u;
const uint RAWR_TF_GAMMA24 = 5u;
const uint RAWR_TF_LOGC3 = 6u;
const uint RAWR_TF_SLOG3 = 7u;
const uint RAWR_TF_VLOG = 8u;
const uint RAWR_TF_FLOG2C = 9u;

float rawrSrgbDecode1(float x) { return x <= 0.04045 ? x / 12.92 : pow((x + 0.055) / 1.055, 2.4); }
float rawrSrgbEncode1(float x) { return x <= 0.0031308 ? 12.92 * x : 1.055 * pow(max(x, 0.0), 1.0 / 2.4) - 0.055; }
float rawrDiEncode1(float x) {
    const float A = 0.0075, B = 7.0, C = 0.07329248, M = 10.44426855, LIN_CUT = 0.00262409;
    return x <= LIN_CUT ? x * M : (log2(x + A) + B) * C;
}
float rawrDiDecode1(float x) {
    const float A = 0.0075, B = 7.0, C = 0.07329248, M = 10.44426855, LOG_CUT = 0.02740668;
    return x <= LOG_CUT ? x / M : exp2(x / C - B) - A;
}
float rawrRec2020Encode1(float x) {
    const float A = 1.09929682680944, B = 0.018053968510807;
    return x < B ? 4.5 * x : A * pow(max(x, 0.0), 0.45) - (A - 1.0);
}
float rawrRec2020Decode1(float x) {
    const float A = 1.09929682680944, B = 0.018053968510807, CUT = 4.5 * B;
    return x < CUT ? x / 4.5 : pow((x + (A - 1.0)) / A, 1.0 / 0.45);
}
float rawrLogC3Encode1(float x) {
    return x > 0.010591 ? 0.247190 * (log2(5.555556 * x + 0.052272) * 0.3010299956639812) + 0.385537
                        : 5.367655 * x + 0.092809;
}
float rawrLogC3Decode1(float x) {
    return x > 0.1496582 ? (pow(10.0, (x - 0.385537) / 0.247190) - 0.052272) / 5.555556 : (x - 0.092809) / 5.367655;
}
float rawrSLog3Encode1(float x) {
    return x >= 0.01125 ? (420.0 + (log2((x + 0.01) / 0.19) * 0.3010299956639812) * 261.5) / 1023.0
                        : (x * (171.2102946929 - 95.0) / 0.01125 + 95.0) / 1023.0;
}
float rawrSLog3Decode1(float x) {
    const float cut = 171.2102946929 / 1023.0;
    return x >= cut ? pow(10.0, (x * 1023.0 - 420.0) / 261.5) * 0.19 - 0.01
                    : (x * 1023.0 - 95.0) * 0.01125 / (171.2102946929 - 95.0);
}
float rawrVLogEncode1(float x) {
    return x < 0.01 ? 5.6 * x + 0.125 : 0.241514 * (log2(x + 0.00873) * 0.3010299956639812) + 0.598206;
}
float rawrVLogDecode1(float x) {
    return x < 0.181 ? (x - 0.125) / 5.6 : pow(10.0, (x - 0.598206) / 0.241514) - 0.00873;
}
float rawrFLog2CEncode1(float x) {
    const float A = 5.555556, B = 0.064829, C = 0.245281, D = 0.384316;
    const float E = 8.799461, F = 0.092864, LIN_CUT = 0.000889;
    return x >= LIN_CUT ? C * (log2(A * x + B) * 0.3010299956639812) + D : E * x + F;
}
float rawrFLog2CDecode1(float x) {
    const float A = 5.555556, B = 0.064829, C = 0.245281, D = 0.384316;
    const float E = 8.799461, F = 0.092864, LOG_CUT = 0.100686685370811;
    return x >= LOG_CUT ? (pow(10.0, (x - D) / C) - B) / A : (x - F) / E;
}
#define RAWR_TF_REC709 10u

float rawrRec709Encode1(float x) {
    return x < 0.018 ? 4.5 * x : 1.099 * pow(max(x, 0.0), 0.45) - 0.099;
}
float rawrRec709Decode1(float x) {
    return x < 0.081 ? x / 4.5 : pow((x + 0.099) / 1.099, 1.0 / 0.45);
}
float rawrTfDecode1(float x, uint tf) {
    if (tf == RAWR_TF_REC709) return rawrRec709Decode1(x);
    if (tf == RAWR_TF_LINEAR) return x;
    if (tf == RAWR_TF_SRGB) return rawrSrgbDecode1(x);
    if (tf == RAWR_TF_DAVINCI_INTERMEDIATE) return rawrDiDecode1(x);
    if (tf == RAWR_TF_REC2020) return rawrRec2020Decode1(x);
    if (tf == RAWR_TF_GAMMA22) return sign(x) * pow(abs(x), 2.2);
    if (tf == RAWR_TF_GAMMA24) return sign(x) * pow(abs(x), 2.4);
    if (tf == RAWR_TF_LOGC3) return rawrLogC3Decode1(x);
    if (tf == RAWR_TF_SLOG3) return rawrSLog3Decode1(x);
    if (tf == RAWR_TF_FLOG2C) return rawrFLog2CDecode1(x);
    return rawrVLogDecode1(x);
}
float rawrTfEncode1(float x, uint tf) {
    if (tf == RAWR_TF_REC709) return rawrRec709Encode1(x);
    if (tf == RAWR_TF_LINEAR) return x;
    if (tf == RAWR_TF_SRGB) return rawrSrgbEncode1(x);
    if (tf == RAWR_TF_DAVINCI_INTERMEDIATE) return rawrDiEncode1(x);
    if (tf == RAWR_TF_REC2020) return rawrRec2020Encode1(x);
    if (tf == RAWR_TF_GAMMA22) return sign(x) * pow(abs(x), 1.0 / 2.2);
    if (tf == RAWR_TF_GAMMA24) return sign(x) * pow(abs(x), 1.0 / 2.4);
    if (tf == RAWR_TF_LOGC3) return rawrLogC3Encode1(x);
    if (tf == RAWR_TF_SLOG3) return rawrSLog3Encode1(x);
    if (tf == RAWR_TF_FLOG2C) return rawrFLog2CEncode1(x);
    return rawrVLogEncode1(x);
}
vec3 rawrDecodeTransfer(vec3 x, uint tf) {
    return vec3(rawrTfDecode1(x.r, tf), rawrTfDecode1(x.g, tf), rawrTfDecode1(x.b, tf));
}
vec3 rawrEncodeTransfer(vec3 x, uint tf) {
    return vec3(rawrTfEncode1(x.r, tf), rawrTfEncode1(x.g, tf), rawrTfEncode1(x.b, tf));
}

// One coefficient source for shader conversions and CPU matrix composition.
#define RAWR_GAMUT_MATRIX(name, a,b,c,d,e,f,g,h,i) const mat3 name = mat3(a,b,c,d,e,f,g,h,i);
#include "gamut_matrices.inc"
#undef RAWR_GAMUT_MATRIX

vec3 rawrToSrgbLinear(vec3 x, uint src) {
    if (src == RAWR_GAMUT_SRGB_REC709) return x;
    if (src == RAWR_GAMUT_ACESCG_AP1) return RAWR_AP1_TO_SRGB * x;
    if (src == RAWR_GAMUT_DWG) return RAWR_DWG_TO_SRGB * x;
    if (src == RAWR_GAMUT_REC2020) return RAWR_REC2020_TO_SRGB * x;
    if (src == RAWR_GAMUT_ARRI_WG3) return RAWR_ARRI_WG3_TO_SRGB * x;
    if (src == RAWR_GAMUT_SONY_SGAMUT3CINE) return RAWR_SONY_SGAMUT3CINE_TO_SRGB * x;
    if (src == RAWR_GAMUT_FUJIFILM_FGAMUT_C) return RAWR_FUJIFILM_FGAMUT_C_TO_SRGB * x;
    return RAWR_PANASONIC_VGAMUT_TO_SRGB * x;
}
vec3 rawrFromSrgbLinear(vec3 x, uint dst) {
    if (dst == RAWR_GAMUT_SRGB_REC709) return x;
    if (dst == RAWR_GAMUT_ACESCG_AP1) return RAWR_SRGB_TO_AP1 * x;
    if (dst == RAWR_GAMUT_DWG) return RAWR_SRGB_TO_DWG * x;
    if (dst == RAWR_GAMUT_REC2020) return RAWR_SRGB_TO_REC2020 * x;
    if (dst == RAWR_GAMUT_ARRI_WG3) return RAWR_SRGB_TO_ARRI_WG3 * x;
    if (dst == RAWR_GAMUT_SONY_SGAMUT3CINE) return RAWR_SRGB_TO_SONY_SGAMUT3CINE * x;
    if (dst == RAWR_GAMUT_FUJIFILM_FGAMUT_C) return RAWR_SRGB_TO_FUJIFILM_FGAMUT_C * x;
    return RAWR_SRGB_TO_PANASONIC_VGAMUT * x;
}
vec3 rawrConvertLinearGamut(vec3 x, uint src, uint dst) {
    if (src == dst) return x;
    return rawrFromSrgbLinear(rawrToSrgbLinear(x, src), dst);
}
vec3 rawrCst(vec3 rgb, uint srcGamut, uint srcTf, uint dstGamut, uint dstTf) {
    if (srcGamut == dstGamut && srcTf == dstTf) return rgb;
    vec3 lin = rawrDecodeTransfer(rgb, srcTf);
    lin = rawrConvertLinearGamut(lin, srcGamut, dstGamut);
    return rawrEncodeTransfer(lin, dstTf);
}
#endif
