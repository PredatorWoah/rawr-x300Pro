#pragma once
#include <array>
#include <cstdint>
#include <vulkan/vulkan.h>
#include "raw_highlight/Coloropp.hpp"
#include "raw_highlight/Spirv.hpp"
#include "rawr/shading/LensShadingMapView.h"
#include "rawr/vk/OwnedImage.h"

namespace rawr::highlight {
struct ColoroppSensorGeometry {
    uint32_t width = 0, height = 0, cropX = 0, cropY = 0, scale = 1;
};
// coloropp.comp (pre-WB build) and coloropp_tone.comp. fusedWb is the
// optional RAWR_COLOROPP_FUSED_WB build used by recordFused().
struct ColoroppShaders {
    SpirvWords coloropp;
    SpirvWords tone;
    SpirvWords fusedWb{};
};
// Inpaint Opposed (RawTherapee Coloropp) for still/video: two deliberately
// separate taps, pre-WB reconstruction and SDR-only highlight compression.
class ColoroppProcessor final {
public:
    ColoroppProcessor(VkPhysicalDevice physicalDevice, VkDevice device, const ColoroppShaders& shaders,
                      uint32_t width, uint32_t height);
    ~ColoroppProcessor();
    ColoroppProcessor(const ColoroppProcessor&) = delete;
    ColoroppProcessor& operator=(const ColoroppProcessor&) = delete;
    void recordPreWb(VkCommandBuffer command, VkImageView source,
                     const std::array<float, 3>& wb, float threshold,
                     const rawr::shading::LensShadingMapView& shading, uint32_t cfaPattern,
                     ColoroppSensorGeometry geometry = {});
    // Inpaint Opposed, white balance and the highlight passthrough in one
    // dispatch, writing post-WB RGB to target. Requires ColoroppShaders::fusedWb.
    // The caller orders target for later reads.
    void recordFused(VkCommandBuffer command, VkImageView source, VkImageView target,
                     const std::array<float, 3>& wb, float threshold,
                     const rawr::shading::LensShadingMapView& shading, uint32_t cfaPattern,
                     ColoroppSensorGeometry geometry = {});
    [[nodiscard]] bool hasFused() const noexcept { return fusedPipeline_ != VK_NULL_HANDLE; }
    void recordTone(VkCommandBuffer command, VkImageView source, float compression, float exposureGain);
    [[nodiscard]] VkImageView preWbView() const noexcept { return preWb_.view; }
    [[nodiscard]] VkImage toneImage() const noexcept { return tone_.image; }
    [[nodiscard]] VkImageView toneView() const noexcept { return tone_.view; }
private:
    void release() noexcept;
    // Uploads the shading grid and returns whether it is used.
    bool uploadShading(VkCommandBuffer command, const rawr::shading::LensShadingMapView& shading);
    ColoroppPush push(const std::array<float, 3>& wb, float threshold, bool useGrid,
                      const rawr::shading::LensShadingMapView& shading, uint32_t cfaPattern,
                      ColoroppSensorGeometry geometry) const;
    VkPhysicalDevice physicalDevice_ = VK_NULL_HANDLE;
    VkDevice device_ = VK_NULL_HANDLE;
    uint32_t width_ = 0, height_ = 0;
    rawr::vk::OwnedImage preWb_{}, tone_{};
    bool preWbInitialized_ = false, toneInitialized_ = false;
    VkBuffer shadingBuffer_ = VK_NULL_HANDLE;
    VkDeviceMemory shadingMemory_ = VK_NULL_HANDLE;
    void* shadingMapped_ = nullptr;
    VkDescriptorSetLayout preDsl_ = VK_NULL_HANDLE, toneDsl_ = VK_NULL_HANDLE;
    VkPipelineLayout preLayout_ = VK_NULL_HANDLE, toneLayout_ = VK_NULL_HANDLE;
    VkPipeline prePipeline_ = VK_NULL_HANDLE, tonePipeline_ = VK_NULL_HANDLE;
    VkDescriptorPool prePool_ = VK_NULL_HANDLE, tonePool_ = VK_NULL_HANDLE;
    VkDescriptorSet preSet_ = VK_NULL_HANDLE, toneSet_ = VK_NULL_HANDLE;
    VkDescriptorSetLayout fusedDsl_ = VK_NULL_HANDLE;
    VkPipelineLayout fusedLayout_ = VK_NULL_HANDLE;
    VkPipeline fusedPipeline_ = VK_NULL_HANDLE;
    VkDescriptorPool fusedPool_ = VK_NULL_HANDLE;
    VkDescriptorSet fusedSet_ = VK_NULL_HANDLE;
};
}  // namespace rawr::highlight
