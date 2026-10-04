#include "MonitoringOverlayProcessor.h"

#include <algorithm>
#include <stdexcept>

#include "monitor_combined.h"
#include "monitor_false_color.h"
#include "monitor_focus.h"
#include "monitor_raw_state.h"
#include "monitor_tonemap_shadow.h"
#include "monitoring_overlays/monitoring_overlays.h"

namespace rawrcam::monitoring {
namespace {
void vkCheck(VkResult result, const char* what) {
    if (result != VK_SUCCESS) throw std::runtime_error(what);
}

void computeWriteToComputeRead(VkCommandBuffer command, VkImage image) {
    VkImageMemoryBarrier barrier{};
    barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
    barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    barrier.oldLayout = VK_IMAGE_LAYOUT_GENERAL;
    barrier.newLayout = VK_IMAGE_LAYOUT_GENERAL;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = image;
    barrier.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    vkCmdPipelineBarrier(command, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 0,
                         nullptr, 0, nullptr, 1, &barrier);
}
}  // namespace

MonitoringOverlayProcessor::MonitoringOverlayProcessor() = default;

MonitoringOverlayProcessor::~MonitoringOverlayProcessor() { reset(); }

void MonitoringOverlayProcessor::initialize(VkPhysicalDevice physicalDevice, VkDevice device, VkQueue queue,
                                            VkCommandPool commandPool, uint32_t width, uint32_t height) {
    reset();
    if (physicalDevice == VK_NULL_HANDLE || device == VK_NULL_HANDLE || queue == VK_NULL_HANDLE ||
        commandPool == VK_NULL_HANDLE || width == 0 || height == 0) {
        throw std::invalid_argument("MonitoringOverlayProcessor invalid initialization");
    }
    device_ = device;
    width_ = width;
    height_ = height;

    monitoring_overlays::MonitoringOverlaysCreateInfo ci{};
    ci.context.physicalDevice = physicalDevice;
    ci.context.device = device;
    ci.rawStateShader = {reinterpret_cast<const uint32_t*>(monitor_raw_state_spv), monitor_raw_state_spv_size};
    ci.focusPeakingShader = {reinterpret_cast<const uint32_t*>(monitor_focus_spv), monitor_focus_spv_size};
    ci.falseColorShader = {reinterpret_cast<const uint32_t*>(monitor_false_color_spv), monitor_false_color_spv_size};
    ci.tonemapShadowShader = {reinterpret_cast<const uint32_t*>(monitor_tonemap_shadow_spv),
                              monitor_tonemap_shadow_spv_size};
    ci.combinedShader = {reinterpret_cast<const uint32_t*>(monitor_combined_spv), monitor_combined_spv_size};
    ci.maxFramesInFlight = rawrcam::imaging::kRealtimeFramesInFlight;
    backend_ = std::make_unique<monitoring_overlays::MonitoringOverlays>(ci);

    for (uint32_t i = 0; i < rawrcam::imaging::kRealtimeFramesInFlight; ++i) {
        rawState_[i] = rawrcam::vulkan::createOwnedImage(physicalDevice, device, width, height, VK_FORMAT_R16_UINT,
                                                         VK_IMAGE_USAGE_STORAGE_BIT);
        overlay_[i] = rawrcam::vulkan::createOwnedImage(physicalDevice, device, width, height, VK_FORMAT_R8G8B8A8_UNORM,
                                                        VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT);
    }
    transitionImagesToGeneral(queue, commandPool);
}

void MonitoringOverlayProcessor::reset() noexcept {
    backend_.reset();
    if (device_ != VK_NULL_HANDLE) {
        for (auto& image : rawState_) rawrcam::vulkan::destroyOwnedImage(device_, image);
        for (auto& image : overlay_) rawrcam::vulkan::destroyOwnedImage(device_, image);
    }
    device_ = VK_NULL_HANDLE;
    width_ = 0;
    height_ = 0;
}

void MonitoringOverlayProcessor::setFocusSensitivity(float sensitivity) noexcept {
    focusSensitivity_ = std::clamp(sensitivity, 0.0f, 1.0f);
}

VkImageView MonitoringOverlayProcessor::rawStateView(uint32_t frameSlot) const {
    if (frameSlot >= rawState_.size()) return VK_NULL_HANDLE;
    return rawState_[frameSlot].view;
}
VkImageView MonitoringOverlayProcessor::overlayView(uint32_t frameSlot) const {
    if (frameSlot >= overlay_.size()) return VK_NULL_HANDLE;
    return overlay_[frameSlot].view;
}
VkImage MonitoringOverlayProcessor::overlayImage(uint32_t frameSlot) const {
    if (frameSlot >= overlay_.size()) return VK_NULL_HANDLE;
    return overlay_[frameSlot].image;
}

void MonitoringOverlayProcessor::record(VkCommandBuffer command, uint32_t frameSlot, VkImageView linearView,
                                        VkImage linearImage, VkImageView tonemappedView, VkImage tonemappedImage,
                                        float shadowsUI, float blacksUI) {
    if (!backend_ || frameSlot >= rawrcam::imaging::kRealtimeFramesInFlight) return;
    if (!layers_.any()) return;
    (void)linearView;
    (void)linearImage;

    monitoring_overlays::CombinedRecordInfo info{};
    info.commandBuffer = command;
    info.frameSlot = frameSlot;
    info.output = {overlay_[frameSlot].view, VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_LAYOUT_GENERAL, width_, height_};
    info.focusParams.enabled = false;
    info.falseColorParams.enabled = false;
    info.tonemapShadowParams.enabled = false;
    info.rawParams.highlightEnabled = false;
    info.rawParams.shadowWarningEnabled = false;
    info.rawParams.shadowClippedEnabled = false;

    const bool needsDisplay = layers_.focusPeaking || layers_.falseColor || layers_.tonemapShadows;
    if (needsDisplay) {
        computeWriteToComputeRead(command, tonemappedImage);
        info.display = {tonemappedView, VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_LAYOUT_GENERAL, width_, height_};
    }
    if (layers_.rawHighlights) {
        computeWriteToComputeRead(command, rawState_[frameSlot].image);
        info.rawState = {rawState_[frameSlot].view, VK_FORMAT_R16_UINT, VK_IMAGE_LAYOUT_GENERAL, width_, height_};
        info.rawParams.highlightEnabled = true;
    }
    if (layers_.focusPeaking) {
        info.focusParams.enabled = true;
        info.focusParams.sensitivity = focusSensitivity_;
    }
    if (layers_.falseColor) {
        info.falseColorParams = monitoring_overlays::FalseColorParams::defaultPreset();
        info.falseColorParams.enabled = true;
    }
    if (layers_.tonemapShadows) {
        info.tonemapShadowParams.enabled = true;
        info.tonemapShadowParams.shadowsUI = std::clamp(shadowsUI, -100.0f, 100.0f);
        info.tonemapShadowParams.blacksUI = std::clamp(blacksUI, -100.0f, 100.0f);
    }
    backend_->recordCombined(info);
}

void MonitoringOverlayProcessor::transitionImagesToGeneral(VkQueue queue, VkCommandPool commandPool) {
    VkCommandBuffer command = VK_NULL_HANDLE;
    VkCommandBufferAllocateInfo ai{};
    ai.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    ai.commandPool = commandPool;
    ai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    ai.commandBufferCount = 1;
    vkCheck(vkAllocateCommandBuffers(device_, &ai, &command), "monitor allocate transition command");
    VkCommandBufferBeginInfo bi{};
    bi.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkCheck(vkBeginCommandBuffer(command, &bi), "monitor begin transition command");
    std::array<VkImageMemoryBarrier, rawrcam::imaging::kRealtimeFramesInFlight * 2> barriers{};
    size_t n = 0;
    for (uint32_t i = 0; i < rawrcam::imaging::kRealtimeFramesInFlight; ++i) {
        for (VkImage image : {rawState_[i].image, overlay_[i].image}) {
            auto& b = barriers[n++];
            b.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
            b.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
            b.newLayout = VK_IMAGE_LAYOUT_GENERAL;
            b.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            b.image = image;
            b.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
            b.dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
        }
    }
    vkCmdPipelineBarrier(command, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 0,
                         nullptr, 0, nullptr, static_cast<uint32_t>(n), barriers.data());
    vkCheck(vkEndCommandBuffer(command), "monitor end transition command");
    VkSubmitInfo si{};
    si.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    si.commandBufferCount = 1;
    si.pCommandBuffers = &command;
    vkCheck(vkQueueSubmit(queue, 1, &si, VK_NULL_HANDLE), "monitor submit transition command");
    vkCheck(vkQueueWaitIdle(queue), "monitor transition wait");
    vkFreeCommandBuffers(device_, commandPool, 1, &command);
}

}  // namespace rawrcam::monitoring
