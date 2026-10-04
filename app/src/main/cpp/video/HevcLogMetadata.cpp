#include "video/HevcLogMetadata.h"

#include <algorithm>
#include <stdexcept>

namespace rawrcam::video {
namespace {
using Bytes = std::vector<uint8_t>;
void require(bool ok) {
    if (!ok) throw std::runtime_error("invalid or unsupported HEVC LOG metadata");
}
// Bounded SPS reader. Syntax: ITU-T H.265 7.3.2.2 and Annex E.
struct Bits {
    Bytes bits;
    size_t pos = 0;
    uint32_t read(unsigned count) {
        require(count <= 32 && count <= bits.size() - pos);
        uint32_t value = 0;
        while (count--) value = (value << 1) | bits[pos++];
        return value;
    }
    void skip(size_t count) {
        require(count <= bits.size() - pos);
        pos += count;
    }
    uint32_t ue(uint32_t max = UINT32_MAX - 1) {
        unsigned zeros = 0;
        while (!read(1)) require(++zeros < 32);
        const uint32_t value = ((uint32_t(1) << zeros) - 1) + read(zeros);
        require(value <= max);
        return value;
    }
};
void appendBits(Bytes& out, uint32_t value, unsigned count) {
    while (count--) out.push_back((value >> count) & 1);
}
void skipToVui(Bits& b) {
    b.skip(4);
    const unsigned layers = b.read(3);
    require(layers <= 6);
    b.skip(1 + 96);  // nesting and general profile_tier_level
    unsigned profile[7]{}, level[7]{};
    for (unsigned i = 0; i < layers; ++i) {
        profile[i] = b.read(1);
        level[i] = b.read(1);
    }
    if (layers) b.skip((8 - layers) * 2);
    for (unsigned i = 0; i < layers; ++i) b.skip(profile[i] * 88 + level[i] * 8);
    b.ue(15);  // SPS id
    if (b.ue(3) == 3) b.skip(1);
    b.ue(65535);
    b.ue(65535);  // width/height
    if (b.read(1))
        for (int i = 0; i < 4; ++i) b.ue();
    b.ue(8);
    b.ue(8);  // bit depth
    const unsigned pocBits = b.ue(12) + 4;
    const unsigned firstLayer = b.read(1) ? 0 : layers;
    for (unsigned i = firstLayer; i <= layers; ++i) {
        b.ue(15);
        b.ue(15);
        b.ue();
    }
    for (int i = 0; i < 6; ++i) b.ue();  // coding/transform block sizes and hierarchy
    if (b.read(1) && b.read(1)) {
        for (unsigned size = 0; size < 4; ++size) {
            for (unsigned matrix = 0; matrix < 6; matrix += size == 3 ? 3 : 1) {
                if (!b.read(1))
                    b.ue(matrix);
                else {
                    if (size > 1) b.ue();  // signed Exp-Golomb has identical bit length
                    for (unsigned i = 0; i < std::min(64u, 1u << (4 + 2 * size)); ++i) b.ue();
                }
            }
        }
    }
    b.skip(2);  // AMP, SAO
    if (b.read(1)) {
        b.skip(8);
        b.ue();
        b.ue();
        b.skip(1);
    }  // PCM
    const unsigned sets = b.ue(64);
    unsigned previousDeltas = 0;
    for (unsigned i = 0; i < sets; ++i) {
        unsigned deltas = 0;
        if (i && b.read(1)) {
            b.skip(1);
            b.ue();
            for (unsigned j = 0; j <= previousDeltas; ++j) {
                const bool used = b.read(1);
                if (used || b.read(1)) ++deltas;
            }
        } else {
            deltas = b.ue(15);
            deltas += b.ue(15);
            require(deltas <= 15);
            for (unsigned j = 0; j < deltas; ++j) {
                b.ue();
                b.skip(1);
            }
        }
        require(deltas <= 15);
        previousDeltas = deltas;
    }
    if (b.read(1)) {
        const unsigned count = b.ue(32);
        b.skip(count * (pocBits + 1));
    }
    b.skip(2);  // temporal MVP, strong intra smoothing
}
size_t startCode(const uint8_t* p, size_t remaining) {
    if (remaining >= 4 && p[0] == 0 && p[1] == 0 && p[2] == 0 && p[3] == 1) return 4;
    if (remaining >= 3 && p[0] == 0 && p[1] == 0 && p[2] == 1) return 3;
    return 0;
}
}  // namespace

std::vector<uint8_t> logHevcSps(const uint8_t* nal, size_t size) {
    require(nal && size >= 3 && size <= 65535);
    require((nal[0] >> 1) == 33 && (nal[0] & 1) == 0 && (nal[1] >> 3) == 0 && (nal[1] & 7) != 0);
    Bits b;
    unsigned zeros = 0;
    for (size_t i = 2; i < size; ++i) {
        const uint8_t v = nal[i];
        if (zeros >= 2 && v == 3) {
            require(i + 1 < size && nal[i + 1] <= 3);
            zeros = 0;
            continue;
        }
        appendBits(b.bits, v, 8);
        zeros = v == 0 ? zeros + 1 : 0;
    }
    // Retain rbsp_stop_one_bit, removing alignment/trailing zero bytes so
    // inserting an absent VUI can restore byte alignment correctly.
    while (!b.bits.empty() && b.bits.back() == 0) b.bits.pop_back();
    require(!b.bits.empty());
    skipToVui(b);
    size_t begin = b.pos;
    Bytes replacement;
    const bool hasVui = b.read(1);
    if (hasVui) {
        if (b.read(1) && b.read(8) == 255) b.skip(32);
        if (b.read(1)) b.skip(1);
        begin = b.pos;  // replace only video_signal_type syntax
        if (b.read(1)) {
            b.skip(3);
            require(b.read(1) == 0);  // never relabel full-range samples as limited
            if (b.read(1)) {
                b.skip(16);
                require(b.read(8) == 1);  // encoder must really use the BT.709 matrix
            }
        }
    } else {
        appendBits(replacement, 1, 1);  // vui_parameters_present_flag
        appendBits(replacement, 0, 2);  // aspect ratio, overscan absent
    }
    appendBits(replacement, 1, 1);               // video_signal_type_present_flag
    appendBits(replacement, 5, 3);               // video_format unspecified
    appendBits(replacement, 0, 1);               // limited range
    appendBits(replacement, 1, 1);               // colour_description_present_flag
    appendBits(replacement, 2, 8);               // primaries unspecified
    appendBits(replacement, 2, 8);               // transfer unspecified (profile is in mdta)
    appendBits(replacement, 1, 8);               // BT.709 YCbCr matrix
    if (!hasVui) appendBits(replacement, 0, 7);  // remaining optional VUI flags
    require(b.pos < b.bits.size());              // SPS extensions and trailing bits must remain
    b.bits.erase(b.bits.begin() + begin, b.bits.begin() + b.pos);
    b.bits.insert(b.bits.begin() + begin, replacement.begin(), replacement.end());
    while (b.bits.size() % 8) b.bits.push_back(0);
    Bytes out{nal[0], nal[1]};
    zeros = 0;
    for (size_t i = 0; i < b.bits.size(); i += 8) {
        uint8_t value = 0;
        for (size_t j = 0; j < 8; ++j) value = uint8_t((value << 1) | b.bits[i + j]);
        if (zeros >= 2 && value <= 3) {
            out.push_back(3);
            zeros = 0;
        }
        out.push_back(value);
        zeros = value == 0 ? zeros + 1 : 0;
    }
    return out;
}

std::vector<uint8_t> logHevcAccessUnit(const uint8_t* data, size_t size, bool requireSps) {
    require(data && size >= 5);
    Bytes out;
    out.reserve(size);
    const bool annexB = startCode(data, size) != 0;
    size_t pos = 0;
    unsigned spsCount = 0;
    while (pos < size) {
        const size_t prefix = annexB ? startCode(data + pos, size - pos) : 4;
        require(prefix && size - pos >= prefix + 2);
        const size_t begin = pos + prefix;
        size_t end = begin;
        if (annexB) {
            while (end < size && !startCode(data + end, size - end)) ++end;
        } else {
            const uint32_t n = (uint32_t(data[pos]) << 24) | (uint32_t(data[pos + 1]) << 16) |
                               (uint32_t(data[pos + 2]) << 8) | data[pos + 3];
            require(n >= 2 && n <= size - begin);
            end += n;
        }
        require(end - begin >= 2);
        Bytes patched;
        if (((data[begin] >> 1) & 63) == 33) {
            patched = logHevcSps(data + begin, end - begin);
            ++spsCount;
        }
        const size_t length = patched.empty() ? end - begin : patched.size();
        if (annexB)
            out.insert(out.end(), data + pos, data + begin);
        else
            for (int shift = 24; shift >= 0; shift -= 8) out.push_back(uint8_t(length >> shift));
        if (patched.empty())
            out.insert(out.end(), data + begin, data + end);
        else
            out.insert(out.end(), patched.begin(), patched.end());
        pos = end;
    }
    require(!requireSps || spsCount > 0);
    return out;
}
}  // namespace rawrcam::video
