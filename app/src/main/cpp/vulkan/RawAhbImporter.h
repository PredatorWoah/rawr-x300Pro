#pragma once
#include <android/hardware_buffer.h>
#include <vulkan/vulkan.h>
#include <vulkan/vulkan_android.h>

#include <cstdint>
#include <functional>
#include <string>
#include <unordered_map>

#include "vulkan/RawImportOptions.h"
namespace rawrcam::vulkan {
struct ImportedRaw {
    AHardwareBuffer* ahb = nullptr;
    VkImage image = VK_NULL_HANDLE;
    VkDeviceMemory memory = VK_NULL_HANDLE;
    VkImageView view = VK_NULL_HANDLE;
    // Diagnostic-only TRANSFER_SRC alias used when the primary zero-copy
    // candidate is STORAGE-only. Both images reference the same camera AHB.
    VkImage referenceTransferImage = VK_NULL_HANDLE;
    VkDeviceMemory referenceTransferMemory = VK_NULL_HANDLE;
    VkImageView referenceTransferView = VK_NULL_HANDLE;
    bool referenceTransferAvailable = false;
    // Diagnostic-only second import of the same camera AHB as a LINEAR image.
    // This is never used by production preview; it exists solely to test whether
    // legal linear-tiled addressing can provide full-frame zero-copy RAW16.
    VkImage linearImage = VK_NULL_HANDLE;
    VkDeviceMemory linearMemory = VK_NULL_HANDLE;
    VkImageView linearView = VK_NULL_HANDLE;
    bool linearCandidateAvailable = false;
    // fix54 diagnostic-only Android external-format import. The VkImage remains
    // bound to the SAME camera AHB; the conversion/view/sampler are owned by the
    // diagnostics layer so production import ownership stays unchanged.
    VkImage externalFormatImage = VK_NULL_HANDLE;
    VkDeviceMemory externalFormatMemory = VK_NULL_HANDLE;
    bool externalFormatCandidateAvailable = false;
    uint64_t externalFormat = 0;
    VkFormatFeatureFlags externalFormatFeatures = 0;
    VkComponentMapping externalComponents{};
    VkSamplerYcbcrModelConversion externalSuggestedModel = VK_SAMPLER_YCBCR_MODEL_CONVERSION_RGB_IDENTITY;
    VkSamplerYcbcrRange externalSuggestedRange = VK_SAMPLER_YCBCR_RANGE_ITU_FULL;
    VkChromaLocation externalSuggestedX = VK_CHROMA_LOCATION_COSITED_EVEN;
    VkChromaLocation externalSuggestedY = VK_CHROMA_LOCATION_COSITED_EVEN;

    // fix54 diagnostic-only DRM-modifier import of the SAME AHB. This is only
    // populated when a genuinely non-linear modifier survives the format/external
    // memory intersection query and bind succeeds.
    VkImage drmImage = VK_NULL_HANDLE;
    VkDeviceMemory drmMemory = VK_NULL_HANDLE;
    VkImageView drmView = VK_NULL_HANDLE;
    VkImageUsageFlags drmUsage = 0;
    bool drmCandidateAvailable = false;

    // fix54 diagnostic-only transfer destination. This is deliberately app-owned
    // memory: it measures vkCmdCopyImageToBuffer as a fallback, not zero-copy.
    VkBuffer copyBuffer = VK_NULL_HANDLE;
    VkDeviceMemory copyBufferMemory = VK_NULL_HANDLE;
    bool copyBufferAvailable = false;
    // Buffer-view zero-copy candidate (diagnostic mode 16): second dedicated
    // import of the SAME camera AHB bound to a VkBuffer. Shader-side byte
    // math (y * stride + x) bypasses sampler pitch granularity, which the
    // texture unit cannot satisfy for odd camera strides (e.g. 4080x16bpp
    // rows are 8160B, but sampled lines walk at 64B granularity). Lazily
    // populated by ensureBufferImport(); never throws.
    VkBuffer importBuffer = VK_NULL_HANDLE;
    VkDeviceMemory importBufferMemory = VK_NULL_HANDLE;
    uint32_t importBufferStridePixels = 0;
    bool importBufferAvailable = false;
    bool importBufferAttempted = false;
    bool importBufferOwnsAcquire = false;
    // App-owned CPU upload (pipeline/RawCpuUploadPool): image/view and
    // importBuffer are not camera memory and must not use foreign barriers.
    bool cpuUploaded = false;
    AHardwareBuffer_Desc desc{};
};
class RawAhbImporter {
   public:
    using Diagnostic = std::function<void(const std::string&)>;
    RawAhbImporter(VkPhysicalDevice physical, VkDevice device, uint32_t queueFamily,
                   PFN_vkGetAndroidHardwareBufferPropertiesANDROID getProperties, Diagnostic diagnostic);
    ~RawAhbImporter();
    uint64_t requiredHardwareBufferUsage(VkImageUsageFlags usage) const;
    ImportedRaw& import(AHardwareBuffer* ahb, uint32_t expectedWidth, uint32_t expectedHeight, VkImageUsageFlags usage,
                        RawImportOptions options = {});
    // Idempotent per-AHB second import bound to a storage buffer (see
    // ImportedRaw::importBuffer). Operates on an existing image-import entry
    // only; never throws. Failures mark the entry unavailable with a
    // diagnostic line so the bridge fallback continues undisturbed.
    void ensureBufferImport(AHardwareBuffer* ahb) noexcept;
    void clear() noexcept;
    size_t size() const noexcept { return imported_.size(); }

   private:
    VkPhysicalDevice physical_;
    VkDevice device_;
    uint32_t queueFamily_;
    PFN_vkGetAndroidHardwareBufferPropertiesANDROID getProperties_;
    Diagnostic diagnostic_;
    std::unordered_map<uintptr_t, ImportedRaw> imported_;
};
}  // namespace rawrcam::vulkan
