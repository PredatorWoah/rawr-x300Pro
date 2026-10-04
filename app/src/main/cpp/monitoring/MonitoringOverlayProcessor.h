#pragma once

#include <vulkan/vulkan.h>

#include <array>
#include <cstdint>
#include <memory>

#include "imaging/FrameLimits.h"
#include "vulkan/ImageResources.h"

namespace monitoring_overlays {
class MonitoringOverlays;
}

namespace rawrcam::monitoring {

// Interaction-gated overlay layers, transmitted as a bitmask (see kLayer*).
// Multiple layers may be active at once; record() composites all enabled
// layers in deterministic false-color -> focus -> tonemap-shadow ->
// RAW-highlight order via a single CombinedRecordInfo.
inline constexpr uint32_t kLayerFocusPeaking = 1u << 0;
inline constexpr uint32_t kLayerRawHighlights = 1u << 1;
inline constexpr uint32_t kLayerTonemapShadows = 1u << 2;
inline constexpr uint32_t kLayerFalseColor = 1u << 3;

struct EnabledLayers {
    bool focusPeaking = false;
    bool rawHighlights = false;
    bool tonemapShadows = false;
    bool falseColor = false;

    [[nodiscard]] bool any() const noexcept { return focusPeaking || rawHighlights || tonemapShadows || falseColor; }
};

[[nodiscard]] inline EnabledLayers decodeLayers(uint32_t bits) noexcept {
    return {(bits & kLayerFocusPeaking) != 0, (bits & kLayerRawHighlights) != 0, (bits & kLayerTonemapShadows) != 0,
            (bits & kLayerFalseColor) != 0};
}

class MonitoringOverlayProcessor final {
   public:
    MonitoringOverlayProcessor();
    ~MonitoringOverlayProcessor();
    MonitoringOverlayProcessor(const MonitoringOverlayProcessor&) = delete;
    MonitoringOverlayProcessor& operator=(const MonitoringOverlayProcessor&) = delete;

    void initialize(VkPhysicalDevice physicalDevice, VkDevice device, VkQueue queue, VkCommandPool commandPool,
                    uint32_t width, uint32_t height);
    void reset() noexcept;

    void setLayers(EnabledLayers layers) noexcept { layers_ = layers; }
    [[nodiscard]] bool needsRawState() const noexcept { return layers_.rawHighlights; }
    [[nodiscard]] bool enabled() const noexcept { return layers_.any(); }

    void setFocusSensitivity(float sensitivity) noexcept;

    [[nodiscard]] VkImageView rawStateView(uint32_t frameSlot) const;
    [[nodiscard]] VkImageView overlayView(uint32_t frameSlot) const;
    [[nodiscard]] VkImage overlayImage(uint32_t frameSlot) const;

    void record(VkCommandBuffer command, uint32_t frameSlot, VkImageView linearView, VkImage linearImage,
                VkImageView tonemappedView, VkImage tonemappedImage, float shadowsUI = 0.0f, float blacksUI = 0.0f);

   private:
    void transitionImagesToGeneral(VkQueue queue, VkCommandPool commandPool);

    VkDevice device_ = VK_NULL_HANDLE;
    uint32_t width_ = 0;
    uint32_t height_ = 0;
    EnabledLayers layers_{};
    float focusSensitivity_ = 0.50f;
    std::unique_ptr<monitoring_overlays::MonitoringOverlays> backend_;
    std::array<rawrcam::vulkan::OwnedImage, rawrcam::imaging::kRealtimeFramesInFlight> rawState_{};
    std::array<rawrcam::vulkan::OwnedImage, rawrcam::imaging::kRealtimeFramesInFlight> overlay_{};
};

}  // namespace rawrcam::monitoring
