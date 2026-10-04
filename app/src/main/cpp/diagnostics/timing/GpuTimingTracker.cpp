#include "diagnostics/timing/GpuTimingTracker.h"

#include <android/log.h>

#include <algorithm>
#include <array>
#include <sstream>
#include <stdexcept>
#include <utility>

namespace rawrcam::diagnostics {
namespace {
constexpr const char* kTag = "RawrCamNative";
constexpr uint32_t kQueriesPerSlot = 15;
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, kTag, __VA_ARGS__)

void vkCheck(VkResult result, const char* what) {
    if (result != VK_SUCCESS) throw std::runtime_error(std::string(what) + " VkResult=" + std::to_string(result));
}
}  // namespace

GpuTimingTracker::GpuTimingTracker(Diagnostic diagnostic) : diagnostic_(std::move(diagnostic)) {}
GpuTimingTracker::~GpuTimingTracker() { destroy(); }

void GpuTimingTracker::initialize(VkDevice device, float timestampPeriodNs, uint32_t frameSlotCount) {
    destroy();
    device_ = device;
    timestampPeriodNs_ = timestampPeriodNs;
    frameSlotCount_ = frameSlotCount;
    VkQueryPoolCreateInfo info{};
    info.sType = VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO;
    info.queryType = VK_QUERY_TYPE_TIMESTAMP;
    info.queryCount = frameSlotCount_ * kQueriesPerSlot;
    vkCheck(vkCreateQueryPool(device_, &info, nullptr, &queryPool_), "vkCreateQueryPool");
}

void GpuTimingTracker::destroy() {
    if (device_ && queryPool_) vkDestroyQueryPool(device_, queryPool_, nullptr);
    queryPool_ = VK_NULL_HANDLE;
    device_ = VK_NULL_HANDLE;
    frameSlotCount_ = 0;
}

void GpuTimingTracker::beginFrame(VkCommandBuffer command, uint32_t slotIndex, bool probeRepeat) {
    vkCmdResetQueryPool(command, queryPool_, slotIndex * kQueriesPerSlot, kQueriesPerSlot);
    probeRepeat_[slotIndex] = probeRepeat;
    videoActive_[slotIndex] = false;
    vkCmdWriteTimestamp(command, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, queryPool_, slotIndex * kQueriesPerSlot);
}
void GpuTimingTracker::markRawInputReady(VkCommandBuffer command, uint32_t slotIndex) const {
    vkCmdWriteTimestamp(command, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, queryPool_, slotIndex * kQueriesPerSlot + 1);
}
void GpuTimingTracker::markVideoProcessBegin(VkCommandBuffer command, uint32_t slotIndex, bool active) {
    videoActive_[slotIndex] = active;
    vkCmdWriteTimestamp(command, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, queryPool_, slotIndex * kQueriesPerSlot + 11);
}
void GpuTimingTracker::markVideoProcessDone(VkCommandBuffer command, uint32_t slotIndex) const {
    vkCmdWriteTimestamp(command, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, queryPool_, slotIndex * kQueriesPerSlot + 12);
}
void GpuTimingTracker::markVideoDemosaicDone(VkCommandBuffer command, uint32_t slotIndex) const {
    vkCmdWriteTimestamp(command, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, queryPool_, slotIndex * kQueriesPerSlot + 13);
}
void GpuTimingTracker::markVideoPostDone(VkCommandBuffer command, uint32_t slotIndex) const {
    vkCmdWriteTimestamp(command, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, queryPool_, slotIndex * kQueriesPerSlot + 14);
}
void GpuTimingTracker::markRawDone(VkCommandBuffer command, uint32_t slotIndex) const {
    vkCmdWriteTimestamp(command, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, queryPool_, slotIndex * kQueriesPerSlot + 2);
}
void GpuTimingTracker::markTonemapInputReady(VkCommandBuffer command, uint32_t slotIndex) const {
    vkCmdWriteTimestamp(command, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, queryPool_, slotIndex * kQueriesPerSlot + 3);
}
void GpuTimingTracker::markTonemapDone(VkCommandBuffer command, uint32_t slotIndex) const {
    vkCmdWriteTimestamp(command, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, queryPool_, slotIndex * kQueriesPerSlot + 4);
}
void GpuTimingTracker::markTonemapRepeatInputReady(VkCommandBuffer command, uint32_t slotIndex) const {
    vkCmdWriteTimestamp(command, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, queryPool_, slotIndex * kQueriesPerSlot + 5);
}
void GpuTimingTracker::markTonemapRepeatDone(VkCommandBuffer command, uint32_t slotIndex) const {
    vkCmdWriteTimestamp(command, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, queryPool_, slotIndex * kQueriesPerSlot + 6);
}
void GpuTimingTracker::markScopesDone(VkCommandBuffer command, uint32_t slotIndex) const {
    vkCmdWriteTimestamp(command, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, queryPool_, slotIndex * kQueriesPerSlot + 7);
}
void GpuTimingTracker::markOverlayDone(VkCommandBuffer command, uint32_t slotIndex) const {
    vkCmdWriteTimestamp(command, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, queryPool_, slotIndex * kQueriesPerSlot + 8);
}
void GpuTimingTracker::markPresentationDone(VkCommandBuffer command, uint32_t slotIndex) const {
    vkCmdWriteTimestamp(command, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, queryPool_, slotIndex * kQueriesPerSlot + 9);
}
void GpuTimingTracker::markFrameDone(VkCommandBuffer command, uint32_t slotIndex) const {
    if (!videoActive_[slotIndex]) {
        markVideoDemosaicDone(command, slotIndex);
        markVideoPostDone(command, slotIndex);
    }
    vkCmdWriteTimestamp(command, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, queryPool_, slotIndex * kQueriesPerSlot + 10);
}

void GpuTimingTracker::consumeCompleted(uint32_t slotIndex, std::size_t importedCount, std::size_t descriptorCount,
                                        bool rawStateEnabled) {
    if (!device_ || !queryPool_) return;
    std::array<uint64_t, kQueriesPerSlot> q{};
    if (vkGetQueryPoolResults(device_, queryPool_, slotIndex * kQueriesPerSlot, kQueriesPerSlot, sizeof(q), q.data(),
                              sizeof(uint64_t), VK_QUERY_RESULT_64_BIT) != VK_SUCCESS)
        return;
    auto ns = [&](int a, int b) { return static_cast<double>(q[b] - q[a]) * timestampPeriodNs_; };
    const double videoProcess = videoActive_[slotIndex] ? ns(11, 12) : 0.0;
    const double rawPrep = ns(0, 1), rawShader = std::max(0.0, ns(1, 2) - videoProcess), toneHandoff = ns(2, 3),
                 tone1 = ns(3, 4), toneRepeatBarrier = ns(4, 5), tone2 = ns(5, 6);
    rawPrepNs_ += rawPrep;
    rawShaderNs_ += rawShader;
    if (videoActive_[slotIndex]) {
        videoProcessNs_ += videoProcess;
        videoDemosaicNs_ += ns(11, 13);
        videoPostNs_ += ns(13, 14);
        videoRenderNs_ += ns(14, 12);
        ++videoTimingSamples_;
    }
    toneHandoffNs_ += toneHandoff;
    rawNs_ += rawPrep + rawShader;
    tonemapNs_ += tone1;
    scopesNs_ += ns(6, 7);
    overlayNs_ += ns(7, 8);
    presentationNs_ += ns(8, 9);
    totalNs_ += ns(0, 10);
    ++timingSamples_;
    if (probeRepeat_[slotIndex]) {
        LOGI(
            "PREVIEW_GPU_PROBE frame=%llu rawPrep=%.3fms rawShader=%.3fms toneHandoff=%.3fms tone1=%.3fms "
            "repeatBarrier=%.3fms tone2=%.3fms rawState=%u",
            static_cast<unsigned long long>(timingSamples_), rawPrep / 1e6, rawShader / 1e6, toneHandoff / 1e6,
            tone1 / 1e6, toneRepeatBarrier / 1e6, tone2 / 1e6, rawStateEnabled ? 1u : 0u);
    }
    if ((timingSamples_ % 120) != 0) return;
    const auto s = snapshot();
    const double denom = static_cast<double>(timingSamples_) * 1e6;
    const double prep = rawPrepNs_ / denom, shader = rawShaderNs_ / denom, hand = toneHandoffNs_ / denom;
    const double sc = scopesNs_ / denom, ov = overlayNs_ / denom, pr = presentationNs_ / denom, post = sc + ov + pr;
    LOGI(
        "PREVIEW_GPU_BREAKDOWN frames=%llu rawPrep=%.3fms rawShader=%.3fms rawAggregate=%.3fms toneHandoff=%.3fms "
        "tonemap=%.3fms videoProcess=%.3fms scopes=%.3fms overlay=%.3fms presentation=%.3fms post=%.3fms "
        "totalGPU=%.3fms "
        "rawState=%u imported=%zu descriptors=%zu dropped=%llu",
        static_cast<unsigned long long>(s.timingSamples), prep, shader, s.avgRawMs, hand, s.avgTonemapMs,
        s.avgVideoProcessMs, sc, ov, pr, post, s.avgTotalGpuMs, rawStateEnabled ? 1u : 0u, importedCount,
        descriptorCount, static_cast<unsigned long long>(s.dropped));
    if (diagnostic_) {
        std::ostringstream d;
        d << "CHECKPOINT frames=" << s.timingSamples << " rawPrepMs=" << prep << " rawShaderMs=" << shader
          << " rawMs=" << s.avgRawMs << " toneHandoffMs=" << hand << " tonemapMs=" << s.avgTonemapMs
          << " videoProcessMs=" << s.avgVideoProcessMs << " totalGpuMs=" << s.avgTotalGpuMs
          << " rawState=" << (rawStateEnabled ? 1 : 0);
        diagnostic_(d.str());
    }
}

GpuTimingSnapshot GpuTimingTracker::snapshot() const noexcept {
    GpuTimingSnapshot out{};
    out.submitted = submitted_;
    out.dropped = dropped_;
    out.timingSamples = timingSamples_;
    out.avgRawMs = avgMs(rawNs_, timingSamples_);
    out.avgTonemapMs = avgMs(tonemapNs_, timingSamples_);
    out.avgTotalGpuMs = avgMs(totalNs_, timingSamples_);
    out.avgVideoProcessMs = avgMs(videoProcessNs_, videoTimingSamples_);
    out.avgVideoDemosaicMs = avgMs(videoDemosaicNs_, videoTimingSamples_);
    out.avgVideoPostMs = avgMs(videoPostNs_, videoTimingSamples_);
    out.avgVideoRenderMs = avgMs(videoRenderNs_, videoTimingSamples_);
    return out;
}

double GpuTimingTracker::avgMs(double totalNs, uint64_t samples) noexcept {
    return samples ? totalNs / static_cast<double>(samples) / 1.0e6 : 0.0;
}

}  // namespace rawrcam::diagnostics
