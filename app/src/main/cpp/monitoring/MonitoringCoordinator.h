#pragma once

#include <vulkan/vulkan.h>

#include <cstdint>
#include <functional>
#include <string>

#include "ImageScopesProcessor.h"
#include "MonitoringOverlayProcessor.h"
#include "MonitoringState.h"

namespace rawrcam::monitoring {

class MonitoringCoordinator final {
   public:
    using Diagnostic = std::function<void(const std::string&)>;

    explicit MonitoringCoordinator(Diagnostic diagnostic = {});
    ~MonitoringCoordinator();

    MonitoringCoordinator(const MonitoringCoordinator&) = delete;
    MonitoringCoordinator& operator=(const MonitoringCoordinator&) = delete;

    void initialize(VkPhysicalDevice physicalDevice, VkDevice device, VkQueue queue, VkCommandPool commandPool,
                    uint32_t queueFamilyIndex, float timestampPeriodNs, uint32_t rawWidth, uint32_t rawHeight,
                    uint32_t previewWidth, uint32_t previewHeight);
    void reset() noexcept;

    void setOverlay(uint32_t layers, float focusSensitivity);
    void setScopePresentationState(const ScopePresentationState& state) noexcept;

    [[nodiscard]] MonitoringOverlayProcessor& overlay() noexcept { return monitoringOverlay_; }
    [[nodiscard]] const MonitoringOverlayProcessor& overlay() const noexcept { return monitoringOverlay_; }
    [[nodiscard]] ImageScopesProcessor& scopes() noexcept { return imageScopes_; }
    [[nodiscard]] const ImageScopesProcessor& scopes() const noexcept { return imageScopes_; }

   private:
    void emit(const std::string& line) const;

    Diagnostic diagnostic_;
    MonitoringOverlayProcessor monitoringOverlay_;
    ImageScopesProcessor imageScopes_;
};

}  // namespace rawrcam::monitoring
