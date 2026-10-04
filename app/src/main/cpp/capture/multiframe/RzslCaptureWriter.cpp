#include "capture/multiframe/RzslCaptureWriter.h"

#include <android/log.h>
#include <sys/sysinfo.h>
#include <unistd.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <thread>
#include <utility>

#include "capture/multiframe/MultiframeDescription.h"
#include "capture/multiframe/MultiframeQueueHelpers.h"
#include "capture/persistence/CaptureJob.h"
#include "color/ColorMath.h"
#include "color/FilmExposure.h"
#include "color/WhiteBalance.h"
#include "develop/DevelopContextBuilder.h"
#include "develop/demosaic/DualStillProcessor.h"
#include "develop/highlight/LensShadingMapSnapshot.h"
#include "develop/render/StillImageRenderer.h"
#include "diagnostics/logging/RuntimeTraceRecorder.h"
#include "encoding/dng/DngCaptureWriter.h"
#include "encoding/jpeg/JpegCaptureWriter.h"
#include "encoding/jpeg/JpegTimingsFormat.h"
#include "encoding/rzsl/RzslBundleSink.h"
#include "geometry/OrientationTransform.h"
#include "imaging/RawSnapshot.h"
#include "rawr/raw_gpu_pipeline/AndroidBurstCoordinator.h"
#include "rawr/raw_gpu_pipeline/ReplayMetadata.h"
#include "rawr/raw_multiframe_output/MultiframeOutputAdapter.h"
#include "rawr/zsl_codec/ZslCodec.h"
#include "rawr/zsl_ring/RawImageRing.h"
#include "rawr/zsl_ring/ZslRing.h"

namespace rawrcam::capture::multiframe {
namespace {
void vkCheck(VkResult r, const char* what) {
    if (r != VK_SUCCESS) throw std::runtime_error(std::string(what) + " VkResult=" + std::to_string(r));
}
}  // namespace
void writePostShutterRzsl(rawrcam::vulkan::VulkanContext& context, std::mutex& /*queueSubmitMutex*/,
                          const std::string& filesDir, std::uint32_t cfa,
                          const std::vector<rawr::raw_gpu_pipeline::BurstFrame>& sourceFrames,
                          const std::vector<rawrcam::metadata::FrameMetadataSnapshot>& metadata,
                          const std::vector<float>& sharpnessScores, const std::string& replayMetadata) {
    if (sourceFrames.empty() || sourceFrames.size() != metadata.size()) {
        throw std::invalid_argument("post-shutter RZSL source mismatch");
    }
    // Audit-only scores: all-or-nothing so replay never mixes stored and
    // recomputed rankings within one burst. A mismatch silently drops the
    // keys; the dump itself must not fail over audit data.
    bool emitScores = sharpnessScores.size() == sourceFrames.size();
    for (float score : sharpnessScores) {
        if (!std::isfinite(score)) {
            emitScores = false;
            break;
        }
    }
    std::vector<VkImageView> rawViews;
    rawViews.reserve(sourceFrames.size());
    for (const auto& frame : sourceFrames) rawViews.push_back(frame.raw.view);

    rawr::zsl_codec::VulkanCodec codec;
    codec.initialize(context.physicalDevice(), context.device(), context.timestampPeriod(),
                     sourceFrames.front().raw.ref.width, sourceFrames.front().raw.ref.height, rawViews, false);
    auto submit = [&](const VkSubmitInfo& info, VkFence fence) {
        vkCheck(context.primaryQueue().submit(info, fence), "post-shutter RZSL queue submit");
    };
    rawr::zsl_ring::ZslRing packetRing(context.physicalDevice(), context.device(), context.queueFamily(), submit,
                                       sourceFrames.size());

    struct Commands {
        VkDevice device = VK_NULL_HANDLE;
        VkCommandPool pool = VK_NULL_HANDLE;
        VkCommandBuffer command = VK_NULL_HANDLE;
        VkFence fence = VK_NULL_HANDLE;
        ~Commands() {
            if (device && fence) vkDestroyFence(device, fence, nullptr);
            if (device && pool) vkDestroyCommandPool(device, pool, nullptr);
        }
    } commands;
    commands.device = context.device();
    VkCommandPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    poolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    poolInfo.queueFamilyIndex = context.queueFamily();
    vkCheck(vkCreateCommandPool(commands.device, &poolInfo, nullptr, &commands.pool), "post-shutter RZSL command pool");
    VkCommandBufferAllocateInfo commandInfo{};
    commandInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    commandInfo.commandPool = commands.pool;
    commandInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    commandInfo.commandBufferCount = 1;
    vkCheck(vkAllocateCommandBuffers(commands.device, &commandInfo, &commands.command),
            "post-shutter RZSL command buffer");
    VkFenceCreateInfo fenceInfo{};
    fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    fenceInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;
    vkCheck(vkCreateFence(commands.device, &fenceInfo, nullptr, &commands.fence), "post-shutter RZSL fence");

    for (std::size_t i = 0; i < sourceFrames.size(); ++i) {
        vkCheck(vkWaitForFences(commands.device, 1, &commands.fence, VK_TRUE, UINT64_MAX),
                "post-shutter RZSL prior fence");
        vkCheck(vkResetFences(commands.device, 1, &commands.fence), "post-shutter RZSL reset fence");
        vkCheck(vkResetCommandBuffer(commands.command, 0), "post-shutter RZSL reset command");
        VkCommandBufferBeginInfo begin{};
        begin.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        vkCheck(vkBeginCommandBuffer(commands.command, &begin), "post-shutter RZSL begin");
        (void)codec.record(commands.command, static_cast<std::uint32_t>(i), sourceFrames[i].raw.ref.timestampNs);
        vkCheck(vkEndCommandBuffer(commands.command), "post-shutter RZSL end");
        VkSubmitInfo submitInfo{};
        submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        submitInfo.commandBufferCount = 1;
        submitInfo.pCommandBuffers = &commands.command;
        submit(submitInfo, commands.fence);
        vkCheck(vkWaitForFences(commands.device, 1, &commands.fence, VK_TRUE, UINT64_MAX),
                "post-shutter RZSL encode fence");
        const auto encoded = codec.poll(static_cast<std::uint32_t>(i));
        if (!encoded || !encoded->validPackedFrame()) {
            throw std::runtime_error("post-shutter RZSL encoder rejected frame");
        }
        const auto& gpu = encoded->gpuPacket;
        const rawr::zsl_ring::PacketSource packet{gpu.meta,
                                                  gpu.sizes,
                                                  gpu.offsets,
                                                  gpu.payload,
                                                  gpu.streams,
                                                  encoded->width,
                                                  encoded->height,
                                                  encoded->tilesX,
                                                  encoded->tilesY,
                                                  gpu.tableBytes,
                                                  encoded->payloadUsedBytes};
        if (!packetRing.push(sourceFrames[i].raw.ref.frameId, sourceFrames[i].raw.ref.timestampNs, packet)) {
            throw std::runtime_error("post-shutter RZSL packet ring rejected frame");
        }
    }

    // Fence a no-op submission behind the last packet copy. Waiting happens
    // without holding the app queue mutex, so preview remains free to submit.
    vkCheck(vkResetFences(commands.device, 1, &commands.fence), "post-shutter RZSL final reset fence");
    vkCheck(vkResetCommandBuffer(commands.command, 0), "post-shutter RZSL final reset command");
    VkCommandBufferBeginInfo begin{};
    begin.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkCheck(vkBeginCommandBuffer(commands.command, &begin), "post-shutter RZSL final begin");
    vkCheck(vkEndCommandBuffer(commands.command), "post-shutter RZSL final end");
    VkSubmitInfo finalSubmit{};
    finalSubmit.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    finalSubmit.commandBufferCount = 1;
    finalSubmit.pCommandBuffers = &commands.command;
    submit(finalSubmit, commands.fence);
    vkCheck(vkWaitForFences(commands.device, 1, &commands.fence, VK_TRUE, UINT64_MAX), "post-shutter RZSL final fence");

    auto refs = packetRing.snapshot();
    if (refs.size() != sourceFrames.size()) {
        packetRing.releaseSnapshot(refs);
        throw std::runtime_error("post-shutter RZSL packet count mismatch");
    }
    std::vector<rawrcam::encoding::rzsl::RzslBundleSink::Frame> frames;
    std::vector<std::string> serializedMetadata;
    frames.reserve(refs.size());
    serializedMetadata.reserve(refs.size());
    for (std::size_t i = 0; i < refs.size(); ++i) {
        frames.push_back(rawrcam::encoding::rzsl::RzslBundleSink::Frame{
            refs[i].frameId, refs[i].timestampNs, refs[i].width, refs[i].height, refs[i].tilesX, refs[i].tilesY,
            refs[i].streams, static_cast<std::uint32_t>(refs[i].tableBytes),
            static_cast<std::uint32_t>(refs[i].payloadBytes)});
        std::string frameMetadata =
            rawrcam::encoding::rzsl::RzslBundleSink::serializeMetadata(refs[i].frameId, metadata[i]) + replayMetadata;
        if (emitScores) {
            std::ostringstream scoreLine;
            scoreLine << std::setprecision(9) << "sharpnessScore\t" << sharpnessScores[i] << '\n';
            frameMetadata += scoreLine.str();
        }
        serializedMetadata.push_back(std::move(frameMetadata));
    }
    auto reader = packetRing.makePacketReaderSession(refs);
    if (!reader) {
        packetRing.releaseSnapshot(refs);
        throw std::runtime_error("post-shutter RZSL packet reader unavailable");
    }
    rawrcam::encoding::rzsl::RzslBundleSink::writeBundle(
        filesDir, cfa, frames, serializedMetadata,
        [&reader](std::uint64_t frameId, std::vector<std::uint8_t>& out) { return reader->read(frameId, out); });
    packetRing.releaseSnapshot(refs);
}
}  // namespace rawrcam::capture::multiframe
