// uhdr_mux_cli: assemble an UltraHDR (JPEG_R) file from two complete JPEGs.
// Pure byte mux on top of gainmap::UltrahdrMux (no re-encode): base JPEG
// (SOI..EOI) + gain map JPEG (SOI..EOI, gray for single-channel, RGB for
// multi) + optional raw EXIF APP1 payload -> full file on stdout or --out.
//
// Build (no CMake needed; UltrahdrMux is dependency-free):
//   g++ -std=c++17 -O2 -o .cache/uhdr_mux_cli tools/uhdr_mux_cli/main.cpp \
//       native/gainmap/src/UltrahdrMux.cpp -Inative/gainmap/include
//
// Usage:
//   uhdr_mux_cli --base base.jpg --map map.jpg --out uhdr.jpg [--multi]
//                [--exif exif.bin] [--min-log2 0 --max-log2 4.7090998
//                --gamma 1 --off-sdr 0.015625 --off-hdr 0.015625
//                --cap-min 0 --cap-max 4.7090998]
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

#include "gainmap/UltrahdrMux.h"

namespace {

std::vector<uint8_t> readFile(const std::string& path, bool& ok) {
    std::ifstream f(path, std::ios::binary | std::ios::ate);
    if (!f) {
        ok = false;
        return {};
    }
    const std::streamoff n = f.tellg();
    if (n <= 0) {
        ok = false;
        return {};
    }
    std::vector<uint8_t> b(static_cast<size_t>(n));
    f.seekg(0);
    f.read(reinterpret_cast<char*>(b.data()), n);
    ok = !!f;
    return b;
}

}  // namespace

int main(int argc, char** argv) {
    std::string basePath, mapPath, exifPath, outPath;
    gainmap::UltrahdrMuxParams params;
    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        auto need = [&](std::string& dst) {
            if (i + 1 >= argc) throw std::runtime_error(std::string("missing value for ") + a);
            dst = argv[++i];
        };
        auto needF = [&](float& dst) {
            std::string s;
            need(s);
            dst = std::stof(s);
        };
        if (a == "--base") need(basePath);
        else if (a == "--map") need(mapPath);
        else if (a == "--exif") need(exifPath);
        else if (a == "--out") need(outPath);
        else if (a == "--multi") params.multiChannel = true;
        else if (a == "--min-log2") needF(params.gainMapMinLog2);
        else if (a == "--max-log2") needF(params.gainMapMaxLog2);
        else if (a == "--gamma") needF(params.gamma);
        else if (a == "--off-sdr") needF(params.offsetSdr);
        else if (a == "--off-hdr") needF(params.offsetHdr);
        else if (a == "--cap-min") needF(params.hdrCapacityMinLog2);
        else if (a == "--cap-max") needF(params.hdrCapacityMaxLog2);
        else throw std::runtime_error(std::string("unknown arg ") + a);
    }
    if (basePath.empty() || mapPath.empty() || outPath.empty()) {
        std::cerr << "usage: uhdr_mux_cli --base B --map M --out O [--multi] ...\n";
        return 2;
    }
    try {
        bool ok = false;
        const auto base = readFile(basePath, ok);
        if (!ok) throw std::runtime_error("cannot read base");
        const auto map = readFile(mapPath, ok);
        if (!ok) throw std::runtime_error("cannot read map");
        std::vector<uint8_t> exif;
        if (!exifPath.empty()) {
            exif = readFile(exifPath, ok);
            if (!ok) throw std::runtime_error("cannot read exif");
        }
        std::vector<uint8_t> out;
        const gainmap::UltrahdrMuxResult r = gainmap::UltrahdrMux::assemble(
            base.data(), base.size(), map.data(), map.size(), exif.empty() ? nullptr : exif.data(), exif.size(),
            params, out);
        if (!r.ok) throw std::runtime_error(r.error);
        std::ofstream f(outPath, std::ios::binary | std::ios::trunc);
        f.write(reinterpret_cast<const char*>(out.data()), static_cast<std::streamsize>(out.size()));
        f.close();
        if (!f) throw std::runtime_error("cannot write out");
        std::cout << "UHDR_MUX_OK bytes=" << out.size() << " primary=" << r.primarySize
                  << " secondary=" << r.secondarySize << " multi=" << (params.multiChannel ? 1 : 0) << "\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "UHDR_MUX_FAIL " << e.what() << "\n";
        return 1;
    }
}
