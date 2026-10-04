#include "diagnostics/probes/RawCpuCopyProbe.h"

#include <algorithm>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <utility>

#include "imaging/Raw16CpuSnapshot.h"

namespace rawrcam::diagnostics {
namespace {
constexpr uint32_t kMaxProbeFrames = 200;
uint64_t sparseChecksum(const std::vector<uint8_t>& bytes) {
    if (bytes.empty()) return 0;
    uint64_t checksum = 1469598103934665603ull;
    constexpr size_t kSamples = 257;
    const size_t step = std::max<size_t>(1, bytes.size() / kSamples);
    for (size_t i = 0; i < bytes.size(); i += step) {
        checksum ^= bytes[i];
        checksum *= 1099511628211ull;
    }
    checksum ^= bytes.back();
    checksum *= 1099511628211ull;
    return checksum;
}
}  // namespace

RawCpuCopyProbe::RawCpuCopyProbe(Diagnostic diagnostic) : diagnostic_(std::move(diagnostic)) {}

void RawCpuCopyProbe::setRequestedFrames(uint32_t frames) noexcept {
    requestedFrames_ = std::min(frames, kMaxProbeFrames);
}

void RawCpuCopyProbe::configure(uint32_t width, uint32_t height) {
    reset();
    if (!requested()) return;
    if (width == 0 || height == 0) throw std::invalid_argument("CPU RAW copy probe geometry is empty");
    const uint64_t rowBytes = static_cast<uint64_t>(width) * sizeof(uint16_t);
    const uint64_t totalBytes = rowBytes * static_cast<uint64_t>(height);
    if (totalBytes > static_cast<uint64_t>(std::numeric_limits<size_t>::max()))
        throw std::overflow_error("CPU RAW copy probe allocation exceeds size_t");
    width_ = width;
    height_ = height;
    packedRaw_.resize(static_cast<size_t>(totalBytes));

    std::ostringstream out;
    out << "CPU_RAW_COPY_PROBE_CONFIG requestedFrames=" << requestedFrames_ << " raw=" << width_ << "x" << height_
        << " packedBytes=" << packedRaw_.size() << " allocationTiming=excluded";
    emit(out.str());
}

void RawCpuCopyProbe::disable(const std::string& reason) {
    disabled_ = true;
    emit("CPU_RAW_COPY_PROBE_DISABLED reason=" + reason);
}

void RawCpuCopyProbe::reset() noexcept {
    width_ = 0;
    height_ = 0;
    packedRaw_.clear();
    attempts_ = 0;
    successes_ = 0;
    failures_ = 0;
    totalLockMs_ = 0.0;
    totalCopyMs_ = 0.0;
    totalUnlockMs_ = 0.0;
    minCopyMs_ = 0.0;
    maxCopyMs_ = 0.0;
    lastChecksum_ = 0;
    disabled_ = false;
    summaryEmitted_ = false;
}

bool RawCpuCopyProbe::enabled() const noexcept {
    return requested() && !disabled_ && width_ != 0 && height_ != 0 && !packedRaw_.empty();
}

bool RawCpuCopyProbe::needsSample() const noexcept { return enabled() && attempts_ < requestedFrames_; }

bool RawCpuCopyProbe::sample(AImage* image, AHardwareBuffer* ahb, int acquireFenceFd, uint64_t timestampNs,
                             int* gpuAcquireFenceFd) {
    if (gpuAcquireFenceFd) *gpuAcquireFenceFd = acquireFenceFd;
    if (!needsSample() || image == nullptr || ahb == nullptr || gpuAcquireFenceFd == nullptr) return false;
    ++attempts_;

    const rawrcam::imaging::Raw16CpuSnapshotResult copied =
        rawrcam::imaging::copyRaw16AhbToPacked(image, ahb, acquireFenceFd, width_, height_, packedRaw_);
    *gpuAcquireFenceFd = copied.gpuAcquireFenceFd;
    if (!copied.success) {
        ++failures_;
        std::ostringstream out;
        out << "CPU_RAW_COPY_PROBE_FAILURE sample=" << attempts_ << " reason=" << copied.error
            << " rowStrideBytes=" << copied.sourceRowStrideBytes
            << " pixelStrideBytes=" << copied.sourcePixelStrideBytes;
        emit(out.str());
        disable(copied.error);
        emitSummary("FAIL");
        return false;
    }

    lastChecksum_ = sparseChecksum(packedRaw_);
    ++successes_;
    totalLockMs_ += copied.lockMs;
    totalCopyMs_ += copied.copyMs;
    totalUnlockMs_ += copied.unlockMs;
    minCopyMs_ = successes_ == 1 ? copied.copyMs : std::min(minCopyMs_, copied.copyMs);
    maxCopyMs_ = std::max(maxCopyMs_, copied.copyMs);

    if (attempts_ <= 5 || attempts_ % 10 == 0 || attempts_ == requestedFrames_) {
        std::ostringstream out;
        out << "CPU_RAW_COPY_PROBE_SAMPLE sample=" << attempts_ << "/" << requestedFrames_
            << " timestampNs=" << timestampNs << " rowStrideBytes=" << copied.sourceRowStrideBytes
            << " packedRowBytes=" << (static_cast<uint64_t>(width_) * sizeof(uint16_t))
            << " bytes=" << packedRaw_.size() << " lockMs=" << copied.lockMs << " copyMs=" << copied.copyMs
            << " unlockMs=" << copied.unlockMs << " gpuAcquireFence=" << copied.gpuAcquireFenceFd
            << " checksum=" << lastChecksum_;
        emit(out.str());
    }

    if (attempts_ == requestedFrames_) emitSummary(failures_ == 0 ? "PASS" : "FAIL");
    return true;
}

void RawCpuCopyProbe::emitSummary(const char* verdict) {
    if (summaryEmitted_) return;
    summaryEmitted_ = true;
    const double divisor = successes_ == 0 ? 1.0 : static_cast<double>(successes_);
    std::ostringstream out;
    out << "CPU_RAW_COPY_PROBE_COPY_" << verdict << " requested=" << requestedFrames_ << " attempts=" << attempts_
        << " successes=" << successes_ << " failures=" << failures_ << " raw=" << width_ << "x" << height_
        << " bytes=" << packedRaw_.size() << " avgLockMs=" << (successes_ == 0 ? 0.0 : totalLockMs_ / divisor)
        << " avgCopyMs=" << (successes_ == 0 ? 0.0 : totalCopyMs_ / divisor)
        << " avgUnlockMs=" << (successes_ == 0 ? 0.0 : totalUnlockMs_ / divisor) << " minCopyMs=" << minCopyMs_
        << " maxCopyMs=" << maxCopyMs_ << " lastChecksum=" << lastChecksum_;
    emit(out.str());
}

void RawCpuCopyProbe::emit(const std::string& line) const {
    if (diagnostic_) diagnostic_(line);
}

}  // namespace rawrcam::diagnostics
