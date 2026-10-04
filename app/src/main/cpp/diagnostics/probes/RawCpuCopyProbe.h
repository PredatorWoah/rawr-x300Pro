#pragma once

#include <android/hardware_buffer.h>
#include <media/NdkImage.h>

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace rawrcam::diagnostics {

// Diagnostic-only feasibility probe for the future still-capture snapshot path.
// Owns its packed RAW16 destination and copy/timing statistics. It does not own
// AImage acquisition, preview submission, Camera2 state, or DNG serialization.
class RawCpuCopyProbe final {
   public:
    using Diagnostic = std::function<void(const std::string&)>;

    explicit RawCpuCopyProbe(Diagnostic diagnostic = {});

    void setRequestedFrames(uint32_t frames) noexcept;
    [[nodiscard]] uint32_t requestedFrames() const noexcept { return requestedFrames_; }
    [[nodiscard]] bool requested() const noexcept { return requestedFrames_ != 0; }

    // Called when authoritative RAW geometry is configured, before reader creation.
    // Allocation is intentionally outside the measured frame-copy path.
    void configure(uint32_t width, uint32_t height);
    void disable(const std::string& reason);
    void reset() noexcept;

    [[nodiscard]] bool enabled() const noexcept;
    [[nodiscard]] bool needsSample() const noexcept;

    // Temporarily transfers the camera AHB to CPU-read ownership, performs a
    // tightly packed RAW16 snapshot, then unlocks it before returning. On
    // success, gpuAcquireFenceFd receives the CPU-unlock release fence that the
    // existing Vulkan submission must wait on (-1 means immediately available).
    // acquireFenceFd remains caller-owned and is never closed here.
    bool sample(AImage* image, AHardwareBuffer* ahb, int acquireFenceFd, uint64_t timestampNs, int* gpuAcquireFenceFd);

   private:
    void emitSummary(const char* verdict);
    void emit(const std::string& line) const;

    Diagnostic diagnostic_;
    uint32_t requestedFrames_ = 0;
    uint32_t width_ = 0;
    uint32_t height_ = 0;
    std::vector<uint8_t> packedRaw_;
    uint32_t attempts_ = 0;
    uint32_t successes_ = 0;
    uint32_t failures_ = 0;
    double totalLockMs_ = 0.0;
    double totalCopyMs_ = 0.0;
    double totalUnlockMs_ = 0.0;
    double minCopyMs_ = 0.0;
    double maxCopyMs_ = 0.0;
    uint64_t lastChecksum_ = 0;
    bool disabled_ = false;
    bool summaryEmitted_ = false;
};

}  // namespace rawrcam::diagnostics
