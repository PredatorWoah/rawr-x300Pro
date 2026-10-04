#ifndef RAWR_TONEMAP_LUT3D_GLSL
#define RAWR_TONEMAP_LUT3D_GLSL

uint rawrUserLutIndex(uint stage, ivec3 p) {
    uvec4 meta = P.lutStageIndexSize[stage];
    uint n = meta.y;
    return meta.x + uint(p.x) + n * (uint(p.y) + n * uint(p.z));
}
vec3 rawrUserLutFetch(uint stage, ivec3 p) {
#if RAWR_TONEMAP_USER_TEXTURE
    // Constant indices avoid requiring sampled-image dynamic indexing features.
    if (stage == 0u) return texelFetch(userLutTextures[0], p, 0).rgb;
    if (stage == 1u) return texelFetch(userLutTextures[1], p, 0).rgb;
    if (stage == 2u) return texelFetch(userLutTextures[2], p, 0).rgb;
    if (stage == 3u) return texelFetch(userLutTextures[3], p, 0).rgb;
    if (stage == 4u) return texelFetch(userLutTextures[4], p, 0).rgb;
    if (stage == 5u) return texelFetch(userLutTextures[5], p, 0).rgb;
    if (stage == 6u) return texelFetch(userLutTextures[6], p, 0).rgb;
    return texelFetch(userLutTextures[7], p, 0).rgb;
#else
    return UL.texel[rawrUserLutIndex(stage, p)].rgb;
#endif
}
vec3 rawrSampleUserLutStage(uint stage, vec3 inputValue) {
    uvec4 meta = P.lutStageIndexSize[stage];
    int n = int(meta.y);
    vec3 mn = P.lutDomainMin[stage].rgb;
    vec3 mx = P.lutDomainMax[stage].rgb;
    vec3 z = clamp((inputValue - mn) / (mx - mn), 0.0, 1.0) * float(n - 1);
    ivec3 i0 = ivec3(floor(z));
    ivec3 i1 = min(i0 + ivec3(1), ivec3(n - 1));
    vec3 f = z - vec3(i0);
    vec3 c000 = rawrUserLutFetch(stage, i0);
    vec3 o;
    if (f.r >= f.g) {
        if (f.g >= f.b) {
            vec3 c100 = rawrUserLutFetch(stage, ivec3(i1.x, i0.y, i0.z));
            vec3 c110 = rawrUserLutFetch(stage, ivec3(i1.x, i1.y, i0.z));
            vec3 c111 = rawrUserLutFetch(stage, i1);
            o = c000 + f.r * (c100 - c000) + f.g * (c110 - c100) + f.b * (c111 - c110);
        } else if (f.r >= f.b) {
            vec3 c100 = rawrUserLutFetch(stage, ivec3(i1.x, i0.y, i0.z));
            vec3 c101 = rawrUserLutFetch(stage, ivec3(i1.x, i0.y, i1.z));
            vec3 c111 = rawrUserLutFetch(stage, i1);
            o = c000 + f.r * (c100 - c000) + f.b * (c101 - c100) + f.g * (c111 - c101);
        } else {
            vec3 c001 = rawrUserLutFetch(stage, ivec3(i0.x, i0.y, i1.z));
            vec3 c101 = rawrUserLutFetch(stage, ivec3(i1.x, i0.y, i1.z));
            vec3 c111 = rawrUserLutFetch(stage, i1);
            o = c000 + f.b * (c001 - c000) + f.r * (c101 - c001) + f.g * (c111 - c101);
        }
    } else {
        if (f.b >= f.g) {
            vec3 c001 = rawrUserLutFetch(stage, ivec3(i0.x, i0.y, i1.z));
            vec3 c011 = rawrUserLutFetch(stage, ivec3(i0.x, i1.y, i1.z));
            vec3 c111 = rawrUserLutFetch(stage, i1);
            o = c000 + f.b * (c001 - c000) + f.g * (c011 - c001) + f.r * (c111 - c011);
        } else if (f.b >= f.r) {
            vec3 c010 = rawrUserLutFetch(stage, ivec3(i0.x, i1.y, i0.z));
            vec3 c011 = rawrUserLutFetch(stage, ivec3(i0.x, i1.y, i1.z));
            vec3 c111 = rawrUserLutFetch(stage, i1);
            o = c000 + f.g * (c010 - c000) + f.b * (c011 - c010) + f.r * (c111 - c011);
        } else {
            vec3 c010 = rawrUserLutFetch(stage, ivec3(i0.x, i1.y, i0.z));
            vec3 c110 = rawrUserLutFetch(stage, ivec3(i1.x, i1.y, i0.z));
            vec3 c111 = rawrUserLutFetch(stage, i1);
            o = c000 + f.g * (c010 - c000) + f.r * (c110 - c010) + f.b * (c111 - c110);
        }
    }
    return o;
}
vec3 rawrEvaluateUserLutChain(vec3 inputValue) {
    vec3 outValue = inputValue;
    for (uint stage = 0u; stage < P.lutControl.y; ++stage) outValue = rawrSampleUserLutStage(stage, outValue);
    return outValue;
}
#endif
