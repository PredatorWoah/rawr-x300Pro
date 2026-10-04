#include <fstream>
#include <iostream>
#include <iterator>
#include <random>
#include <stdexcept>

#include "video/HevcLogMetadata.h"

int main(int argc, char** argv) {
    if (argc != 3) return 2;
    try {
        std::ifstream in(argv[1], std::ios::binary);
        std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(in)), {});
        const auto patched = rawrcam::video::logHevcAccessUnit(bytes.data(), bytes.size(), true);
        if (rawrcam::video::logHevcAccessUnit(patched.data(), patched.size(), true) != patched)
            throw std::runtime_error("not idempotent");
        // Truncated SPS/CSD must fail, and never mutate the caller's bytes.
        for (size_t size = 0; size < 4; ++size) {
            bool failed = false;
            try {
                (void)rawrcam::video::logHevcSps(bytes.data(), size);
            } catch (const std::exception&) {
                failed = true;
            }
            if (!failed) throw std::runtime_error("accepted truncated SPS");
        }
        // Each packet may arrive as Annex B or with a four-byte NAL length.
        // Exercise both, plus packets without SPSs and malformed lengths.
        auto prefix = [&](size_t i) -> size_t {
            if (i + 4 <= bytes.size() && bytes[i] == 0 && bytes[i + 1] == 0 && bytes[i + 2] == 0 && bytes[i + 3] == 1)
                return 4;
            if (i + 3 <= bytes.size() && bytes[i] == 0 && bytes[i + 1] == 0 && bytes[i + 2] == 1) return 3;
            return 0;
        };
        for (size_t i = 0; i < bytes.size();) {
            const size_t start = i + prefix(i);
            if (start == i) throw std::runtime_error("fixture is not Annex B");
            size_t end = start;
            while (end < bytes.size() && !prefix(end)) ++end;
            std::vector<uint8_t> packet(bytes.begin() + i, bytes.begin() + end);
            const auto annex = rawrcam::video::logHevcAccessUnit(packet.data(), packet.size());
            std::vector<uint8_t> lengthPrefixed;
            for (int shift = 24; shift >= 0; shift -= 8) lengthPrefixed.push_back(uint8_t((end - start) >> shift));
            lengthPrefixed.insert(lengthPrefixed.end(), bytes.begin() + start, bytes.begin() + end);
            const auto lengthPatched = rawrcam::video::logHevcAccessUnit(lengthPrefixed.data(), lengthPrefixed.size());
            if (std::vector<uint8_t>(annex.begin() + prefix(i), annex.end()) !=
                std::vector<uint8_t>(lengthPatched.begin() + 4, lengthPatched.end()))
                throw std::runtime_error("NAL framing changes result");
            if (((bytes[start] >> 1) & 63) != 33 && packet != annex)
                throw std::runtime_error("changed non-SPS payload");
            lengthPrefixed[0] = 0x7f;
            bool rejected = false;
            try {
                (void)rawrcam::video::logHevcAccessUnit(lengthPrefixed.data(), lengthPrefixed.size());
            } catch (const std::exception&) {
                rejected = true;
            }
            if (!rejected) throw std::runtime_error("accepted out-of-bounds NAL length");
            i = end;
        }
        // Bounded malformed SPS fuzz under ASan/UBSan.
        std::mt19937 random(42);
        for (unsigned trial = 0; trial < 2000; ++trial) {
            std::vector<uint8_t> noise(3 + random() % 256);
            for (auto& byte : noise) byte = uint8_t(random());
            noise[0] = 66;
            noise[1] = 1;
            try {
                (void)rawrcam::video::logHevcSps(noise.data(), noise.size());
            } catch (const std::exception&) {
            }
        }
        std::ofstream out(argv[2], std::ios::binary);
        out.write(reinterpret_cast<const char*>(patched.data()), patched.size());
        if (!out) throw std::runtime_error("output failed");
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
