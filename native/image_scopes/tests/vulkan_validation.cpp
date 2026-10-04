#include <algorithm>
#include <array>
#include <cstring>
#include <iostream>
#include <stdexcept>
#include <utility>
#include <vector>

#include "image_scopes/image_scopes.h"
#include "image_scopes_cpu.h"
#include "layouts.hpp"
#include "vulkan_test_context.hpp"
using namespace image_scopes;
namespace t = image_scopes::tests;
static void require(bool v, const char* m) {
    if (!v) throw std::runtime_error(m);
}
static bool hasSignal(const std::vector<std::uint8_t>& rgba, std::uint8_t threshold = 40) {
    for (size_t i = 0; i + 3 < rgba.size(); i += 4)
        if (rgba[i] > threshold || rgba[i + 1] > threshold || rgba[i + 2] > threshold) return true;
    return false;
}
static bool hasDominant(const std::vector<std::uint8_t>& rgba, unsigned channel, std::uint8_t threshold = 150) {
    for (size_t i = 0; i + 3 < rgba.size(); i += 4) {
        const auto v = rgba[i + channel];
        if (v > threshold && v >= rgba[i + (channel + 1) % 3] && v >= rgba[i + (channel + 2) % 3]) return true;
    }
    return false;
}
static std::vector<std::uint8_t> pattern(std::uint32_t w, std::uint32_t h) {
    std::vector<std::uint8_t> a(size_t(w) * h * 4);
    for (std::uint32_t y = 0; y < h; ++y)
        for (std::uint32_t x = 0; x < w; ++x) {
            auto* p = &a[(size_t(y) * w + x) * 4];
            p[0] = std::uint8_t((x * 17u + y * 3u) & 255u);
            p[1] = std::uint8_t((x * 5u + y * 29u) & 255u);
            p[2] = std::uint8_t(((x ^ y) * 11u) & 255u);
            p[3] = 255;
        }
    return a;
}
int main() {
    std::cout.setf(std::ios::unitbuf);
    std::cerr.setf(std::ios::unitbuf);
    try {
        t::VulkanTestContext vk;
        std::cout << "Using GPU: " << vk.deviceName() << "\n";
        constexpr std::uint32_t W = 63, H = 65;
        auto px = pattern(W, H);
        auto im = vk.createRgbaImage(W, H);
        vk.uploadRgba(im, px);
        {
            bool rejected = false;
            try {
                ImageScopes tooMany({{vk.physicalDevice(), vk.device(), nullptr}, kMaxFramesInFlight + 1u});
            } catch (const std::invalid_argument&) {
                rejected = true;
            }
            require(rejected, "excessive maxFramesInFlight accepted");
        }
        ImageScopes scopes({{vk.physicalDevice(), vk.device(), nullptr}, 3});
        DisplayImageView iv{im.view, VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_LAYOUT_GENERAL, W, H};

        // Public-boundary hardening: invalid sampling and unsupported presentation
        // controls must fail before GPU work is recorded.
        {
            vk.beginCommands();
            bool rejected = false;
            VectorscopeRecordInfo bad{};
            bad.commandBuffer = vk.commandBuffer();
            bad.input = iv;
            bad.renderEnabled = false;
            bad.samplingMode = static_cast<SamplingMode>(999u);
            try {
                scopes.recordVectorscope(bad);
            } catch (const std::invalid_argument&) {
                rejected = true;
            }
            require(rejected, "invalid vectorscope sampling mode accepted");
            vk.submitAndWait();
            scopes.retireFrameSlot(0);
        }
        {
            vk.beginCommands();
            bool rejected = false;
            VectorscopeRecordInfo bad{};
            bad.commandBuffer = vk.commandBuffer();
            bad.input = iv;
            bad.renderEnabled = false;
            bad.input.width = 0xffffffffu;
            bad.input.height = 2u;
            try {
                scopes.recordVectorscope(bad);
            } catch (const std::invalid_argument&) {
                rejected = true;
            }
            require(rejected, "overflowing vectorscope source pixel count accepted");
            vk.submitAndWait();
            scopes.retireFrameSlot(0);
        }
        {
            auto rejectRender = [&](const VectorscopeRenderParams& rp, const char* msg) {
                vk.beginCommands();
                bool rejected = false;
                VectorscopeRecordInfo bad{};
                bad.commandBuffer = vk.commandBuffer();
                bad.input = iv;
                bad.renderEnabled = true;
                bad.renderTarget = {im.view, VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_LAYOUT_GENERAL, W, H};
                bad.render = rp;
                try {
                    scopes.recordVectorscope(bad);
                } catch (const std::invalid_argument&) {
                    rejected = true;
                }
                require(rejected, msg);
                vk.submitAndWait();
                scopes.retireFrameSlot(0);
            };
            VectorscopeRenderParams p{};
            p.densityScale = DensityScale::Linear;
            rejectRender(p, "unsupported vectorscope densityScale accepted");
            p = {};
            p.pointSpreadBins = 2u;
            rejectRender(p, "unsupported vectorscope pointSpreadBins accepted");
            p = {};
            p.opacity = 1.1f;
            rejectRender(p, "out-of-range vectorscope opacity accepted");
            p = {};
            p.densityGain = -1.0f;
            rejectRender(p, "negative vectorscope densityGain accepted");
        }
        std::cout << "VECTORSCOPE_PUBLIC_BOUNDARY_PASS\n";
        // A public vectorscope view is meaningful only after a measurement has
        // actually retired. Slot 2 is intentionally untouched here.
        {
            bool rejected = false;
            try {
                (void)scopes.vectorscopeView(2);
            } catch (const std::logic_error&) {
                rejected = true;
            }
            require(rejected, "vectorscopeView exposed metadata before first retired measurement");
        }
        std::cout << "VECTORSCOPE_VIEW_VALIDITY_GUARD_PASS\n";
        // Vectorscope recorded state must not become public/valid until retirement.
        vk.beginCommands();
        VectorscopeRecordInfo vecValid256{};
        vecValid256.commandBuffer = vk.commandBuffer();
        vecValid256.input = iv;
        vecValid256.renderEnabled = false;
        vecValid256.grid = VectorscopeGrid::Reference256;
        vecValid256.samplingMode = SamplingMode::FullReference;
        scopes.recordVectorscope(vecValid256);
        vk.submitAndWait();
        scopes.retireFrameSlot(0);
        require(scopes.vectorscopeView(0).sampling.gridWidth == 256u, "retired vectorscope 256 metadata missing");
        vk.beginCommands();
        VectorscopeRecordInfo vecAbandoned128{};
        vecAbandoned128.commandBuffer = vk.commandBuffer();
        vecAbandoned128.input = iv;
        vecAbandoned128.renderEnabled = false;
        vecAbandoned128.grid = VectorscopeGrid::Compact128;
        vecAbandoned128.samplingMode = SamplingMode::Production25;
        scopes.recordVectorscope(vecAbandoned128);
        require(vkEndCommandBuffer(vk.commandBuffer()) == VK_SUCCESS,
                "abandoned vectorscope command buffer end failed");
        scopes.discardFrameSlotRecordings(0);
        require(scopes.vectorscopeView(0).sampling.gridWidth == 256u,
                "abandoned vectorscope recording changed retired public metadata");
        std::cout << "VECTORSCOPE_RECORDED_VS_VALID_STATE_PASS\n";
        auto vecStateTarget = vk.createRgbaImage(W, H);
        ScopeRenderTarget vecStateRt{vecStateTarget.view, VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_LAYOUT_GENERAL, W, H};
        VkCommandBuffer vecSecondCb = VK_NULL_HANDLE;
        VkCommandBufferAllocateInfo vecCbai{};
        vecCbai.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        vecCbai.commandPool = vk.commandPool();
        vecCbai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        vecCbai.commandBufferCount = 1;
        require(vkAllocateCommandBuffers(vk.device(), &vecCbai, &vecSecondCb) == VK_SUCCESS,
                "vectorscope second command buffer allocation failed");
        vk.beginCommands();
        VectorscopeRecordInfo vecPending128{};
        vecPending128.commandBuffer = vk.commandBuffer();
        vecPending128.input = iv;
        vecPending128.renderEnabled = false;
        vecPending128.grid = VectorscopeGrid::Compact128;
        scopes.recordVectorscope(vecPending128);
        VectorscopeRenderInfo vecSameCb{};
        vecSameCb.commandBuffer = vk.commandBuffer();
        vecSameCb.renderTarget = vecStateRt;
        vecSameCb.render.showGrid = false;
        vecSameCb.render.showBackground = false;
        scopes.recordVectorscopeRender(vecSameCb);
        VkCommandBufferBeginInfo vecCbbi{};
        vecCbbi.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        require(vkBeginCommandBuffer(vecSecondCb, &vecCbbi) == VK_SUCCESS,
                "vectorscope second command buffer begin failed");
        VectorscopeRenderInfo vecCrossCb{};
        vecCrossCb.commandBuffer = vecSecondCb;
        vecCrossCb.renderTarget = vecStateRt;
        bool vecCrossRejected = false;
        try {
            scopes.recordVectorscopeRender(vecCrossCb);
        } catch (const std::logic_error&) {
            vecCrossRejected = true;
        }
        require(vecCrossRejected,
                "pending vectorscope overwrite did not suspend old render-only validity across command buffers");
        require(vkEndCommandBuffer(vecSecondCb) == VK_SUCCESS, "vectorscope second command buffer end failed");
        vkFreeCommandBuffers(vk.device(), vk.commandPool(), 1, &vecSecondCb);
        vk.submitAndWait();
        scopes.retireFrameSlot(0);
        require(scopes.vectorscopeView(0).sampling.gridWidth == 128u,
                "retired vectorscope 128 metadata was not promoted");
        require(hasSignal(vk.downloadRgba(vecStateTarget)),
                "same-command-buffer pending vectorscope render produced no output");
        vk.destroyRgbaImage(vecStateTarget);
        std::cout << "VECTORSCOPE_PENDING_SUSPENDS_OLD_VALIDITY_PASS\n";

        // Pending vectorscope renders must normalize with the pending measurement's
        // sample count, never the previously retired metadata. Compare complete
        // rendered pixels so a mere non-empty image cannot hide stale normalization.
        auto vecNormImmediate = vk.createRgbaImage(W, H), vecNormRetired = vk.createRgbaImage(W, H),
             vecNormAfterFull = vk.createRgbaImage(W, H);
        ScopeRenderTarget vecNormImmediateRt{vecNormImmediate.view, VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_LAYOUT_GENERAL,
                                             W, H};
        ScopeRenderTarget vecNormRetiredRt{vecNormRetired.view, VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_LAYOUT_GENERAL, W,
                                           H};
        ScopeRenderTarget vecNormAfterFullRt{vecNormAfterFull.view, VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_LAYOUT_GENERAL,
                                             W, H};
        VectorscopeRenderParams vecNormRender{};
        vecNormRender.showGrid = false;
        vecNormRender.showTargets = false;
        vecNormRender.showBackground = false;
        // Fresh slot: Production25 measure+render.
        vk.beginCommands();
        VectorscopeRecordInfo vecNormA{};
        vecNormA.commandBuffer = vk.commandBuffer();
        vecNormA.input = iv;
        vecNormA.frameSlot = 1;
        vecNormA.samplingMode = SamplingMode::Production25;
        vecNormA.renderEnabled = true;
        vecNormA.renderTarget = vecNormImmediateRt;
        vecNormA.render = vecNormRender;
        scopes.recordVectorscope(vecNormA);
        vk.submitAndWait();
        scopes.retireFrameSlot(1);
        auto vecNormAPixels = vk.downloadRgba(vecNormImmediate);
        // Clean reference: same Production25 measurement, retire, then render-only.
        vk.beginCommands();
        VectorscopeRecordInfo vecNormBMeasure{};
        vecNormBMeasure.commandBuffer = vk.commandBuffer();
        vecNormBMeasure.input = iv;
        vecNormBMeasure.frameSlot = 2;
        vecNormBMeasure.samplingMode = SamplingMode::Production25;
        vecNormBMeasure.renderEnabled = false;
        scopes.recordVectorscope(vecNormBMeasure);
        vk.submitAndWait();
        scopes.retireFrameSlot(2);
        vk.beginCommands();
        VectorscopeRenderInfo vecNormB{};
        vecNormB.commandBuffer = vk.commandBuffer();
        vecNormB.frameSlot = 2;
        vecNormB.renderTarget = vecNormRetiredRt;
        vecNormB.render = vecNormRender;
        scopes.recordVectorscopeRender(vecNormB);
        vk.submitAndWait();
        scopes.retireFrameSlot(2);
        auto vecNormBPixels = vk.downloadRgba(vecNormRetired);
        require(vecNormAPixels == vecNormBPixels,
                "fresh Production25 measure+render normalization differs from retired render-only reference");
        // Reused slot: retire FullReference first, then record Production25 and render
        // it in the same command buffer. The result must still equal clean P25.
        vk.beginCommands();
        VectorscopeRecordInfo vecNormFull{};
        vecNormFull.commandBuffer = vk.commandBuffer();
        vecNormFull.input = iv;
        vecNormFull.frameSlot = 1;
        vecNormFull.samplingMode = SamplingMode::FullReference;
        vecNormFull.renderEnabled = false;
        scopes.recordVectorscope(vecNormFull);
        vk.submitAndWait();
        scopes.retireFrameSlot(1);
        vk.beginCommands();
        VectorscopeRecordInfo vecNormPending{};
        vecNormPending.commandBuffer = vk.commandBuffer();
        vecNormPending.input = iv;
        vecNormPending.frameSlot = 1;
        vecNormPending.samplingMode = SamplingMode::Production25;
        vecNormPending.renderEnabled = false;
        scopes.recordVectorscope(vecNormPending);
        VectorscopeRenderInfo vecNormSameCb{};
        vecNormSameCb.commandBuffer = vk.commandBuffer();
        vecNormSameCb.frameSlot = 1;
        vecNormSameCb.renderTarget = vecNormAfterFullRt;
        vecNormSameCb.render = vecNormRender;
        scopes.recordVectorscopeRender(vecNormSameCb);
        vk.submitAndWait();
        scopes.retireFrameSlot(1);
        auto vecNormCPixels = vk.downloadRgba(vecNormAfterFull);
        require(vecNormCPixels == vecNormBPixels,
                "pending Production25 render reused stale retired FullReference sample count");
        vk.destroyRgbaImage(vecNormImmediate);
        vk.destroyRgbaImage(vecNormRetired);
        vk.destroyRgbaImage(vecNormAfterFull);
        std::cout << "VECTORSCOPE_PENDING_RENDER_NORMALIZATION_PASS\n";

        // Waveform presentation boundary hardening.
        {
            auto rejectWave = [&](const WaveformRenderParams& rp, const char* msg) {
                vk.beginCommands();
                bool rejected = false;
                DisplayWaveformRecordInfo bad{};
                bad.commandBuffer = vk.commandBuffer();
                bad.input = iv;
                bad.render = rp;
                bad.renderEnabled = true;
                bad.renderTarget = {im.view, VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_LAYOUT_GENERAL, W, H};
                try {
                    scopes.recordDisplayWaveform(bad);
                } catch (const std::invalid_argument&) {
                    rejected = true;
                }
                require(rejected, msg);
                vk.submitAndWait();
                scopes.retireFrameSlot(0);
            };
            WaveformRenderParams p{};
            p.mode = static_cast<WaveformMode>(999u);
            rejectWave(p, "unsupported waveform mode accepted");
            p = {};
            p.densityScale = DensityScale::Logarithmic;
            rejectWave(p, "unsupported waveform densityScale accepted");
            p = {};
            p.pointSpreadBins = 2u;
            rejectWave(p, "unsupported waveform pointSpreadBins accepted");
            p = {};
            p.opacity = 1.1f;
            rejectWave(p, "out-of-range waveform opacity accepted");
            p = {};
            p.densityGain = -1.0f;
            rejectWave(p, "negative waveform densityGain accepted");
        }
        std::cout << "WAVEFORM_PUBLIC_BOUNDARY_PASS\n";
        auto host = vk.createBuffer(sizeof(detail::DisplayWaveformStd430), VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                                    VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT, VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
        // All public-boundary/state-lifetime tests above must leave the shared source
        // fixture untouched. This catches accidental render-to-input contamination
        // before it can masquerade as a waveform/vectorscope parity failure.
        require(vk.downloadRgba(im) == px, "validation source fixture was mutated by an earlier test");
        std::cout << "VALIDATION_SOURCE_FIXTURE_INTEGRITY_PASS\n";
        // Waveform parity all sampling modes, luma and RGB.
        for (auto sm : {SamplingMode::FullReference, SamplingMode::Production50, SamplingMode::Production25})
            for (auto wm : {WaveformMode::Luma, WaveformMode::RgbOverlay}) {
                vk.beginCommands();
                DisplayWaveformRecordInfo wi{};
                wi.commandBuffer = vk.commandBuffer();
                wi.input = iv;
                wi.render.mode = wm;
                wi.samplingMode = sm;
                wi.renderEnabled = false;
                scopes.recordDisplayWaveform(wi);
                scopes.recordCopyDisplayWaveform(vk.commandBuffer(), 0, host.buffer, 0);
                vk.submitAndWait();
                scopes.retireFrameSlot(0);
                auto* gp = static_cast<const detail::DisplayWaveformStd430*>(vk.map(host));
                vk.invalidate(host);
                auto rr = cpu::measureDisplayWaveform({px.data(), W, H, W * 4u}, wm, sm, 512, 256);
                require(std::equal(rr.density.begin(), rr.density.end(), gp->density), "wave density mismatch");
                require(gp->sampledPixelCount == rr.sampling.sampledPixelCount, "wave sampled count mismatch");
                vk.unmap(host);
            }
        std::cout << "DISPLAY_WAVEFORM_GPU_PARITY_PASS\n";
        // Vectorscope parity all sampling modes.
        for (auto sm : {SamplingMode::FullReference, SamplingMode::Production50, SamplingMode::Production25}) {
            vk.beginCommands();
            VectorscopeRecordInfo vi{};
            vi.commandBuffer = vk.commandBuffer();
            vi.input = iv;
            vi.samplingMode = sm;
            vi.renderEnabled = false;
            scopes.recordVectorscope(vi);
            scopes.recordCopyVectorscope(vk.commandBuffer(), 0, host.buffer, 0);
            vk.submitAndWait();
            scopes.retireFrameSlot(0);
            auto* gp = static_cast<const detail::VectorscopeStd430*>(vk.map(host));
            vk.invalidate(host);
            auto rr = cpu::measureVectorscope({px.data(), W, H, W * 4u}, sm, 256);
            require(std::equal(rr.density.begin(), rr.density.end(), gp->density), "vectorscope density mismatch");
            require(gp->sampledPixelCount == rr.sampling.sampledPixelCount, "vectorscope sampled count mismatch");
            vk.unmap(host);
        }
        std::cout << "VECTORSCOPE_GPU_PARITY_PASS\n";
        // Compact 128x128 production grid must preserve exact CPU/GPU quantization semantics.
        vk.beginCommands();
        VectorscopeRecordInfo vc{};
        vc.commandBuffer = vk.commandBuffer();
        vc.input = iv;
        vc.samplingMode = SamplingMode::FullReference;
        vc.grid = VectorscopeGrid::Compact128;
        vc.renderEnabled = false;
        scopes.recordVectorscope(vc);
        scopes.recordCopyVectorscope(vk.commandBuffer(), 0, host.buffer, 0);
        vk.submitAndWait();
        scopes.retireFrameSlot(0);
        auto* gpc = static_cast<const detail::VectorscopeStd430*>(vk.map(host));
        vk.invalidate(host);
        auto rrc = cpu::measureVectorscope({px.data(), W, H, W * 4u}, SamplingMode::FullReference, 128);
        require(std::equal(rrc.density.begin(), rrc.density.end(), gpc->density), "vectorscope 128 density mismatch");
        require(gpc->sampledPixelCount == rrc.sampling.sampledPixelCount, "vectorscope 128 sampled count mismatch");
        vk.unmap(host);
        std::cout << "VECTORSCOPE_COMPACT128_GPU_PARITY_PASS\n";

        // Production-resolution parity: representative RGB waveform/vectorscope.
        // Render-output smoke/parity path for the display scopes.
        auto target = vk.createRgbaImage(128, 96);
        std::vector<std::uint8_t> zero(size_t(target.width) * target.height * 4u, 0u);
        vk.uploadRgba(target, zero);
        ScopeRenderTarget tv{target.view, VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_LAYOUT_GENERAL, target.width,
                             target.height};
        vk.beginCommands();
        DisplayWaveformRecordInfo wri{};
        wri.commandBuffer = vk.commandBuffer();
        wri.input = iv;
        wri.samplingMode = SamplingMode::FullReference;
        wri.render.mode = WaveformMode::RgbOverlay;
        wri.renderEnabled = true;
        wri.renderTarget = tv;
        wri.render.showGrid = false;
        wri.render.showBackground = false;
        wri.render.densityGain = 1.0f;
        scopes.recordDisplayWaveform(wri);
        vk.submitAndWait();
        scopes.retireFrameSlot(0);
        require(hasSignal(vk.downloadRgba(target)), "display waveform render empty");
        // Luma rendering is independently qualified and must be monochrome.
        vk.beginCommands();
        DisplayWaveformRecordInfo lwri{};
        lwri.commandBuffer = vk.commandBuffer();
        lwri.input = iv;
        lwri.samplingMode = SamplingMode::FullReference;
        lwri.render.mode = WaveformMode::Luma;
        lwri.renderEnabled = true;
        lwri.renderTarget = tv;
        lwri.render.showGrid = false;
        lwri.render.showBackground = false;
        scopes.recordDisplayWaveform(lwri);
        vk.submitAndWait();
        scopes.retireFrameSlot(0);
        auto lumaSmoke = vk.downloadRgba(target);
        require(hasSignal(lumaSmoke), "display luma waveform render empty");
        for (size_t i = 0; i + 3 < lumaSmoke.size(); i += 4)
            require(lumaSmoke[i] == lumaSmoke[i + 1] && lumaSmoke[i + 1] == lumaSmoke[i + 2],
                    "luma waveform not monochrome");
        // Render-only must reuse the measured numerical buffer and reject a mode mismatch.
        vk.beginCommands();
        DisplayWaveformRenderInfo lwro{};
        lwro.commandBuffer = vk.commandBuffer();
        lwro.renderTarget = tv;
        lwro.render.mode = WaveformMode::Luma;
        lwro.render.showGrid = false;
        lwro.render.showBackground = false;
        scopes.recordDisplayWaveformRender(lwro);
        vk.submitAndWait();
        scopes.retireFrameSlot(0);
        require(hasSignal(vk.downloadRgba(target)), "luma waveform render-only empty");
        vk.beginCommands();
        bool waveModeRejected = false;
        DisplayWaveformRenderInfo mismatch{};
        mismatch.commandBuffer = vk.commandBuffer();
        mismatch.renderTarget = tv;
        mismatch.render.mode = WaveformMode::RgbOverlay;
        try {
            scopes.recordDisplayWaveformRender(mismatch);
        } catch (const std::invalid_argument&) {
            waveModeRejected = true;
        }
        require(waveModeRejected, "waveform render-only mode mismatch accepted");
        vk.submitAndWait();
        scopes.retireFrameSlot(0);

        // Descriptor lifetime regression: two waveform render invocations in one
        // command buffer must retain immutable target bindings until execution.
        auto waveTargetA = vk.createRgbaImage(192, 128), waveTargetB = vk.createRgbaImage(192, 128);
        std::vector<std::uint8_t> waveZeroAB(size_t(192) * 128 * 4u, 0u);
        vk.uploadRgba(waveTargetA, waveZeroAB);
        vk.uploadRgba(waveTargetB, waveZeroAB);
        ScopeRenderTarget waveRtA{waveTargetA.view, VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_LAYOUT_GENERAL, 192, 128};
        ScopeRenderTarget waveRtB{waveTargetB.view, VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_LAYOUT_GENERAL, 192, 128};
        vk.beginCommands();
        DisplayWaveformRecordInfo lifeMeasure{};
        lifeMeasure.commandBuffer = vk.commandBuffer();
        lifeMeasure.input = iv;
        lifeMeasure.renderTarget = waveRtA;
        lifeMeasure.render.mode = WaveformMode::Luma;
        lifeMeasure.render.showGrid = false;
        lifeMeasure.render.showBackground = false;
        lifeMeasure.renderEnabled = true;
        scopes.recordDisplayWaveform(lifeMeasure);
        DisplayWaveformRenderInfo lifeRender{};
        lifeRender.commandBuffer = vk.commandBuffer();
        lifeRender.renderTarget = waveRtB;
        lifeRender.render.mode = WaveformMode::Luma;
        lifeRender.render.showGrid = false;
        lifeRender.render.showBackground = false;
        scopes.recordDisplayWaveformRender(lifeRender);
        vk.submitAndWait();
        scopes.retireFrameSlot(0);
        auto lifeA = vk.downloadRgba(waveTargetA), lifeB = vk.downloadRgba(waveTargetB);
        require(hasSignal(lifeA), "waveform descriptor lifetime retargeted measure+render target A");
        require(hasSignal(lifeB), "waveform descriptor lifetime render-only target B empty");
        scopes.retireFrameSlot(0);
        vk.destroyRgbaImage(waveTargetA);
        vk.destroyRgbaImage(waveTargetB);
        std::cout << "WAVEFORM_DESCRIPTOR_LIFETIME_PASS\n";
        // Recorded != valid: an abandoned RGB measurement must not replace the
        // previously retired luma measurement used by render-only.
        vk.beginCommands();
        DisplayWaveformRecordInfo abandoned{};
        abandoned.commandBuffer = vk.commandBuffer();
        abandoned.input = iv;
        abandoned.render.mode = WaveformMode::RgbOverlay;
        abandoned.renderEnabled = false;
        scopes.recordDisplayWaveform(abandoned);
        // The recording is intentionally abandoned, but the Vulkan command buffer
        // must still leave RECORDING state before it can be reset/begun again.
        // It is never submitted.
        require(vkEndCommandBuffer(vk.commandBuffer()) == VK_SUCCESS, "abandoned waveform command buffer end failed");
        scopes.discardFrameSlotRecordings(0);
        vk.beginCommands();
        DisplayWaveformRenderInfo stillLuma{};
        stillLuma.commandBuffer = vk.commandBuffer();
        stillLuma.renderTarget = tv;
        stillLuma.render.mode = WaveformMode::Luma;
        stillLuma.render.showGrid = false;
        stillLuma.render.showBackground = false;
        scopes.recordDisplayWaveformRender(stillLuma);
        vk.submitAndWait();
        scopes.retireFrameSlot(0);
        vk.beginCommands();
        bool abandonedRgbRejected = false;
        DisplayWaveformRenderInfo staleRgb{};
        staleRgb.commandBuffer = vk.commandBuffer();
        staleRgb.renderTarget = tv;
        staleRgb.render.mode = WaveformMode::RgbOverlay;
        try {
            scopes.recordDisplayWaveformRender(staleRgb);
        } catch (const std::invalid_argument&) {
            abandonedRgbRejected = true;
        }
        require(abandonedRgbRejected, "abandoned waveform recording changed valid render-only mode");
        vk.submitAndWait();
        scopes.retireFrameSlot(0);
        std::cout << "WAVEFORM_RECORDED_VS_VALID_STATE_PASS\n";
        // A pending overwrite suspends the old retired measurement for every other
        // command buffer. Only the command buffer that recorded the pending measure
        // may consume it before retirement.
        VkCommandBuffer secondCb = VK_NULL_HANDLE;
        VkCommandBufferAllocateInfo cbai{};
        cbai.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        cbai.commandPool = vk.commandPool();
        cbai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        cbai.commandBufferCount = 1;
        require(vkAllocateCommandBuffers(vk.device(), &cbai, &secondCb) == VK_SUCCESS,
                "second command buffer allocation failed");
        vk.beginCommands();
        DisplayWaveformRecordInfo pendingOverwrite{};
        pendingOverwrite.commandBuffer = vk.commandBuffer();
        pendingOverwrite.input = iv;
        pendingOverwrite.render.mode = WaveformMode::RgbOverlay;
        pendingOverwrite.renderEnabled = false;
        scopes.recordDisplayWaveform(pendingOverwrite);
        VkCommandBufferBeginInfo cbbi{};
        cbbi.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        require(vkBeginCommandBuffer(secondCb, &cbbi) == VK_SUCCESS, "second command buffer begin failed");
        DisplayWaveformRenderInfo crossCb{};
        crossCb.commandBuffer = secondCb;
        crossCb.renderTarget = tv;
        crossCb.render.mode = WaveformMode::Luma;
        bool crossCbRejected = false;
        try {
            scopes.recordDisplayWaveformRender(crossCb);
        } catch (const std::logic_error&) {
            crossCbRejected = true;
        }
        require(crossCbRejected,
                "pending waveform overwrite did not suspend old render-only validity across command buffers");
        require(vkEndCommandBuffer(secondCb) == VK_SUCCESS, "second command buffer end failed");
        scopes.discardFrameSlotRecordings(0);
        require(vkEndCommandBuffer(vk.commandBuffer()) == VK_SUCCESS, "abandoned primary command buffer end failed");
        vkFreeCommandBuffers(vk.device(), vk.commandPool(), 1, &secondCb);
        std::cout << "WAVEFORM_PENDING_SUSPENDS_OLD_VALIDITY_PASS\n";

        std::cout << "WAVEFORM_RGB_LUMA_RENDER_PASS\n";
        // Cumulative descriptor-lifetime regression for vectorscope: two render-only
        // targets are recorded before one submission. Neither target binding may be retargeted.
        auto descRed = vk.createRgbaImage(64, 64);
        std::vector<std::uint8_t> redPx(size_t(64) * 64 * 4u, 255u);
        for (size_t q = 0; q < size_t(64) * 64; ++q) {
            redPx[q * 4] = 255;
            redPx[q * 4 + 1] = redPx[q * 4 + 2] = 0;
        }
        vk.uploadRgba(descRed, redPx);
        DisplayImageView redIv{descRed.view, VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_LAYOUT_GENERAL, 64, 64};
        auto vecA = vk.createRgbaImage(256, 256), vecB = vk.createRgbaImage(256, 256);
        std::vector<std::uint8_t> vz(size_t(256) * 256 * 4u, 0);
        vk.uploadRgba(vecA, vz);
        vk.uploadRgba(vecB, vz);
        ScopeRenderTarget vecRtA{vecA.view, VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_LAYOUT_GENERAL, 256, 256},
            vecRtB{vecB.view, VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_LAYOUT_GENERAL, 256, 256};
        // Vectorscope now intentionally permits only one unretired measurement per
        // frame slot. Establish one retired measurement, then exercise the invocation
        // arena with two render-only calls in a single command buffer. This proves
        // that render target A cannot be retargeted to B without violating the
        // recorded-vs-valid measurement contract merely to test descriptor reuse.
        vk.beginCommands();
        VectorscopeRecordInfo dvm{};
        dvm.commandBuffer = vk.commandBuffer();
        dvm.input = redIv;
        dvm.renderEnabled = false;
        scopes.recordVectorscope(dvm);
        vk.submitAndWait();
        scopes.retireFrameSlot(0);
        vk.beginCommands();
        VectorscopeRenderInfo dva{};
        dva.commandBuffer = vk.commandBuffer();
        dva.renderTarget = vecRtA;
        dva.render.showGrid = false;
        dva.render.showTargets = false;
        dva.render.showBackground = false;
        scopes.recordVectorscopeRender(dva);
        VectorscopeRenderInfo dvb = dva;
        dvb.renderTarget = vecRtB;
        scopes.recordVectorscopeRender(dvb);
        vk.submitAndWait();
        scopes.retireFrameSlot(0);
        auto vA = vk.downloadRgba(vecA), vB = vk.downloadRgba(vecB);
        require(hasSignal(vA) && hasSignal(vB), "vectorscope cumulative descriptor A/B target lost");
        require(vA == vB, "vectorscope cumulative descriptor A/B renders diverged/retargeted");
        vk.destroyRgbaImage(vecA);
        vk.destroyRgbaImage(vecB);
        vk.destroyRgbaImage(descRed);
        std::cout << "DISPLAY_DESCRIPTOR_LIFETIME_AB_PASS\n";

        auto vecNativeEarly = vk.createRgbaImage(512, 512);
        std::vector<std::uint8_t> vecNativeEarlyZero(size_t(512) * 512 * 4u, 0u);
        vk.uploadRgba(vecNativeEarly, vecNativeEarlyZero);
        ScopeRenderTarget vecNativeEarlyTarget{vecNativeEarly.view, VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_LAYOUT_GENERAL,
                                               512, 512};
        vk.beginCommands();
        VectorscopeRecordInfo vne{};
        vne.commandBuffer = vk.commandBuffer();
        vne.input = iv;
        vne.samplingMode = SamplingMode::FullReference;
        vne.renderEnabled = true;
        vne.renderTarget = vecNativeEarlyTarget;
        vne.render.showGrid = false;
        vne.render.showTargets = false;
        vne.render.showBackground = false;
        vne.render.densityGain = 1.0f;
        scopes.recordVectorscope(vne);
        vk.submitAndWait();
        scopes.retireFrameSlot(0);
        auto nativePixels = vk.downloadRgba(vecNativeEarly);
        require(hasSignal(nativePixels, 20), "vectorscope 512 presentation render empty");
        vk.destroyRgbaImage(vecNativeEarly);
        std::cout << "VECTORSCOPE_512_PRESENTATION_RENDER_PASS\n";
        vk.beginCommands();
        VectorscopeRecordInfo vri{};
        vri.commandBuffer = vk.commandBuffer();
        vri.input = iv;
        vri.samplingMode = SamplingMode::FullReference;
        vri.renderEnabled = true;
        vri.renderTarget = tv;
        vri.render.showGrid = false;
        vri.render.showTargets = false;
        vri.render.showBackground = false;
        vri.render.densityGain = 1.0f;
        scopes.recordVectorscope(vri);
        vk.submitAndWait();
        scopes.retireFrameSlot(0);
        require(hasSignal(vk.downloadRgba(target), 20), "vectorscope 128x96 presentation render empty");
        vk.destroyRgbaImage(target);
        std::cout << "DISPLAY_RENDER_OUTPUT_PASS\n";

        // Renderer-shape oracles: these validate presentation independently of numerical parity.
        auto makeSolid = [](std::uint32_t w, std::uint32_t h, std::uint8_t v) {
            std::vector<std::uint8_t> a(size_t(w) * h * 4u, 255u);
            for (size_t i = 0; i < size_t(w) * h; ++i) a[i * 4] = a[i * 4 + 1] = a[i * 4 + 2] = v;
            return a;
        };
        auto renderShape = [&](const std::vector<std::uint8_t>& src, std::uint32_t sw, std::uint32_t sh,
                               std::uint32_t ow, std::uint32_t oh, auto rec) {
            auto si = vk.createRgbaImage(sw, sh);
            vk.uploadRgba(si, src);
            auto to = vk.createRgbaImage(ow, oh);
            std::vector<std::uint8_t> z(size_t(ow) * oh * 4u, 0u);
            vk.uploadRgba(to, z);
            DisplayImageView siv{si.view, VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_LAYOUT_GENERAL, sw, sh};
            ScopeRenderTarget rt{to.view, VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_LAYOUT_GENERAL, ow, oh};
            vk.beginCommands();
            rec(siv, rt);
            vk.submitAndWait();
            scopes.retireFrameSlot(0);
            auto out = vk.downloadRgba(to);
            vk.destroyRgbaImage(si);
            vk.destroyRgbaImage(to);
            return out;
        };
        auto grayWave =
            renderShape(makeSolid(256, 256, 128), 256, 256, 512, 256, [&](DisplayImageView siv, ScopeRenderTarget rt) {
                DisplayWaveformRecordInfo r{};
                r.commandBuffer = vk.commandBuffer();
                r.input = siv;
                r.renderTarget = rt;
                r.render.mode = WaveformMode::RgbOverlay;
                r.render.showGrid = false;
                r.render.showBackground = false;
                r.render.densityGain = 1.f;
                r.render.pointSpreadBins = 1;
                r.samplingMode = SamplingMode::FullReference;
                scopes.recordDisplayWaveform(r);
            });
        int minY = 999, maxY = -1;
        for (int y = 0; y < 256; ++y)
            for (int x = 0; x < 512; ++x) {
                size_t i = (size_t(y) * 512 + x) * 4u;
                if (grayWave[i] > 12 || grayWave[i + 1] > 12 || grayWave[i + 2] > 12) {
                    minY = std::min(minY, y);
                    maxY = std::max(maxY, y);
                }
            }
        require(maxY >= minY, "constant gray waveform empty");
        require(maxY - minY <= 7, "constant gray waveform rendered as thick filled band");
        require(minY <= 130 && maxY >= 125, "constant gray waveform at wrong level");
        std::cout << "WAVEFORM_CONSTANT_BAND_RENDER_PASS\n";
        std::vector<std::uint8_t> ramp(size_t(256) * 128 * 4u, 255u);
        for (std::uint32_t y = 0; y < 128; ++y)
            for (std::uint32_t x = 0; x < 256; ++x) {
                auto* p = &ramp[(size_t(y) * 256 + x) * 4u];
                p[0] = p[1] = p[2] = std::uint8_t(x);
            }
        auto rampWave = renderShape(ramp, 256, 128, 512, 256, [&](DisplayImageView siv, ScopeRenderTarget rt) {
            DisplayWaveformRecordInfo r{};
            r.commandBuffer = vk.commandBuffer();
            r.input = siv;
            r.renderTarget = rt;
            r.render.mode = WaveformMode::RgbOverlay;
            r.render.showGrid = false;
            r.render.showBackground = false;
            r.render.densityGain = 1.f;
            r.render.pointSpreadBins = 1;
            r.samplingMode = SamplingMode::FullReference;
            scopes.recordDisplayWaveform(r);
        });
        for (int x = 16; x < 496; x += 32) {
            int lo = 999, hi = -1;
            for (int y = 0; y < 256; ++y) {
                size_t i = (size_t(y) * 512 + x) * 4u;
                if (rampWave[i] > 12 || rampWave[i + 1] > 12 || rampWave[i + 2] > 12) {
                    lo = std::min(lo, y);
                    hi = std::max(hi, y);
                }
            }
            require(hi >= lo, "ramp waveform missing trace column");
            require(hi - lo <= 9, "ramp waveform column rendered as filled area");
        }
        std::cout << "WAVEFORM_DIAGONAL_RENDER_PASS\n";

        // Waveform E horizontal-topology freeze gates. These inspect only the bright
        // (top) luma band so the black floor cannot mask horizontal bridging.
        auto makeColumns = [](std::uint32_t w, std::uint32_t h, const std::vector<std::uint32_t>& bright) {
            std::vector<std::uint8_t> a(size_t(w) * h * 4u, 255u);
            for (size_t i = 0; i < size_t(w) * h; ++i) a[i * 4] = a[i * 4 + 1] = a[i * 4 + 2] = 0u;
            for (auto x : bright)
                if (x < w)
                    for (std::uint32_t y = 0; y < h; ++y)
                        a[(size_t(y) * w + x) * 4] = a[(size_t(y) * w + x) * 4 + 1] = a[(size_t(y) * w + x) * 4 + 2] =
                            255u;
            return a;
        };
        auto renderLumaTopology = [&](const std::vector<std::uint8_t>& src, std::uint32_t sw, std::uint32_t sh) {
            return renderShape(src, sw, sh, 512, 256, [&](DisplayImageView siv, ScopeRenderTarget rt) {
                DisplayWaveformRecordInfo r{};
                r.commandBuffer = vk.commandBuffer();
                r.input = siv;
                r.renderTarget = rt;
                r.render.mode = WaveformMode::Luma;
                r.render.showGrid = false;
                r.render.showBackground = false;
                scopes.recordDisplayWaveform(r);
            });
        };
        auto brightAt = [&](const std::vector<std::uint8_t>& o, int x) {
            for (int y = 0; y < 24; ++y) {
                size_t i = (size_t(y) * 512 + size_t(x)) * 4u;
                if (o[i] > 12) return true;
            }
            return false;
        };
        auto isolated = renderLumaTopology(makeColumns(256, 128, {128}), 256, 128);
        int isoLo = 999, isoHi = -1;
        for (int x = 0; x < 512; ++x)
            if (brightAt(isolated, x)) {
                isoLo = std::min(isoLo, x);
                isoHi = std::max(isoHi, x);
            }
        require(isoHi >= isoLo && isoHi - isoLo <= 5, "waveform isolated bright column spreads too far horizontally");
        auto separated = renderLumaTopology(makeColumns(256, 128, {96, 112}), 256, 128);
        require(brightAt(separated, 192) && brightAt(separated, 224), "waveform separated columns missing");
        for (int x = 200; x <= 216; ++x)
            require(!brightAt(separated, x), "waveform invented bridge across known-zero column gap");
        std::vector<std::uint8_t> edge(size_t(256) * 128 * 4u, 255u);
        for (std::uint32_t y = 0; y < 128; ++y)
            for (std::uint32_t x = 0; x < 256; ++x) {
                auto v = x < 128 ? 0u : 255u;
                auto* i = &edge[(size_t(y) * 256 + x) * 4u];
                i[0] = i[1] = i[2] = static_cast<std::uint8_t>(v);
            }
        auto hardEdge = renderLumaTopology(edge, 256, 128);
        for (int x = 0; x < 250; ++x)
            require(!brightAt(hardEdge, x), "waveform hard vertical edge leaked materially into zero side");
        require(brightAt(hardEdge, 258), "waveform hard vertical edge lost bright side");
        std::vector<std::uint32_t> bars;
        for (std::uint32_t x = 120; x <= 136; x += 4) bars.push_back(x);
        auto narrow = renderLumaTopology(makeColumns(256, 128, bars), 256, 128);
        for (std::uint32_t x = 122; x < 136; x += 4) {
            int center = int(x * 2u);
            require(!brightAt(narrow, center), "waveform alternating narrow bars bridged a known-zero column");
        }
        std::cout << "WAVEFORM_HORIZONTAL_TOPOLOGY_PASS\n";
        // Sampling-normalization presentation gate: density exposure should remain
        // materially stable when only the deterministic sampling fraction changes.
        // Keep every 2x2 sampler block chromatically identical so Full/50/25
        // have the same waveform geometry and differ only in population count.
        // This isolates presentation normalization from deterministic-sampler aliasing,
        // which is tested separately by the sampling-quality suite.
        std::vector<std::uint8_t> normSrc(size_t(256) * 256 * 4u, 255u);
        for (std::uint32_t y = 0; y < 256; ++y)
            for (std::uint32_t x = 0; x < 256; ++x) {
                const std::uint32_t bx = x >> 1u, by = y >> 1u;
                std::uint8_t v = static_cast<std::uint8_t>((bx + by) & 255u);
                auto* q = &normSrc[(size_t(y) * 256 + x) * 4u];
                q[0] = q[1] = q[2] = v;
            }
        auto renderNorm = [&](SamplingMode smode) {
            return renderShape(normSrc, 256, 256, 512, 256, [&](DisplayImageView siv, ScopeRenderTarget rt) {
                DisplayWaveformRecordInfo r{};
                r.commandBuffer = vk.commandBuffer();
                r.input = siv;
                r.renderTarget = rt;
                r.render.mode = WaveformMode::Luma;
                r.samplingMode = smode;
                r.render.showGrid = false;
                r.render.showBackground = false;
                scopes.recordDisplayWaveform(r);
            });
        };
        auto exposure = [&](const std::vector<std::uint8_t>& o) {
            double sum = 0.0;
            for (size_t i = 0; i < o.size(); i += 4) sum += o[i] / 255.0;
            return sum / double(o.size() / 4u);
        };
        auto nFull = renderNorm(SamplingMode::FullReference), n50 = renderNorm(SamplingMode::Production50),
             n25 = renderNorm(SamplingMode::Production25);
        double eFull = exposure(nFull), e50 = exposure(n50), e25 = exposure(n25);
        require(eFull > 0.0, "waveform normalization full exposure empty");
        double r50 = e50 / eFull, r25 = e25 / eFull;
        std::cout << "WAVEFORM_SAMPLING_NORMALIZATION values full=" << eFull << " p50=" << e50 << " p25=" << e25
                  << " ratios=" << r50 << "," << r25 << "\n";
        require(r50 > .90 && r50 < 1.10, "waveform 50% sampling materially changed rendered exposure");
        require(r25 > .85 && r25 < 1.15, "waveform 25% sampling materially changed rendered exposure");
        std::cout << "WAVEFORM_SAMPLING_NORMALIZATION_PASS\n";

        auto grayLuma =
            renderShape(makeSolid(256, 256, 128), 256, 256, 512, 256, [&](DisplayImageView siv, ScopeRenderTarget rt) {
                DisplayWaveformRecordInfo r{};
                r.commandBuffer = vk.commandBuffer();
                r.input = siv;
                r.renderTarget = rt;
                r.render.mode = WaveformMode::Luma;
                r.render.showGrid = false;
                r.render.showBackground = false;
                r.samplingMode = SamplingMode::FullReference;
                scopes.recordDisplayWaveform(r);
            });
        int lmin = 999, lmax = -1;
        for (int y = 0; y < 256; ++y)
            for (int x = 0; x < 512; ++x) {
                size_t i = (size_t(y) * 512 + x) * 4u;
                if (grayLuma[i] > 12) {
                    require(grayLuma[i] == grayLuma[i + 1] && grayLuma[i + 1] == grayLuma[i + 2],
                            "constant luma waveform not monochrome");
                    lmin = std::min(lmin, y);
                    lmax = std::max(lmax, y);
                }
            }
        require(lmax >= lmin, "constant luma waveform empty");
        require(lmax - lmin <= 7, "constant luma waveform rendered as thick filled band");
        require(lmin <= 130 && lmax >= 125, "constant luma waveform at wrong level");
        auto rampLuma = renderShape(ramp, 256, 128, 512, 256, [&](DisplayImageView siv, ScopeRenderTarget rt) {
            DisplayWaveformRecordInfo r{};
            r.commandBuffer = vk.commandBuffer();
            r.input = siv;
            r.renderTarget = rt;
            r.render.mode = WaveformMode::Luma;
            r.render.showGrid = false;
            r.render.showBackground = false;
            r.samplingMode = SamplingMode::FullReference;
            scopes.recordDisplayWaveform(r);
        });
        for (int x = 16; x < 496; x += 32) {
            int lo = 999, hi = -1;
            for (int y = 0; y < 256; ++y) {
                size_t i = (size_t(y) * 512 + x) * 4u;
                if (rampLuma[i] > 12) {
                    lo = std::min(lo, y);
                    hi = std::max(hi, y);
                }
            }
            require(hi >= lo, "luma ramp waveform missing trace column");
            require(hi - lo <= 9, "luma ramp waveform column rendered as filled area");
        }
        std::cout << "WAVEFORM_LUMA_SHAPE_PASS\n";

        // RAW waveform render validation with a synthetic frozen-layout fixture: no RAW analysis is performed here.
        auto rawTarget = vk.createRgbaImage(512, 256);
        std::vector<std::uint8_t> rawZero(size_t(rawTarget.width) * rawTarget.height * 4u, 0u);
        vk.uploadRgba(rawTarget, rawZero);
        auto rawWave = vk.createBuffer(262144, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT,
                                       VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
        auto* rw = static_cast<std::uint32_t*>(vk.map(rawWave));
        std::fill(rw, rw + 262144 / 4u, 0u);
        auto wi = [&](unsigned c, unsigned y, unsigned x) { return c * 16384u + y * 256u + x; };
        rw[wi(0, 12, 32)] = 12u;
        rw[wi(1, 24, 96)] = 12u;
        rw[wi(2, 36, 160)] = 12u;
        rw[wi(3, 48, 224)] = 12u;
        vk.flush(rawWave);
        vk.unmap(rawWave);
        RawWaveformRecordInfo rwi{};
        rwi.rawWaveform.waveform = {rawWave.buffer, 0, 262144};
        rwi.renderTarget = {rawTarget.view, VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_LAYOUT_GENERAL, rawTarget.width,
                            rawTarget.height};
        rwi.render.mode = RawWaveformMode::PhysicalOverlay;
        rwi.render.showGrid = false;
        rwi.render.showBackground = false;
        rwi.render.densityGain = 0.5f;
        vk.beginCommands();
        rwi.commandBuffer = vk.commandBuffer();
        scopes.recordRawWaveformRender(rwi);
        vk.submitAndWait();
        scopes.retireFrameSlot(0);
        auto rawWavePixels = vk.downloadRgba(rawTarget);
        require(hasDominant(rawWavePixels, 0), "RAW waveform R render missing");
        require(hasDominant(rawWavePixels, 1), "RAW waveform green render missing");
        require(hasDominant(rawWavePixels, 2), "RAW waveform B render missing");
        std::cout << "RAW_WAVEFORM_RENDER_PASS\n";

        // RAW render descriptor lifetime uses two command buffers recorded before a
        // single queue submission. This still proves descriptor-update lifetime:
        // if A and B shared one mutable set, B's later vkUpdateDescriptorSets would
        // retarget the already-recorded A command before either command executes.
        auto submitRawAB = [&](auto recordA, auto recordB) {
            VkCommandBuffer cbs[2]{};
            VkCommandBufferAllocateInfo ai{};
            ai.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
            ai.commandPool = vk.commandPool();
            ai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
            ai.commandBufferCount = 2;
            require(vkAllocateCommandBuffers(vk.device(), &ai, cbs) == VK_SUCCESS,
                    "RAW A/B command buffer allocation failed");
            VkCommandBufferBeginInfo bi{};
            bi.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
            bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
            require(vkBeginCommandBuffer(cbs[0], &bi) == VK_SUCCESS, "RAW A command begin failed");
            recordA(cbs[0]);
            require(vkEndCommandBuffer(cbs[0]) == VK_SUCCESS, "RAW A command end failed");
            require(vkBeginCommandBuffer(cbs[1], &bi) == VK_SUCCESS, "RAW B command begin failed");
            recordB(cbs[1]);
            require(vkEndCommandBuffer(cbs[1]) == VK_SUCCESS, "RAW B command end failed");
            VkFence fence = VK_NULL_HANDLE;
            VkFenceCreateInfo fi{};
            fi.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
            require(vkCreateFence(vk.device(), &fi, nullptr, &fence) == VK_SUCCESS, "RAW A/B fence creation failed");
            VkSubmitInfo si{};
            si.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
            si.commandBufferCount = 2;
            si.pCommandBuffers = cbs;
            require(vkQueueSubmit(vk.queue(), 1, &si, fence) == VK_SUCCESS, "RAW A/B queue submit failed");
            require(vkWaitForFences(vk.device(), 1, &fence, VK_TRUE, UINT64_MAX) == VK_SUCCESS,
                    "RAW A/B fence wait failed");
            vkDestroyFence(vk.device(), fence, nullptr);
            vkFreeCommandBuffers(vk.device(), vk.commandPool(), 2, cbs);
            scopes.retireFrameSlot(0);
        };

        std::vector<std::uint8_t> rz(size_t(256) * 128 * 4u, 0);
        auto rawWaveB = vk.createBuffer(262144, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT,
                                        VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
        auto* rwb = static_cast<std::uint32_t*>(vk.map(rawWaveB));
        std::fill(rwb, rwb + 262144 / 4u, 0u);
        constexpr unsigned rawWaveTestChannel = 2u, rawWaveTestY = 52u, rawWaveTestX = 200u;
        static_assert(rawWaveTestChannel < 4u && rawWaveTestY < 64u && rawWaveTestX < 256u);
        const auto rawWaveTestIndex = wi(rawWaveTestChannel, rawWaveTestY, rawWaveTestX);
        require(rawWaveTestIndex < (262144u / 4u), "RAW waveform B fixture index out of bounds");
        rwb[rawWaveTestIndex] = 20u;
        vk.flush(rawWaveB);
        vk.unmap(rawWaveB);
        auto rawWaveAImg = vk.createRgbaImage(256, 128), rawWaveBImg = vk.createRgbaImage(256, 128);
        vk.uploadRgba(rawWaveAImg, rz);
        vk.uploadRgba(rawWaveBImg, rz);
        RawWaveformRecordInfo rwa = rwi;
        rwa.renderTarget = {rawWaveAImg.view, VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_LAYOUT_GENERAL, 256, 128};
        RawWaveformRecordInfo rwbri = rwa;
        rwbri.rawWaveform.waveform = {rawWaveB.buffer, 0, 262144};
        rwbri.renderTarget = {rawWaveBImg.view, VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_LAYOUT_GENERAL, 256, 128};
        submitRawAB(
            [&](VkCommandBuffer cb) {
                rwa.commandBuffer = cb;
                scopes.recordRawWaveformRender(rwa);
            },
            [&](VkCommandBuffer cb) {
                rwbri.commandBuffer = cb;
                scopes.recordRawWaveformRender(rwbri);
            });
        auto rwA = vk.downloadRgba(rawWaveAImg), rwBpx = vk.downloadRgba(rawWaveBImg);
        require(hasSignal(rwA) && hasSignal(rwBpx), "RAW waveform descriptor A/B target lost");
        require(rwA != rwBpx, "RAW waveform descriptor A/B source collapsed/retargeted");
        std::cout << "RAW_WAVEFORM_DESCRIPTOR_LIFETIME_AB_PASS\n";

        vk.destroyRgbaImage(rawWaveAImg);
        vk.destroyRgbaImage(rawWaveBImg);
        vk.destroyBuffer(rawWaveB);
        std::cout << "RAW_DESCRIPTOR_LIFETIME_AB_PASS\n";
        vk.destroyRgbaImage(rawTarget);
        vk.destroyBuffer(rawWave);
        std::cout << "RAW_RENDER_OUTPUT_PASS\n";

        vk.destroyRgbaImage(im);
        for (const auto& dims :
             {std::pair<std::uint32_t, std::uint32_t>{2040u, 1532u}, {2040u, 1536u}, {2048u, 1536u}}) {
            const std::uint32_t pw = dims.first, ph = dims.second;
            auto prodPx = pattern(pw, ph);
            auto prodIm = vk.createRgbaImage(pw, ph);
            vk.uploadRgba(prodIm, prodPx);
            DisplayImageView piv{prodIm.view, VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_LAYOUT_GENERAL, pw, ph};
            vk.beginCommands();
            DisplayWaveformRecordInfo pwi{};
            pwi.commandBuffer = vk.commandBuffer();
            pwi.input = piv;
            pwi.render.mode = WaveformMode::RgbOverlay;
            pwi.samplingMode = SamplingMode::Production50;
            pwi.renderEnabled = false;
            scopes.recordDisplayWaveform(pwi);
            scopes.recordCopyDisplayWaveform(vk.commandBuffer(), 0, host.buffer, 0);
            vk.submitAndWait();
            scopes.retireFrameSlot(0);
            auto* pwp = static_cast<const detail::DisplayWaveformStd430*>(vk.map(host));
            vk.invalidate(host);
            auto pwref = cpu::measureDisplayWaveform({prodPx.data(), pw, ph, pw * 4u}, WaveformMode::RgbOverlay,
                                                     SamplingMode::Production50, 512, 256);
            require(std::equal(pwref.density.begin(), pwref.density.end(), pwp->density), "prod wave mismatch");
            require(pwp->sampledPixelCount == pwref.sampling.sampledPixelCount, "prod wave sample mismatch");
            vk.unmap(host);
            vk.beginCommands();
            VectorscopeRecordInfo pvi{};
            pvi.commandBuffer = vk.commandBuffer();
            pvi.input = piv;
            pvi.samplingMode = SamplingMode::Production50;
            pvi.renderEnabled = false;
            scopes.recordVectorscope(pvi);
            scopes.recordCopyVectorscope(vk.commandBuffer(), 0, host.buffer, 0);
            vk.submitAndWait();
            scopes.retireFrameSlot(0);
            auto* pvp = static_cast<const detail::VectorscopeStd430*>(vk.map(host));
            vk.invalidate(host);
            auto pvref = cpu::measureVectorscope({prodPx.data(), pw, ph, pw * 4u}, SamplingMode::Production50, 256);
            require(std::equal(pvref.density.begin(), pvref.density.end(), pvp->density), "prod vector mismatch");
            require(pvp->sampledPixelCount == pvref.sampling.sampledPixelCount, "prod vector sample mismatch");
            vk.unmap(host);
            vk.destroyRgbaImage(prodIm);
            std::cout << "PRODUCTION_RESOLUTION_GPU_PARITY_PASS " << pw << "x" << ph << "\n";
        }
        vk.destroyBuffer(host);
        std::cout << "IMAGE_SCOPES_VULKAN_VALIDATION_PASS\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "FAIL: " << e.what() << "\n";
        return 1;
    }
}
