#include "Raw16SourceParityProbe.h"

#ifndef NDEBUG

#include <android/log.h>
#include <sys/system_properties.h>

#include <algorithm>
#include <array>
#include <cstring>
#include <sstream>

namespace rawrcam::diagnostics {
namespace {

constexpr size_t kMaxMismatchLines = 32;

void emit(const std::function<void(const std::string&)>& diagnostic, const std::string& line) {
    __android_log_print(ANDROID_LOG_INFO, "RawrCamNative", "%s", line.c_str());
    if (diagnostic) diagnostic(line);
}

uint16_t loadU16(const uint8_t* p) noexcept {
    uint16_t v = 0;
    std::memcpy(&v, p, sizeof(v));
    return v;
}

const char* cfaClass(uint32_t x, uint32_t y) noexcept {
    static constexpr const char* kNames[4] = {"p00", "p10", "p01", "p11"};
    return kNames[((y & 1u) << 1u) | (x & 1u)];
}

}  // namespace

bool raw16SourceParityProbeEnabled() noexcept {
    char value[PROP_VALUE_MAX]{};
    const int n = __system_property_get("debug.rawr.raw16_source_parity", value);
    if (n <= 0) return false;
    return value[0] == '1' || value[0] == 'y' || value[0] == 'Y' || value[0] == 't' || value[0] == 'T';
}

int runRaw16SourceParityProbe(AImage* image, AHardwareBuffer* ahb, int acquireFenceFd, uint32_t width, uint32_t height,
                              const std::vector<uint8_t>& packed,
                              const std::function<void(const std::string&)>& diagnostic) {
    if (!raw16SourceParityProbeEnabled()) return acquireFenceFd;
    if (!image || !ahb || width == 0 || height == 0) {
        emit(diagnostic, "RAW16_SOURCE_PARITY FAIL reason=invalid_input");
        return acquireFenceFd;
    }

    uint8_t* imageData = nullptr;
    int imageDataLength = 0;
    int32_t imageRowStride = 0;
    int32_t imagePixelStride = 0;
    const bool imageOk = AImage_getPlaneData(image, 0, &imageData, &imageDataLength) == AMEDIA_OK &&
                         AImage_getPlaneRowStride(image, 0, &imageRowStride) == AMEDIA_OK &&
                         AImage_getPlanePixelStride(image, 0, &imagePixelStride) == AMEDIA_OK && imageData != nullptr &&
                         imageRowStride > 0 && imagePixelStride == 2;
    if (!imageOk) {
        emit(diagnostic, "RAW16_SOURCE_PARITY FAIL reason=aimage_plane_unavailable");
        return acquireFenceFd;
    }

    AHardwareBuffer_Desc desc{};
    AHardwareBuffer_describe(ahb, &desc);
    const uint64_t ahbRowBytes64 = static_cast<uint64_t>(desc.stride) * 2u;
    const uint64_t packedRowBytes64 = static_cast<uint64_t>(width) * 2u;
    const uint64_t packedBytes64 = packedRowBytes64 * static_cast<uint64_t>(height);
    if (desc.width != width || desc.height != height || ahbRowBytes64 < packedRowBytes64 ||
        packedBytes64 != packed.size()) {
        emit(diagnostic, "RAW16_SOURCE_PARITY FAIL reason=layout_mismatch");
        return acquireFenceFd;
    }

    void* ahbAddress = nullptr;
    const int lockResult =
        AHardwareBuffer_lock(ahb, AHARDWAREBUFFER_USAGE_CPU_READ_OFTEN, acquireFenceFd, nullptr, &ahbAddress);
    if (lockResult != 0 || ahbAddress == nullptr) {
        emit(diagnostic, "RAW16_SOURCE_PARITY FAIL reason=ahb_lock_failed code=" + std::to_string(lockResult));
        return acquireFenceFd;
    }

    const auto* ahbBase = static_cast<const uint8_t*>(ahbAddress);
    const size_t ahbRowBytes = static_cast<size_t>(ahbRowBytes64);
    const size_t packedRowBytes = static_cast<size_t>(packedRowBytes64);

    uint64_t aimageVsAhb = 0;
    uint64_t aimageVsPacked = 0;
    uint64_t ahbVsPacked = 0;
    uint32_t maxAbsAimageAhb = 0;
    uint32_t maxAbsAimagePacked = 0;
    uint32_t maxAbsAhbPacked = 0;
    std::array<uint64_t, 4> aimageAhbByParity{};
    std::array<uint64_t, 4> aimagePackedByParity{};
    std::array<uint64_t, 4> ahbPackedByParity{};
    size_t mismatchLines = 0;

    for (uint32_t y = 0; y < height; ++y) {
        const uint8_t* imageRow = imageData + static_cast<size_t>(y) * static_cast<size_t>(imageRowStride);
        const uint8_t* ahbRow = ahbBase + static_cast<size_t>(y) * ahbRowBytes;
        const uint8_t* packedRow = packed.data() + static_cast<size_t>(y) * packedRowBytes;
        for (uint32_t x = 0; x < width; ++x) {
            const uint16_t a = loadU16(imageRow + static_cast<size_t>(x) * 2u);
            const uint16_t h = loadU16(ahbRow + static_cast<size_t>(x) * 2u);
            const uint16_t p = loadU16(packedRow + static_cast<size_t>(x) * 2u);
            const size_t parity = static_cast<size_t>(((y & 1u) << 1u) | (x & 1u));
            if (a != h) {
                ++aimageVsAhb;
                ++aimageAhbByParity[parity];
                maxAbsAimageAhb = std::max(maxAbsAimageAhb, static_cast<uint32_t>(a > h ? a - h : h - a));
            }
            if (a != p) {
                ++aimageVsPacked;
                ++aimagePackedByParity[parity];
                maxAbsAimagePacked = std::max(maxAbsAimagePacked, static_cast<uint32_t>(a > p ? a - p : p - a));
            }
            if (h != p) {
                ++ahbVsPacked;
                ++ahbPackedByParity[parity];
                maxAbsAhbPacked = std::max(maxAbsAhbPacked, static_cast<uint32_t>(h > p ? h - p : p - h));
            }
            if ((a != h || a != p || h != p) && mismatchLines < kMaxMismatchLines) {
                std::ostringstream m;
                m << "RAW16_SOURCE_PARITY_MISMATCH index=" << mismatchLines << " x=" << x << " y=" << y
                  << " parity=" << cfaClass(x, y) << " aimage=" << a << " ahb=" << h << " packed=" << p;
                emit(diagnostic, m.str());
                ++mismatchLines;
            }
        }
    }

    int releaseFenceFd = -1;
    const int unlockResult = AHardwareBuffer_unlock(ahb, &releaseFenceFd);
    if (unlockResult != 0) {
        emit(diagnostic, "RAW16_SOURCE_PARITY FAIL reason=ahb_unlock_failed code=" + std::to_string(unlockResult));
        return -2;
    }

    const uint64_t samples = static_cast<uint64_t>(width) * static_cast<uint64_t>(height);
    std::ostringstream s;
    s << "RAW16_SOURCE_PARITY " << ((aimageVsAhb == 0 && aimageVsPacked == 0 && ahbVsPacked == 0) ? "PASS" : "FAIL")
      << " size=" << width << 'x' << height << " samples=" << samples << " aimageRowStride=" << imageRowStride
      << " aimagePixelStride=" << imagePixelStride << " aimageDataLength=" << imageDataLength
      << " ahbStridePixels=" << desc.stride << " packedRowStride=" << packedRowBytes
      << " aimage_vs_ahb_diff=" << aimageVsAhb << " aimage_vs_packed_diff=" << aimageVsPacked
      << " ahb_vs_packed_diff=" << ahbVsPacked << " maxAbs_aimage_ahb=" << maxAbsAimageAhb
      << " maxAbs_aimage_packed=" << maxAbsAimagePacked << " maxAbs_ahb_packed=" << maxAbsAhbPacked
      << " parityDiff_aimage_ahb=" << aimageAhbByParity[0] << ',' << aimageAhbByParity[1] << ',' << aimageAhbByParity[2]
      << ',' << aimageAhbByParity[3] << " parityDiff_aimage_packed=" << aimagePackedByParity[0] << ','
      << aimagePackedByParity[1] << ',' << aimagePackedByParity[2] << ',' << aimagePackedByParity[3]
      << " parityDiff_ahb_packed=" << ahbPackedByParity[0] << ',' << ahbPackedByParity[1] << ',' << ahbPackedByParity[2]
      << ',' << ahbPackedByParity[3] << " gpuAcquireFence=" << releaseFenceFd;
    emit(diagnostic, s.str());
    return releaseFenceFd;
}

}  // namespace rawrcam::diagnostics

#endif  // !NDEBUG
