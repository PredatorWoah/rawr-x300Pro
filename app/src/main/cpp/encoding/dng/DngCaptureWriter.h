#pragma once

#include <cstdint>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>

#include "encoding/dng/DngCaptureContext.h"
#include "imaging/RawSnapshot.h"

namespace rawrcam::encoding::dng {

struct DngWriteCompletion {
    uint64_t requestId = 0;
    bool success = false;
    std::string displayName;
    std::string error;
    double writeMs = 0.0;
    double fsyncMs = 0.0;
    uint64_t fileBytes = 0;
};

// Owns asynchronous DNG serialization to a caller-provided file descriptor only.
// Android storage/MediaStore lifecycle remains Kotlin-owned. The worker retains
// immutable shared ownership of rawrcam::imaging::RawSnapshot; Camera2/AImage/Vulkan never enter here.
class DngCaptureWriter final {
   public:
    DngCaptureWriter() = default;
    ~DngCaptureWriter();

    DngCaptureWriter(const DngCaptureWriter&) = delete;
    DngCaptureWriter& operator=(const DngCaptureWriter&) = delete;

    // Consumes outputFd on every outcome, including start rejection.
    bool start(std::shared_ptr<const rawrcam::imaging::RawSnapshot> frame, DngCaptureContext context);
    std::optional<DngWriteCompletion> pollCompletion();
    bool busy() const noexcept;

   private:
    void joinWorker();
    mutable std::mutex mutex_;
    std::thread worker_;
    bool busy_ = false;
    std::optional<DngWriteCompletion> completion_;
};

}  // namespace rawrcam::encoding::dng
