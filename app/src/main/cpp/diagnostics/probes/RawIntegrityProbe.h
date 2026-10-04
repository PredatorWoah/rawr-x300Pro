#pragma once

#include <vulkan/vulkan.h>

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace rawrcam::diagnostics {

class RawIntegrityProbe final {
   public:
    struct BufferPreviewParams {
        float black[4] = {0, 0, 0, 0};
        float whiteLevel = 65535.0f;
        float whiteBalance[4] = {1, 1, 1, 1};
        float clipThreshold = 0.995f;
        float edgeStrength = 8.0f;
        float chromaBlend = 0.20f;
        float highlightWarningThreshold = 0.98f;
        float shadowWarningThreshold = 0.01f;
        uint32_t pattern = 0;
    };

    struct AdvancedCandidates {
        VkImage drmImage = VK_NULL_HANDLE;
        VkImageView drmView = VK_NULL_HANDLE;
        VkImageUsageFlags drmUsage = 0;
        bool drmAvailable = false;
        VkBuffer copyBuffer = VK_NULL_HANDLE;
        bool copyBufferAvailable = false;
        VkImage externalFormatImage = VK_NULL_HANDLE;
        bool externalFormatAvailable = false;
        uint64_t externalFormat = 0;
        VkFormatFeatureFlags externalFormatFeatures = 0;
        VkComponentMapping externalComponents{};
        VkSamplerYcbcrModelConversion externalModel = VK_SAMPLER_YCBCR_MODEL_CONVERSION_RGB_IDENTITY;
        VkSamplerYcbcrRange externalRange = VK_SAMPLER_YCBCR_RANGE_ITU_FULL;
        VkChromaLocation externalX = VK_CHROMA_LOCATION_COSITED_EVEN;
        VkChromaLocation externalY = VK_CHROMA_LOCATION_COSITED_EVEN;
    };

    using Diagnostic = std::function<void(const std::string&)>;

    explicit RawIntegrityProbe(Diagnostic diagnostic);
    ~RawIntegrityProbe();

    RawIntegrityProbe(const RawIntegrityProbe&) = delete;
    RawIntegrityProbe& operator=(const RawIntegrityProbe&) = delete;

    void initialize(VkPhysicalDevice physicalDevice, VkDevice device, uint32_t rawWidth, uint32_t rawHeight,
                    uint32_t outputWidth, uint32_t outputHeight, uint32_t frameSlotCount);
    void reset();

    [[nodiscard]] bool readyForCapture() const noexcept {
        return resources_.pipeline != VK_NULL_HANDLE && !done_ && pendingSlot_ < 0;
    }

    void beginProductionRawBenchmark(VkCommandBuffer command, uint32_t slotIndex);
    void endProductionRawBenchmark(VkCommandBuffer command, uint32_t slotIndex);
    void recordBufferPreviewBenchmark(VkCommandBuffer command, uint32_t slotIndex, VkImage sourceImage,
                                      VkBuffer copyBuffer, const BufferPreviewParams& params);

    // Buffer-view zero-copy parity (diagnostic mode 16): full-frame compare
    // of byte-addressed imported-AHB reads against the bridge-owned R16
    // reference. Standalone one-shot state machine; independent of the main
    // dense/full probe above.
    void recordBufferImportParity(VkCommandBuffer command, VkImageView referenceView, VkBuffer importBuffer,
                                  uint32_t stridePixels, uint32_t slotIndex, uint64_t timestampNs);

    void record(VkCommandBuffer command, VkImageView rawView, VkImageView referenceRawView, bool compareRawReference,
                VkImageView linearCandidateView, bool linearCandidateAvailable, const AdvancedCandidates& advanced,
                VkImageView linearView, VkImageView displayView, VkImage linearImage, VkImage displayImage,
                uint32_t slotIndex, uint64_t timestampNs);

    void complete(uint32_t slotIndex);

   private:
    struct Resources {
        VkDescriptorSetLayout dsl = VK_NULL_HANDLE;
        VkPipelineLayout layout = VK_NULL_HANDLE;
        VkPipeline pipeline = VK_NULL_HANDLE;
        VkPipeline fullComparePipeline = VK_NULL_HANDLE;
        VkPipeline pathPipeline = VK_NULL_HANDLE;
        VkDescriptorSetLayout externalDsl = VK_NULL_HANDLE;
        VkPipelineLayout externalLayout = VK_NULL_HANDLE;
        VkPipeline externalPipeline = VK_NULL_HANDLE;
        VkDescriptorPool externalPool = VK_NULL_HANDLE;
        VkDescriptorSet externalSet = VK_NULL_HANDLE;
        VkSamplerYcbcrConversion externalConversion = VK_NULL_HANDLE;
        VkSampler externalSampler = VK_NULL_HANDLE;
        VkImageView externalView = VK_NULL_HANDLE;
        VkImage externalViewImage = VK_NULL_HANDLE;
        uint64_t externalViewFormat = 0;
        VkDescriptorSetLayout bufferPreviewDsl = VK_NULL_HANDLE;
        VkPipelineLayout bufferPreviewLayout = VK_NULL_HANDLE;
        VkPipeline bufferPreviewPipeline = VK_NULL_HANDLE;
        VkDescriptorPool bufferPreviewPool = VK_NULL_HANDLE;
        VkQueryPool bufferBenchmarkQueryPool = VK_NULL_HANDLE;
        VkSampler rawSampler = VK_NULL_HANDLE;
        VkDescriptorPool pool = VK_NULL_HANDLE;
        VkDescriptorSet set = VK_NULL_HANDLE;
        VkBuffer buffer = VK_NULL_HANDLE;
        VkDeviceMemory memory = VK_NULL_HANDLE;
        void* mapped = nullptr;
        bool coherent = false;
        VkDeviceSize allocationSize = 0;
        // Buffer-import parity resources (mode 16). Probe buffer holds 8
        // uint32 stats words: diff, maxAbs, 4 parity quadrants, reserved,
        // firstMismatch, validCount(unused, samples reported from CPU side).
        VkDescriptorSetLayout bufImportDsl = VK_NULL_HANDLE;
        VkPipelineLayout bufImportLayout = VK_NULL_HANDLE;
        VkPipeline bufImportPipeline = VK_NULL_HANDLE;
        VkDescriptorPool bufImportPool = VK_NULL_HANDLE;
        VkDescriptorSet bufImportSet = VK_NULL_HANDLE;
        VkBuffer bufImportBuffer = VK_NULL_HANDLE;
        VkDeviceMemory bufImportMemory = VK_NULL_HANDLE;
        void* bufImportMapped = nullptr;
        bool bufImportCoherent = false;
    };

    struct Push {
        uint32_t rawWidth;
        uint32_t rawHeight;
        uint32_t outputWidth;
        uint32_t outputHeight;
        uint32_t linearCandidateAvailable;
        uint32_t drmStorageAvailable;
        uint32_t drmSampledAvailable;
        uint32_t copyBufferAvailable;
        uint32_t bufferPreviewAvailable;
    };

    struct ExternalPush {
        uint32_t rawWidth;
        uint32_t rawHeight;
        uint32_t statsBase;
        uint32_t reserved;
    };

    struct BufferPreviewPush {
        float black[4];
        float invRange[4];
        float wb[4];
        float clipThreshold;
        float edgeStrength;
        float chromaBlend;
        float highlightWarningThreshold;
        float shadowWarningThreshold;
        float pad0;
        uint32_t width;
        uint32_t height;
        uint32_t pattern;
        uint32_t pad1;
    };

    struct BufferImportPush {
        uint32_t rawWidth;
        uint32_t rawHeight;
        uint32_t stridePixels;
        uint32_t reserved;
    };

    struct BufferBenchmarkSlot {
        VkImage image = VK_NULL_HANDLE;
        VkDeviceMemory memory = VK_NULL_HANDLE;
        VkImageView view = VK_NULL_HANDLE;
        VkDescriptorSet set = VK_NULL_HANDLE;
        bool layoutInitialized = false;
        bool timingPending = false;
    };

    uint32_t findMemoryType(uint32_t bits, VkMemoryPropertyFlags required, bool* coherent) const;
    uint32_t findDeviceLocalMemoryType(uint32_t bits) const;
    void initializeBufferPreviewBenchmark(uint32_t frameSlotCount);
    void initializeBufferImportParity();
    void consumeBufferPreviewBenchmark(uint32_t slotIndex);
    static float wordToFloat(uint32_t word);
    bool ensureExternalPipeline(const AdvancedCandidates& advanced);
    void emit(const std::string& line) const;

    Diagnostic diagnostic_;
    VkPhysicalDevice physicalDevice_ = VK_NULL_HANDLE;
    VkDevice device_ = VK_NULL_HANDLE;
    uint32_t rawWidth_ = 0;
    uint32_t rawHeight_ = 0;
    uint32_t outputWidth_ = 0;
    uint32_t outputHeight_ = 0;
    Resources resources_{};
    std::vector<BufferBenchmarkSlot> bufferBenchmarkSlots_;
    float timestampPeriodNs_ = 1.0f;
    uint64_t bufferBenchmarkSamples_ = 0;
    double productionRawBenchmarkNs_ = 0.0;
    double copyToBufferBenchmarkNs_ = 0.0;
    double bufferPreviewComputeNs_ = 0.0;
    double bufferPathTotalNs_ = 0.0;
    int pendingSlot_ = -1;
    uint64_t pendingTimestamp_ = 0;
    bool done_ = false;
    bool pendingRawReferenceComparison_ = false;
    bool pendingLinearCandidateAvailable_ = false;
    bool pendingDrmStorageAvailable_ = false;
    bool pendingDrmSampledAvailable_ = false;
    bool pendingCopyBufferAvailable_ = false;
    bool pendingBufferPreviewAvailable_ = false;
    bool pendingExternalCandidateAvailable_ = false;
    int pendingBufImportSlot_ = -1;
    uint64_t pendingBufImportTimestamp_ = 0;
    bool bufImportDone_ = false;
};

}  // namespace rawrcam::diagnostics
