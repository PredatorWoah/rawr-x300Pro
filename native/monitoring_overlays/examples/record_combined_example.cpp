#include "monitoring_overlays/monitoring_overlays.h"

using namespace monitoring_overlays;

// Application-side example. The caller has already recorded raw_preview and
// TonemapEngine and inserted producer->compute visibility barriers.
void recordMonitoring(MonitoringOverlays& monitoring, VkCommandBuffer cmd, VkImageView rawStateView,
                      VkImageView tonemapView, VkImageView overlayView, uint32_t width, uint32_t height,
                      uint32_t frameSlot) {
    CombinedRecordInfo r{};
    r.commandBuffer = cmd;
    r.rawState = {rawStateView, VK_FORMAT_R16_UINT, VK_IMAGE_LAYOUT_GENERAL, width, height};
    r.display = {tonemapView, VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_LAYOUT_GENERAL, width, height};
    r.output = {overlayView, VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_LAYOUT_GENERAL, width, height};
    r.frameSlot = frameSlot;

    // Defaults enable the standard monitoring presentation.
    // Application policy may turn individual modes on/off.
    r.focusParams.sensitivity = 0.55f;

    monitoring.recordCombined(r);
}
