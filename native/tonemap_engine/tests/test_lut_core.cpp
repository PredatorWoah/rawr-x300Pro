#include <algorithm>
#include <cmath>
#include <fstream>
#include <iostream>
#include <random>
#include <stdexcept>

#include "tonemap/color/ColorSpace.h"
#include "tonemap/DirectRender.h"
#include "tonemap/lut/Lut3D.h"
#include "tonemap/lut/LutChain.h"
#include "tonemap/lut/LutGpuPayload.h"
#include "../src/GamutConversions.h"
using namespace tonemap;
static void req(bool x, const char* m) {
    if (!x) throw std::runtime_error(m);
}
static float max3(color::Vec3 a, color::Vec3 b) {
    return std::max({std::abs(a[0] - b[0]), std::abs(a[1] - b[1]), std::abs(a[2] - b[2])});
}
int main(int argc, char** argv) {
    using namespace color;
    // All 64 gamut pairs, signed/HDR values, and all tetrahedral input orderings.
    // Compare composed matrices against the original shader's two matvecs.
    auto apply = [](const auto& matrix, int stride, Vec3 input) {
        Vec3 result{};
        for (int row = 0; row < 3; ++row)
            for (int col = 0; col < 3; ++col) result[row] += matrix[col * stride + row] * input[col];
        return result;
    };
    std::mt19937 matrixRandom(421);
    std::uniform_real_distribution<float> matrixValues(-2, 32);
    for (uint32_t source = 0; source < 8; ++source) {
        for (uint32_t destination = 0; destination < 8; ++destination) {
            const auto src = static_cast<Gamut>(source), dst = static_cast<Gamut>(destination);
            const auto direct = detail::directGamutMatrix(src, dst);
            for (int sample = 0; sample < 2048; ++sample) {
                const Vec3 input{matrixValues(matrixRandom), matrixValues(matrixRandom), matrixValues(matrixRandom)};
                const auto reference = src == dst ? input : apply(detail::fromSrgb(dst), 3, apply(detail::toSrgb(src), 3, input));
                const auto actual = apply(direct, 4, input);
                for (int channel = 0; channel < 3; ++channel)
                    req(std::abs(actual[channel] - reference[channel]) <= 1e-5f + 1e-5f * std::abs(reference[channel]),
                        "composed shader gamut matrix parity");
                if (src == dst) req(actual == input, "identical gamut must remain exact");
            }
        }
    }
    std::cout << "SHADER_GAMUT_MATRIX_PARITY_PASS pairs=64 samplesPerPair=2048\n";
    for (float x : {-0.01f, 0.0f, 0.001f, 0.18f, 1.0f, 16.0f}) {
        float y = encodeTransfer(x, TransferFunction::DaVinciIntermediate);
        float z = decodeTransfer(y, TransferFunction::DaVinciIntermediate);
        req(std::abs(z - x) < 2e-5f, "DaVinci Intermediate roundtrip");
        float r = encodeTransfer(x, TransferFunction::Rec2020);
        float q = decodeTransfer(r, TransferFunction::Rec2020);
        req(std::abs(q - x) < 2e-5f, "Rec.2020 roundtrip");
    }
    for (float x : {0.0f, 0.001f, 0.18f, 1.0f}) {
        float y = encodeTransfer(x, TransferFunction::SRgb);
        req(std::abs(decodeTransfer(y, TransferFunction::SRgb) - x) < 2e-6f, "sRGB roundtrip");
    }
    req(std::abs(encodeTransfer(0.018f, TransferFunction::Rec709) - 0.08124794f) < 1e-7f, "BT.709 breakpoint oracle");
    req(std::abs(encodeTransfer(0.18f, TransferFunction::Rec709) - 0.40900773f) < 1e-7f, "BT.709 middle gray oracle");
    req(std::abs(encodeTransfer(0.001f, TransferFunction::Rec709) - 0.0045f) < 1e-8f, "BT.709 toe oracle");
    for (float x : {0.0f, 0.001f, 0.017f, 0.018f, 0.18f, 1.0f})
        req(std::abs(decodeTransfer(encodeTransfer(x, TransferFunction::Rec709), TransferFunction::Rec709) - x) < 2e-6f, "BT.709 roundtrip");
    auto direct = TonemapPresets::NeutralBaseline().params;
    direct.renderTransform = RenderTransform::SRgb;
    req(max3(renderDirectReference({16,16,16}, direct), Vec3{1,1,1}) < 1e-6f, "direct SDR clips highlights");
    req(renderDirectReference({-1,-1,-1}, direct) == Vec3{0,0,0}, "direct SDR clips negative output");
    direct.renderTransform = RenderTransform::Log;
    for (const ColorSpace space : {ColorSpace{Gamut::ArriWideGamut3, TransferFunction::LogC3},
            ColorSpace{Gamut::SonySGamut3Cine, TransferFunction::SLog3},
            ColorSpace{Gamut::PanasonicVGamut, TransferFunction::VLog},
            ColorSpace{Gamut::FujifilmFGamutC, TransferFunction::FLog2C},
            ColorSpace{Gamut::DaVinciWideGamut, TransferFunction::DaVinciIntermediate}}) {
        direct.outputSpace = space;
        const auto middle = renderDirectReference({.18f,.18f,.18f}, direct);
        const auto high = renderDirectReference({4,4,4}, direct);
        req(high[0] > middle[0] && high[0] < 1, "LOG retains scene highlights");
        auto edited = direct;
        edited.exposureEV = 3; edited.saturation = -100; edited.vibrance = 100;
        edited.highlightBiasEV = -100; edited.contrast = 100;
        req(renderDirectReference({.18f,.18f,.18f}, edited) == middle, "LOG ignores creative controls");
    }
    lut::LutGpuPayload ordinaryPayload;
    ordinaryPayload.stageCount = 1;
    ordinaryPayload.rgbaTexels = {{{0, 0.5f, 1, 0}}};
    req(detail::GamutConversions(ordinaryPayload).combineUser, "ordinary custom gamut composition");
    ordinaryPayload.outputSpace.transfer = TransferFunction::LogC3;
    req(!detail::GamutConversions(ordinaryPayload).combineUser, "log output arithmetic fallback");
    ordinaryPayload.enabled = true;
    ordinaryPayload.intensity = 1;
    ordinaryPayload.placement = lut::LutPlacement::PostRender;
    req(!detail::GamutConversions(ordinaryPayload).combineNeutral, "sensitive post-render Neutral fallback");
    ordinaryPayload.intensity = 0;
    req(detail::GamutConversions(ordinaryPayload).combineNeutral, "zero intensity exact Neutral bypass");
    ordinaryPayload.intensity = 1;
    ordinaryPayload.outputSpace.transfer = TransferFunction::SRgb;
    ordinaryPayload.stages[0].domainMin[0] = -1;
    req(!detail::GamutConversions(ordinaryPayload).combineUser, "custom domain arithmetic fallback");
    ordinaryPayload.stages[0].domainMin[0] = 0;
    ordinaryPayload.rgbaTexels[0][1] = 1.2f;
    req(!detail::GamutConversions(ordinaryPayload).combineUser, "extended LUT arithmetic fallback");
    // Manufacturer-spec oracle anchors. These are deliberately not round-trip tests:
    // they pin Rawr to published ARRI/Sony/Panasonic definitions.
    req(std::abs(encodeTransfer(0.0f, TransferFunction::LogC3) - 0.092809f) < 2e-7f, "ARRI LogC3 EI800 black oracle");
    req(std::abs(encodeTransfer(0.18f, TransferFunction::LogC3) - 0.39100683f) < 2e-6f,
        "ARRI LogC3 EI800 18pct oracle");
    req(std::abs(encodeTransfer(0.18f, TransferFunction::SLog3) - (420.0f / 1023.0f)) < 2e-7f,
        "Sony S-Log3 18pct CV420 oracle");
    req(std::abs(encodeTransfer(0.90f, TransferFunction::SLog3) - (598.0f / 1023.0f)) < 6e-4f,
        "Sony S-Log3 90pct CV598 oracle");
    req(std::abs(encodeTransfer(0.0f, TransferFunction::VLog) - (128.0f / 1023.0f)) < 3e-4f,
        "Panasonic V-Log black CV128 oracle");
    req(std::abs(encodeTransfer(0.18f, TransferFunction::VLog) - (433.0f / 1023.0f)) < 6e-4f,
        "Panasonic V-Log 18pct CV433 oracle");
    req(std::abs(encodeTransfer(0.90f, TransferFunction::VLog) - (602.0f / 1023.0f)) < 6e-4f,
        "Panasonic V-Log 90pct CV602 oracle");
    req(std::abs(encodeTransfer(0.0f, TransferFunction::FLog2C) - (95.0f / 1023.0f)) < 2e-7f,
        "FUJIFILM F-Log2 C black CV95 oracle");
    req(std::abs(encodeTransfer(0.18f, TransferFunction::FLog2C) - (400.0f / 1023.0f)) < 6e-7f,
        "FUJIFILM F-Log2 C 18pct CV400 oracle");
    req(std::abs(encodeTransfer(0.90f, TransferFunction::FLog2C) - (570.0f / 1023.0f)) < 6e-4f,
        "FUJIFILM F-Log2 C 90pct CV570 oracle");

    for (auto tf : {TransferFunction::Gamma22, TransferFunction::Gamma24, TransferFunction::LogC3,
                    TransferFunction::SLog3, TransferFunction::VLog, TransferFunction::FLog2C}) {
        for (float x : {-0.005f, 0.0f, 0.000889f, 0.005f, 0.01125f, 0.18f, 1.0f, 4.0f}) {
            const float y = encodeTransfer(x, tf);
            const float z = decodeTransfer(y, tf);
            req(std::isfinite(y) && std::isfinite(z) && std::abs(z - x) < 8e-5f, "new transfer roundtrip");
        }
    }
    // Manufacturer gamut oracles: ARRI and Panasonic publish direct linear-to-Rec.709
    // matrices; Sony and Fujifilm publish primaries, from which their D65 matrices are derived.
    const Mat3 arriOfficial{1.617523f,  -0.537287f, -0.080237f, -0.070573f, 1.334613f,
                            -0.264040f, -0.021102f, -0.226954f, 1.248056f};
    const Mat3 sonyFromOfficialPrimaries{1.62694741f,  -0.54013854f, -0.08680887f, -0.17851553f, 1.41794093f,
                                         -0.23942540f, -0.04443612f, -0.19591997f, 1.24035608f};
    const Mat3 panaOfficial{1.806576f,  -0.695697f, -0.110879f, -0.170090f, 1.305955f,
                            -0.135865f, -0.025206f, -0.154468f, 1.179674f};
    const Mat3 fujiFromPublishedPrimaries{2.11985147f,  -1.07570505f, -0.04414642f, -0.23033585f, 1.37244215f,
                                          -0.14210630f, -0.01422743f, -0.15022499f, 1.16445242f};
    for (auto [g, oracle] :
         {std::pair{Gamut::ArriWideGamut3, arriOfficial}, std::pair{Gamut::SonySGamut3Cine, sonyFromOfficialPrimaries},
          std::pair{Gamut::PanasonicVGamut, panaOfficial},
          std::pair{Gamut::FujifilmFGamutC, fujiFromPublishedPrimaries}}) {
        auto actual = linearGamutMatrix(g, Gamut::SRgbRec709);
        for (int k = 0; k < 9; ++k) req(std::abs(actual[k] - oracle[k]) < 1.5e-6f, "manufacturer gamut matrix oracle");
    }

    for (auto g : {Gamut::ArriWideGamut3, Gamut::SonySGamut3Cine, Gamut::PanasonicVGamut, Gamut::FujifilmFGamutC}) {
        auto a = linearGamutMatrix(g, Gamut::SRgbRec709), b = linearGamutMatrix(Gamut::SRgbRec709, g);
        Vec3 q{0.2f, 0.5f, 0.8f};
        auto rr = applyMatrix(b, applyMatrix(a, q));
        req(max3(q, rr) < 5e-5f, "new gamut roundtrip");
        auto w = applyMatrix(a, {1.0f, 1.0f, 1.0f});
        req(max3(w, {1.0f, 1.0f, 1.0f}) < 8e-5f, "new gamut D65 white");
    }
    auto m = linearGamutMatrix(Gamut::AcesCgAp1, Gamut::DaVinciWideGamut);
    auto inv = linearGamutMatrix(Gamut::DaVinciWideGamut, Gamut::AcesCgAp1);
    auto r2s = linearGamutMatrix(Gamut::Rec2020, Gamut::SRgbRec709);
    auto s2r = linearGamutMatrix(Gamut::SRgbRec709, Gamut::Rec2020);
    auto white = applyMatrix(r2s, {1.0f, 1.0f, 1.0f});
    req(std::abs(white[0] - 1.0f) < 3e-5f && std::abs(white[1] - 1.0f) < 3e-5f && std::abs(white[2] - 1.0f) < 3e-5f,
        "Rec.2020 D65 white");
    auto probe = applyMatrix(s2r, {0.2f, 0.5f, 0.8f});
    probe = applyMatrix(r2s, probe);
    req(std::abs(probe[0] - 0.2f) < 3e-5f && std::abs(probe[1] - 0.5f) < 3e-5f && std::abs(probe[2] - 0.8f) < 3e-5f,
        "Rec.2020 gamut roundtrip");
    Vec3 v{0.21f, 0.37f, 0.09f};
    auto back = applyMatrix(inv, applyMatrix(m, v));
    for (int c = 0; c < 3; ++c) req(std::abs(back[c] - v[c]) < 2e-5f, "gamut roundtrip");

    const char* tmp = "/tmp/tonemap_identity_2.cube";
    std::ofstream f(tmp);
    f << "LUT_3D_SIZE 2\nDOMAIN_MIN 0 0 0\nDOMAIN_MAX 1 1 1\n";
    for (int b = 0; b < 2; ++b)
        for (int g = 0; g < 2; ++g)
            for (int r = 0; r < 2; ++r) f << r << " " << g << " " << b << "\n";
    f.close();
    auto identity = lut::parseCubeFile(tmp);
    for (Vec3 q : {Vec3{0.1f, 0.2f, 0.3f}, Vec3{0.8f, 0.4f, 0.7f}}) {
        auto o = identity.sampleTetrahedral(q);
        for (int c = 0; c < 3; ++c) req(std::abs(o[c] - q[c]) < 1e-6f, "tetra identity");
    }
    lut::LutChain single;
    single.stages.push_back(identity);
    auto co = single.evaluateStages(v);
    for (int c = 0; c < 3; ++c) req(std::abs(co[c] - v[c]) < 1e-6f, "single LUT chain");

    auto disabled = lut::packGpuPayload(nullptr);
    req(!disabled.enabled && disabled.rgbaTexels.size() == 1, "disabled GPU payload");
    auto onePayload = lut::packGpuPayload(&single);
    req(onePayload.enabled && onePayload.stageCount == 1, "single GPU stage");
    req(max3(lut::evaluatePackedStages(onePayload, v), single.evaluateStages(v)) < 1e-7f, "single packed parity");

    if (argc > 1) {
        lut::LutChain chain;
        chain.inputSpace = {Gamut::DaVinciWideGamut, TransferFunction::DaVinciIntermediate};
        chain.outputSpace = {Gamut::SRgbRec709, TransferFunction::SRgb};
        chain.placement = lut::LutPlacement::RenderTransform;
        for (int i = 1; i < argc; ++i) {
            auto real = lut::parseCubeFile(argv[i]);
            req(real.size == 33, "expected 33 cube");
            chain.stages.push_back(std::move(real));
        }
        req(chain.stages.size() <= lut::kMaxGpuLutStages, "stage count");
        auto payload = lut::packGpuPayload(&chain);
        req(payload.stageCount == chain.stages.size(), "packed stage count");
        uint32_t expectedOffset = 0;
        for (uint32_t i = 0; i < payload.stageCount; ++i) {
            req(payload.stages[i].texelOffset == expectedOffset, "packed offset continuity");
            expectedOffset += 33u * 33u * 33u;
        }
        std::mt19937 rng(0x52415752u);
        std::uniform_real_distribution<float> u(0.0f, 1.0f);
        float worst = 0.0f;
        for (int i = 0; i < 2000; ++i) {
            Vec3 q{u(rng), u(rng), u(rng)};
            worst = std::max(worst, max3(chain.evaluateStages(q), lut::evaluatePackedStages(payload, q)));
        }
        req(worst < 5e-6f, "packed four-LUT parity");
        std::cout << "LUT_PACKED_PARITY max_abs=" << worst << " stages=" << payload.stageCount
                  << " texels=" << payload.rgbaTexels.size() << "\n";
    }
    std::cout << "TONEMAP_LUT_CORE_PASS\n";
}
