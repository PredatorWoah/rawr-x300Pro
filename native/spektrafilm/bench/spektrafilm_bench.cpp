// Perf bench for the spektrafilm adapter: full image-to-image record()
// path (input image -> film chain -> output image) at quarter-res preview
// sizes. Reports wall-clock median/p90 per frame.
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <numeric>
#include <string>
#include <vector>

#include "../tools/vk_bench_common.hpp"
#include "spektrafilm/SpektraFilm.h"

namespace {

double nowMs() {
    return std::chrono::duration<double, std::milli>(
               std::chrono::steady_clock::now().time_since_epoch())
        .count();
}

}  // namespace

int main(int argc, char** argv) {
    const char* spirvDir = nullptr;
    const char* hanatosPath = nullptr;
    const char* gamutPath = nullptr;
    uint32_t width = 1008, height = 756;
    int iterations = 30;
    int method = 2;  // 0=Hanatos2025, 1=Mallett2019, 2=Hanatos2026
    bool negBleach = false;  // measure the off-fast-path cost
    bool grain = false;      // measure preview-grain cost
    bool dir = false;        // measure DIR-couplers cost (amount 0.5)
    bool halation = false;   // measure halation cost (scatter+bounce)
    bool diffusion = false;  // measure camera-diffusion cost (default family)
    bool printDiffusion = false;  // measure print-diffusion cost
    bool prodGrain = false;  // measure production-grain cost
    bool scanner = false;    // measure scanner-post cost (all paths)
    bool tiled = false;      // split full-res passes into tiles (activeRect)
    uint32_t tileW = 512, tileH = 256;
    bool tileMemory = false;  // tile-memory engine (working-size arenas)
    bool boost = false;       // halation boost 1EV (needs --tiled --tile-memory: two-phase)
    uint32_t chunks = 1u;     // split the tile grid into N submits (watchdog safety)
    uint32_t framesInFlight = 1u;  // still exporter uses a single frame slot
    bool conditional = false;  // conditional effect scratch (app stills config)
    bool autoExposure = false;  // meter the (mosaicked) input, set EV like app stills
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--spirv-dir") == 0 && i + 1 < argc) {
            spirvDir = argv[++i];
        } else if (std::strcmp(argv[i], "--hanatos") == 0 && i + 1 < argc) {
            hanatosPath = argv[++i];
        } else if (std::strcmp(argv[i], "--gamut") == 0 && i + 1 < argc) {
            gamutPath = argv[++i];
        } else if (std::strcmp(argv[i], "--width") == 0 && i + 1 < argc) {
            width = (uint32_t)std::atoi(argv[++i]);
        } else if (std::strcmp(argv[i], "--height") == 0 && i + 1 < argc) {
            height = (uint32_t)std::atoi(argv[++i]);
        } else if (std::strcmp(argv[i], "--iterations") == 0 && i + 1 < argc) {
            iterations = std::atoi(argv[++i]);
        } else if (std::strcmp(argv[i], "--method") == 0 && i + 1 < argc) {
            method = std::atoi(argv[++i]);
        } else if (std::strcmp(argv[i], "--neg-bleach") == 0) {
            negBleach = true;
        } else if (std::strcmp(argv[i], "--grain") == 0) {
            grain = true;
        } else if (std::strcmp(argv[i], "--dir") == 0) {
            dir = true;
        } else if (std::strcmp(argv[i], "--halation") == 0) {
            halation = true;
        } else if (std::strcmp(argv[i], "--diffusion") == 0) {
            diffusion = true;
        } else if (std::strcmp(argv[i], "--print-diffusion") == 0) {
            printDiffusion = true;
        } else if (std::strcmp(argv[i], "--prod-grain") == 0) {
            prodGrain = true;
        } else if (std::strcmp(argv[i], "--scanner") == 0) {
            scanner = true;
        } else if (std::strcmp(argv[i], "--tiled") == 0) {
            tiled = true;
        } else if (std::strcmp(argv[i], "--tile") == 0 && i + 1 < argc) {
            const char* spec = argv[++i];
            unsigned x = 0, y = 0;
            if (std::sscanf(spec, "%ux%u", &x, &y) != 2 || x < 64 || y < 64) {
                std::fprintf(stderr, "bench: --tile must be WxH (each >= 64)\n");
                return 2;
            }
            tileW = x;
            tileH = y;
        } else if (std::strcmp(argv[i], "--tile-memory") == 0) {
            tileMemory = true;
        } else if (std::strcmp(argv[i], "--boost") == 0) {
            halation = true;
            boost = true;
        } else if (std::strcmp(argv[i], "--chunks") == 0 && i + 1 < argc) {
            chunks = (uint32_t)std::max(1, std::atoi(argv[++i]));
        } else if (std::strcmp(argv[i], "--frames-in-flight") == 0 && i + 1 < argc) {
            framesInFlight = (uint32_t)std::atoi(argv[++i]);
        } else if (std::strcmp(argv[i], "--conditional") == 0) {
            conditional = true;
        } else if (std::strcmp(argv[i], "--auto-exposure") == 0) {
            autoExposure = true;
        }
    }
    if (!spirvDir || !hanatosPath || !gamutPath) {
        std::fprintf(stderr,
                     "usage: spektrafilm_bench --spirv-dir DIR --hanatos H.f32 "
                     "--gamut G.f32 [--width N --height N --iterations N "
                     "--method 0|1|2 --neg-bleach --grain --dir --halation --diffusion --print-diffusion "
                     "--prod-grain --scanner --tiled [--tile WxH] [--tile-memory] [--boost] [--chunks N]]\n");
        return 2;
    }
    if (method < 0 || method > 2) {
        std::fprintf(stderr, "spektrafilm_bench: method must be 0, 1, or 2\n");
        return 2;
    }
    if (framesInFlight < 1u || framesInFlight > 3u) {
        std::fprintf(stderr, "spektrafilm_bench: frames in flight must be 1..3\n");
        return 2;
    }
    const std::vector<uint8_t> inputSpv =
        spektra_test::loadFileBytes(std::string(spirvDir) + "/spektra_input.comp.spv");
    const std::vector<uint8_t> exposureSpv =
        spektra_test::loadFileBytes(std::string(spirvDir) + "/SpektraFilmExposure.comp.spv");
    const std::vector<uint8_t> developSpv =
        spektra_test::loadFileBytes(std::string(spirvDir) + "/SpektraCurveDevelop.comp.spv");
    const std::vector<uint8_t> printScanSpv =
        spektra_test::loadFileBytes(std::string(spirvDir) + "/SpektraPrintScan.comp.spv");
    const std::vector<uint8_t> outputSpv =
        spektra_test::loadFileBytes(std::string(spirvDir) + "/spektra_output.comp.spv");
    const std::vector<uint8_t> grainSpv =
        spektra_test::loadFileBytes(std::string(spirvDir) + "/SpektraGrain.comp.spv");
    const std::vector<uint8_t> dirSpv =
        spektra_test::loadFileBytes(std::string(spirvDir) + "/SpektraDir.comp.spv");
    const std::vector<uint8_t> halationSpv = spektra_test::loadFileBytes(
        std::string(spirvDir) + "/SpektraHalation.comp.spv");
    const std::vector<uint8_t> diffusionSpv = spektra_test::loadFileBytes(
        std::string(spirvDir) + "/SpektraDiffusion.comp.spv");
    const std::vector<uint8_t> scannerSpv = spektra_test::loadFileBytes(
        std::string(spirvDir) + "/SpektraScannerPost.comp.spv");
    const std::vector<uint8_t> boostMsSpv = spektra_test::loadFileBytes(
        std::string(spirvDir) + "/spektra_boost_milestone.comp.spv");
    const std::vector<uint8_t> hanatosRaw =
        spektra_test::loadFileBytes(hanatosPath);
    const std::vector<uint8_t> gamutRaw = spektra_test::loadFileBytes(gamutPath);

    spektra_test::Ctx ctx = spektra_test::makeCtx("spektrafilm_bench", false);
    VkCommandPoolCreateInfo pi{};
    pi.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    pi.queueFamilyIndex = ctx.queueFamily;
    pi.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    VkCommandPool pool = VK_NULL_HANDLE;
    spektra_test::check(vkCreateCommandPool(ctx.device, &pi, nullptr, &pool),
                        "pool");

    if (tileMemory && !tiled) {
        tiled = true;  // memory engines reject whole-frame records
    }
    auto engine = std::make_unique<spektrafilm_native::SpektraFilm>(
        [&] {
            spektrafilm_native::SpektraFilmCreateInfo ci{};
            ci.look.rgbToRawMethod = method;
            // Baked enables match the measured look (arena sizing).
            ci.look.grainEnabled = grain || prodGrain;
            ci.look.grainModel = prodGrain ? 1 : 0;
            ci.look.halationEnabled = halation;
            ci.look.cameraDiffusionEnabled = diffusion;
            ci.look.printDiffusionEnabled = printDiffusion;
            ci.look.scannerEnabled = scanner;
            ci.look.dirCouplersAmount = dir ? 0.5f : 0.0f;
            ci.tiledMemorySaving = tileMemory;
            ci.maxTileWidth = tileW;
            ci.maxTileHeight = tileH;
            ci.conditionalEffectScratch = conditional;
            ci.context.physicalDevice = ctx.physical;
            ci.context.device = ctx.device;
            ci.queue = ctx.queue;
            ci.queueFamilyIndex = ctx.queueFamily;
            ci.maxFramesInFlight = framesInFlight;
            ci.maxWidth = width;
            ci.maxHeight = height;
            ci.inputSpirv = reinterpret_cast<const uint32_t*>(inputSpv.data());
            ci.inputSpirvBytes = inputSpv.size();
            ci.exposureSpirv =
                reinterpret_cast<const uint32_t*>(exposureSpv.data());
            ci.exposureSpirvBytes = exposureSpv.size();
            ci.developSpirv =
                reinterpret_cast<const uint32_t*>(developSpv.data());
            ci.developSpirvBytes = developSpv.size();
            ci.printScanSpirv =
                reinterpret_cast<const uint32_t*>(printScanSpv.data());
            ci.printScanSpirvBytes = printScanSpv.size();
    ci.outputSpirv =
        reinterpret_cast<const uint32_t*>(outputSpv.data());
    ci.outputSpirvBytes = outputSpv.size();
    ci.boostMilestoneSpirv =
        reinterpret_cast<const uint32_t*>(boostMsSpv.data());
    ci.boostMilestoneSpirvBytes = boostMsSpv.size();
    ci.grainSpirv = reinterpret_cast<const uint32_t*>(grainSpv.data());
    ci.grainSpirvBytes = grainSpv.size();
    ci.dirSpirv = reinterpret_cast<const uint32_t*>(dirSpv.data());
    ci.dirSpirvBytes = dirSpv.size();
    ci.halationSpirv = reinterpret_cast<const uint32_t*>(halationSpv.data());
    ci.halationSpirvBytes = halationSpv.size();
    ci.diffusionSpirv = reinterpret_cast<const uint32_t*>(diffusionSpv.data());
    ci.diffusionSpirvBytes = diffusionSpv.size();
    ci.scannerSpirv = reinterpret_cast<const uint32_t*>(scannerSpv.data());
    ci.scannerSpirvBytes = scannerSpv.size();
            ci.hanatosSpectra =
                reinterpret_cast<const float*>(hanatosRaw.data());
            ci.hanatosSpectraFloats = hanatosRaw.size() / sizeof(float);
            ci.gamutCompression =
                reinterpret_cast<const float*>(gamutRaw.data());
            ci.gamutCompressionFloats = gamutRaw.size() / sizeof(float);
            return ci;
        }());

    std::vector<uint16_t> halves((size_t)width * height * 4);
    for (uint32_t y = 0; y < height; ++y) {
        for (uint32_t x = 0; x < width; ++x) {
            halves[((size_t)y * width + x) * 4 + 0] =
                spektra_test::floatToHalf(0.05f + 0.9f * (float)x / width);
            halves[((size_t)y * width + x) * 4 + 1] =
                spektra_test::floatToHalf(0.05f + 0.9f * (float)y / height);
            halves[((size_t)y * width + x) * 4 + 2] =
                spektra_test::floatToHalf(0.05f + 0.45f * ((float)x / width +
                                                           (float)y / height));
            halves[((size_t)y * width + x) * 4 + 3] =
                spektra_test::floatToHalf(1.0f);
        }
    }
    spektra_test::Image input = spektra_test::makeImage(
        ctx, width, height, VK_FORMAT_R16G16B16A16_SFLOAT);
    spektra_test::uploadRgba16f(ctx, pool, input, width, height, halves);
    spektra_test::Image output = spektra_test::makeImage(
        ctx, width, height, VK_FORMAT_R8G8B8A8_UNORM);
    spektra_test::transitionToGeneral(ctx, pool, output);

    VkCommandBufferAllocateInfo cai{};
    cai.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    cai.commandPool = pool;
    cai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    cai.commandBufferCount = 1;
    VkCommandBuffer cmd = VK_NULL_HANDLE;
    spektra_test::check(vkAllocateCommandBuffers(ctx.device, &cai, &cmd),
                        "alloc cmd");

    spektrafilm_native::SpektraFilmRecordInfo ri{};
    ri.commandBuffer = cmd;
    ri.look.rgbToRawMethod = method;
    if (negBleach) {
        ri.look.negativeBleachBypassAmount = 0.5f;
    }
    if (grain) {
        ri.look.grainEnabled = true;
    }
    if (dir) {
        ri.look.dirCouplersAmount = 0.5f;
    }
    if (halation) {
        ri.look.halationEnabled = true;
    }
    if (diffusion) {
        ri.look.cameraDiffusionEnabled = true;
    }
    if (printDiffusion) {
        ri.look.printDiffusionEnabled = true;
    }
    if (prodGrain) {
        ri.look.grainEnabled = true;
        ri.look.grainModel = 1;
    }
    if (scanner) {
        ri.look.scannerEnabled = true;
    }
    if (autoExposure) {
        // Mosaic the gradient input to RGGB Bayer DN and meter it (mirrors
        // the app stills flow: meter snapshot, set EV). Gradient halves are
        // 0..1 linear; DN rescales by black/white.
        const auto halfBitsToFloat = [](uint16_t h) {
            const uint32_t s = (uint32_t)(h & 0x8000u) << 16;
            const uint32_t e = (h >> 10) & 31u;
            const uint32_t m = h & 1023u;
            uint32_t f;
            if (e == 0) {
                f = (m == 0) ? s : s | ((uint32_t)(127 - 14) << 23);
            } else if (e == 31) {
                f = s | 0x7f800000u | (m << 13);
            } else {
                f = s | (((e - 15 + 127) << 23) | (m << 13));
            }
            float out = 0.0f;
            std::memcpy(&out, &f, 4);
            return out;
        };
        std::vector<uint16_t> bayer((size_t)width * height, 0);
        for (size_t px = 0; px < (size_t)width * height; ++px) {
            // Gradient input is smooth; every Bayer phase sees ~the pixel.
            const float v = halfBitsToFloat(halves[px * 4]);
            const float dn = 64.0f + v * (1023.0f - 64.0f);
            bayer[px] =
                (uint16_t)std::clamp((int)std::lround(dn), 0, 65535);
        }
        spektrafilm_native::tables::BayerMeterInput mi{};
        mi.pixels = bayer.data();
        mi.width = width;
        mi.height = height;
        mi.blackRggb[0] = mi.blackRggb[1] = mi.blackRggb[2] =
            mi.blackRggb[3] = 64.0f;
        mi.whiteLevel = 1023.0f;
        const float meterEv =
            spektrafilm_native::tables::autoExposureEvFromBayer(mi, 0);
        ri.look.filmExposureEv = std::clamp(meterEv, -10.0f, 10.0f);
        std::printf("spektrafilm_bench info: autoExposure meterEv=%.3f\n",
                    meterEv);
    }
    if (tiled) {
        ri.tilingMode = spektrafilm_native::GpuRenderTilingMode::Tiled;
        ri.tileWidth = tileW;
        ri.tileHeight = tileH;
    }
    if (boost) {
        ri.look.halationEnabled = true;
        ri.look.halationBoostEv = 1.0f;
    }
    ri.input.view = input.view;
    ri.input.format = VK_FORMAT_R16G16B16A16_SFLOAT;
    ri.input.layout = VK_IMAGE_LAYOUT_GENERAL;
    ri.input.width = width;
    ri.input.height = height;
    ri.output.view = output.view;
    ri.output.format = VK_FORMAT_R8G8B8A8_UNORM;
    ri.output.layout = VK_IMAGE_LAYOUT_GENERAL;
    ri.output.width = width;
    ri.output.height = height;
    ri.frameSlot = 0;

    const auto submitWait = [&](VkCommandBuffer c) {
        VkSubmitInfo submit{};
        submit.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        submit.commandBufferCount = 1;
        submit.pCommandBuffers = &c;
        const double t0 = nowMs();
        spektra_test::check(vkQueueSubmit(ctx.queue, 1, &submit, VK_NULL_HANDLE),
                            "submit");
        spektra_test::check(vkQueueWaitIdle(ctx.queue), "wait idle");
        return nowMs() - t0;
    };
    const auto runOnce = [&]() {
        const bool twoPhase = boost && tileMemory;
        double gpuMs = 0.0;        if (twoPhase) {
            VkCommandBufferBeginInfo begin{};
            begin.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
            begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
            spektra_test::check(vkBeginCommandBuffer(cmd, &begin), "begin");
            engine->recordBoostMilestone(ri);
            spektra_test::check(vkEndCommandBuffer(cmd), "end");
            gpuMs += submitWait(cmd);
            spektra_test::check(vkResetCommandBuffer(cmd, 0), "reset cmd");
            const auto ms = engine->readBoostMilestone(0);
            ri.hasBoostInfo = true;
            std::memcpy(ri.boostInfo, ms.values, sizeof(ri.boostInfo));
        }
        VkCommandBufferBeginInfo begin{};
        begin.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        uint32_t tilesTotal = 1u;
        uint32_t chunkCount = 1u;
        if (chunks > 1u && tiled) {
            tilesTotal = ((width + tileW - 1u) / tileW) *
                         ((height + tileH - 1u) / tileH);
            chunkCount = std::min(chunks, tilesTotal);
        }
        for (uint32_t k = 0; k < chunkCount; ++k) {
            if (chunkCount > 1u) {
                ri.tileFirst =
                    (uint32_t)((uint64_t)k * tilesTotal / chunkCount);
                const uint32_t end =
                    (uint32_t)((uint64_t)(k + 1u) * tilesTotal / chunkCount);
                ri.tileCount = end - ri.tileFirst;
                ri.tileFinalize = (k + 1u == chunkCount);
            }
        spektra_test::check(vkBeginCommandBuffer(cmd, &begin), "begin");
        engine->record(ri);
        spektra_test::check(vkEndCommandBuffer(cmd), "end");
        const double chunkMs = submitWait(cmd);
        gpuMs += chunkMs;
        if (chunkCount > 1u) {
            std::printf("spektrafilm_bench chunk k=%u/%u tiles=[%u,%u) fin=%d ms=%.1f\n",
                        k, chunkCount, ri.tileFirst, ri.tileFirst + ri.tileCount,
                        (int)ri.tileFinalize, chunkMs);
        } else {
            std::printf("spektrafilm_bench single ms=%.1f\n", chunkMs);
        }
        std::fflush(stdout);
        spektra_test::check(vkResetCommandBuffer(cmd, 0), "reset cmd");
        }
        return gpuMs;
    };
    runOnce();  // warmup (descriptor bind, shader compile on some drivers)
    std::vector<double> times;
    times.reserve((size_t)iterations);
    for (int i = 0; i < iterations; ++i) {
        ri.frameSlot = (uint32_t)(i % framesInFlight);
        times.push_back(runOnce());
    }
    std::sort(times.begin(), times.end());
    const double median = times[times.size() / 2];
    const double p90 =
        times[std::min(times.size() - 1, times.size() * 9 / 10)];
    std::printf("spektrafilm_bench method=%d %ux%u iters=%d median_ms=%.2f p90_ms=%.2f%s%s%s%s%s%s%s%s%s%s%s%s%s\n",
                method, width, height, iterations, median, p90,
                negBleach ? " negbleach" : "", grain ? " grain" : "",
                dir ? " dir" : "", halation ? " halation" : "",
                diffusion ? " diffusion" : "",
                printDiffusion ? " print-diffusion" : "",
                prodGrain ? " prod-grain" : "",
                scanner ? " scanner" : "", tiled ? " tiled" : "",
                tileMemory ? " tile-memory" : "", boost ? " boost" : "",
                chunks > 1u ? " chunks" : "",
                autoExposure ? " auto-exposure" : "");
    engine.reset();
    spektra_test::destroyImage(ctx, input);
    spektra_test::destroyImage(ctx, output);
    spektra_test::destroyCtx(ctx);
    return 0;
}
