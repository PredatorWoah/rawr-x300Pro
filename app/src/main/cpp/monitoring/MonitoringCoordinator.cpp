#include "MonitoringCoordinator.h"

#include <algorithm>
#include <cmath>
#include <utility>

namespace rawrcam::monitoring {

MonitoringCoordinator::MonitoringCoordinator(Diagnostic diagnostic) : diagnostic_(std::move(diagnostic)) {}

MonitoringCoordinator::~MonitoringCoordinator() = default;

void MonitoringCoordinator::emit(const std::string& line) const {
    if (diagnostic_) diagnostic_(line);
}

void MonitoringCoordinator::initialize(VkPhysicalDevice physicalDevice, VkDevice device, VkQueue queue,
                                       VkCommandPool commandPool, uint32_t queueFamilyIndex, float timestampPeriodNs,
                                       uint32_t rawWidth, uint32_t rawHeight, uint32_t previewWidth,
                                       uint32_t previewHeight) {
    (void)rawWidth;
    (void)rawHeight;
    monitoringOverlay_.initialize(physicalDevice, device, queue, commandPool, previewWidth, previewHeight);
    imageScopes_.initialize(physicalDevice, device, queue, commandPool, queueFamilyIndex, timestampPeriodNs,
                            previewWidth, previewHeight, [this](const std::string& line) { emit(line); });
}

void MonitoringCoordinator::reset() noexcept {
    monitoringOverlay_.reset();
    imageScopes_.reset();
}

void MonitoringCoordinator::setOverlay(uint32_t layers, float focusSensitivity) {
    layers &= (kLayerFocusPeaking | kLayerRawHighlights | kLayerTonemapShadows | kLayerFalseColor);
    if (!std::isfinite(focusSensitivity)) focusSensitivity = 0.50f;
    focusSensitivity = std::clamp(focusSensitivity, 0.0f, 1.0f);
    monitoringOverlay_.setLayers(decodeLayers(layers));
    monitoringOverlay_.setFocusSensitivity(focusSensitivity);
    emit("MONITORING_OVERLAY layers=" + std::to_string(layers) + " focusSensitivity=" +
         std::to_string(focusSensitivity));
}

void MonitoringCoordinator::setScopePresentationState(const ScopePresentationState& state) noexcept {
    imageScopes_.setPresentationState(state);
}

}  // namespace rawrcam::monitoring
