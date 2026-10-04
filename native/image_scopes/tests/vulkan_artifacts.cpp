#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

#include "image_scopes/image_scopes.h"
#include "vulkan_test_context.hpp"
using namespace image_scopes;
namespace t = image_scopes::tests;
namespace {
using Pixels = std::vector<std::uint8_t>;
Pixels rgba(std::uint32_t w, std::uint32_t h) { return Pixels(static_cast<std::size_t>(w) * h * 4u, 255u); }
Pixels solid(std::uint32_t w, std::uint32_t h, std::uint8_t v) {
    auto a = rgba(w, h);
    for (std::size_t i = 0; i < static_cast<std::size_t>(w) * h; ++i) a[i * 4] = a[i * 4 + 1] = a[i * 4 + 2] = v;
    return a;
}
Pixels xRamp(std::uint32_t w, std::uint32_t h) {
    auto a = rgba(w, h);
    for (std::uint32_t y = 0; y < h; ++y)
        for (std::uint32_t x = 0; x < w; ++x) {
            auto v = static_cast<std::uint8_t>((std::uint64_t(x) * 255u) / (w - 1u));
            auto* p = &a[(static_cast<std::size_t>(y) * w + x) * 4u];
            p[0] = p[1] = p[2] = v;
        }
    return a;
}
Pixels colorTarget(std::uint32_t w, std::uint32_t h) {
    auto a = rgba(w, h);
    const std::uint8_t c[6][3] = {{191, 0, 0}, {0, 191, 0}, {0, 0, 191}, {0, 191, 191}, {191, 0, 191}, {191, 191, 0}};
    const std::uint8_t n[8] = {0, 32, 64, 96, 128, 160, 208, 255};
    for (std::uint32_t y = 0; y < h; ++y)
        for (std::uint32_t x = 0; x < w; ++x) {
            auto* p = &a[(static_cast<std::size_t>(y) * w + x) * 4u];
            if (y < h * 2u / 3u) {
                auto k = std::min(5u, x * 6u / w);
                p[0] = c[k][0];
                p[1] = c[k][1];
                p[2] = c[k][2];
            } else {
                auto k = std::min(7u, x * 8u / w);
                p[0] = p[1] = p[2] = n[k];
            }
        }
    return a;
}
Pixels photo(std::uint32_t w, std::uint32_t h) {
    auto a = rgba(w, h);
    const float sx = .77f * w, sy = .18f * h, sr = .065f * h;
    auto u8 = [](float v) { return static_cast<std::uint8_t>(std::clamp(v, 0.f, 255.f)); };
    for (std::uint32_t y = 0; y < h; ++y)
        for (std::uint32_t x = 0; x < w; ++x) {
            float fx = float(x) / float(w - 1), fy = float(y) / float(h - 1);
            float r, g, b;
            if (fy < .53f) {
                float q = fy / .53f;
                r = 45 + 105 * q;
                g = 115 + 95 * q;
                b = 205 + 30 * q;
                float warm = std::max(0.f, 1.f - std::abs(fy - .53f) / .12f);
                r += 55 * warm;
                g += 20 * warm;
                b -= 35 * warm;
            } else {
                float q = (fy - .53f) / .47f;
                r = 40 - 22 * q;
                g = 82 - 42 * q;
                b = 38 - 25 * q;
            }
            float ridge1 = .43f + .08f * std::sin(fx * 9.f) + .025f * std::sin(fx * 31.f),
                  ridge2 = .50f + .045f * std::sin(fx * 13.f + 1.2f);
            if (fy > ridge1) {
                r = 34;
                g = 43;
                b = 50;
            }
            if (fy > ridge2) {
                r = 22 + 10 * (1 - fy);
                g = 48 + 26 * (1 - fy);
                b = 28;
            }
            float dx = float(x) - sx, dy = float(y) - sy, dist = std::sqrt(dx * dx + dy * dy);
            if (dist < sr * 2.4f) {
                float k = std::max(0.f, 1 - dist / (sr * 2.4f));
                r += 145 * k;
                g += 120 * k;
                b += 70 * k;
            }
            if (dist < sr) {
                r = 255;
                g = 245;
                b = 205;
            }
            if (fx > .13f && fx < .25f && fy > .67f && fy < .88f) {
                r = 238;
                g = 70;
                b = 30;
            }
            if (fx > .66f && fx < .76f && fy > .65f && fy < .84f) {
                r = 25;
                g = 150;
                b = 220;
            }
            float ex = (fx - .48f) / .065f, ey = (fy - .70f) / .12f;
            if (ex * ex + ey * ey < 1.f) {
                float sh = std::max(.55f, 1 - .28f * (ex + .4f * ey));
                r = 205 * sh;
                g = 142 * sh;
                b = 105 * sh;
            }
            if (fx > .52f && fx < .535f && fy > .645f && fy < .665f) {
                r = 255;
                g = 255;
                b = 248;
            }
            auto* p = &a[(static_cast<std::size_t>(y) * w + x) * 4u];
            p[0] = u8(r);
            p[1] = u8(g);
            p[2] = u8(b);
        }
    return a;
}

Pixels warmPortrait(std::uint32_t w, std::uint32_t h) {
    auto a = rgba(w, h);
    auto u8 = [](float v) { return static_cast<std::uint8_t>(std::clamp(v, 0.f, 255.f)); };
    for (std::uint32_t y = 0; y < h; ++y) {
        for (std::uint32_t x = 0; x < w; ++x) {
            float fx = float(x) / float(w - 1), fy = float(y) / float(h - 1);
            float lum = .18f + .57f * (1.f - fy);
            float r = 255.f * lum * .98f, g = 255.f * lum * .82f, b = 255.f * lum * .69f;
            float face = 1.f - std::min(1.f, std::pow((fx - .50f) / .17f, 2.f) + std::pow((fy - .48f) / .27f, 2.f));
            face = face * face;
            r = r * (1 - .78f * face) + (198.f + 28.f * (1 - fy)) * .78f * face;
            g = g * (1 - .78f * face) + (136.f + 18.f * (1 - fy)) * .78f * face;
            b = b * (1 - .78f * face) + (101.f + 13.f * (1 - fy)) * .78f * face;
            float cyan = std::exp(-((fx - .77f) * (fx - .77f) / .018f + (fy - .68f) * (fy - .68f) / .065f));
            r = r * (1 - .62f * cyan) + 18.f * .62f * cyan;
            g = g * (1 - .62f * cyan) + 150.f * .62f * cyan;
            b = b * (1 - .62f * cyan) + 185.f * .62f * cyan;
            float tex = (std::sin(float(x) * .071f) + std::sin(float(y) * .053f)) * 2.2f;
            r += tex;
            g += tex;
            b += tex;
            auto* p = &a[(static_cast<std::size_t>(y) * w + x) * 4u];
            p[0] = u8(r);
            p[1] = u8(g);
            p[2] = u8(b);
        }
    }
    return a;
}
Pixels multicolorPhoto(std::uint32_t w, std::uint32_t h) {
    auto a = rgba(w, h);
    auto u8 = [](float v) { return static_cast<std::uint8_t>(std::clamp(v, 0.f, 255.f)); };
    for (std::uint32_t y = 0; y < h; ++y) {
        for (std::uint32_t x = 0; x < w; ++x) {
            float fx = float(x) / float(w - 1), fy = float(y) / float(h - 1);
            float r = 24, g = 25, b = 30;
            auto glow = [&](float cx, float cy, float sx, float sy) {
                float dx = (fx - cx) / sx, dy = (fy - cy) / sy;
                return std::exp(-.5f * (dx * dx + dy * dy));
            };
            float gr = glow(.12f, .35f, .28f, .55f), gb = glow(.88f, .30f, .30f, .50f),
                  gg = glow(.78f, .78f, .24f, .28f), gy = glow(.26f, .82f, .22f, .28f),
                  gm = glow(.52f, .14f, .18f, .18f);
            r += 185 * gr + 12 * gb + 16 * gg + 155 * gy + 125 * gm;
            g += 12 * gr + 42 * gb + 165 * gg + 105 * gy + 8 * gm;
            b += 8 * gr + 195 * gb + 40 * gg + 8 * gy + 150 * gm;
            float face = 1.f - std::min(1.f, std::pow((fx - .52f) / .16f, 2.f) + std::pow((fy - .49f) / .25f, 2.f));
            face = face * face;
            r = r * (1 - .60f * face) + 190.f * .60f * face;
            g = g * (1 - .60f * face) + 125.f * .60f * face;
            b = b * (1 - .60f * face) + 92.f * .60f * face;
            float tex =
                (std::sin(float(x) * .071f) + std::sin(float(y) * .053f) + std::sin(float(x + y) * .031f)) * 2.0f;
            r += tex;
            g += tex;
            b += tex;
            auto* p = &a[(static_cast<std::size_t>(y) * w + x) * 4u];
            p[0] = u8(r);
            p[1] = u8(g);
            p[2] = u8(b);
        }
    }
    return a;
}
Pixels hueSweep(std::uint32_t w, std::uint32_t h) {
    auto a = rgba(w, h);
    for (std::uint32_t y = 0; y < h; ++y) {
        for (std::uint32_t x = 0; x < w; ++x) {
            float t = float(x) / float(w - 1) * 6.f;
            int k = int(std::floor(t)) % 6;
            float f = t - std::floor(t);
            float v = .28f + .72f * float(y) / float(h - 1);
            float r = 0, g = 0, b = 0;
            switch (k) {
                case 0:
                    r = 1;
                    g = f;
                    break;
                case 1:
                    r = 1 - f;
                    g = 1;
                    break;
                case 2:
                    g = 1;
                    b = f;
                    break;
                case 3:
                    g = 1 - f;
                    b = 1;
                    break;
                case 4:
                    r = f;
                    b = 1;
                    break;
                default:
                    r = 1;
                    b = 1 - f;
                    break;
            }
            auto* p = &a[(static_cast<std::size_t>(y) * w + x) * 4u];
            p[0] = std::uint8_t(std::clamp(r * v * 255.f, 0.f, 255.f));
            p[1] = std::uint8_t(std::clamp(g * v * 255.f, 0.f, 255.f));
            p[2] = std::uint8_t(std::clamp(b * v * 255.f, 0.f, 255.f));
        }
    }
    return a;
}
void ppm(const std::string& path, std::uint32_t w, std::uint32_t h, const Pixels& rgba) {
    FILE* f = std::fopen(path.c_str(), "wb");
    if (!f) throw std::runtime_error("cannot write " + path);
    std::fprintf(f, "P6\n%u %u\n255\n", w, h);
    for (std::size_t i = 0; i < static_cast<std::size_t>(w) * h; ++i) std::fwrite(&rgba[i * 4], 1, 3, f);
    std::fclose(f);
}
}  // namespace
int main(int argc, char** argv) {
    try {
        std::string out = argc > 1 ? argv[1] : "scope_review/gpu";
        std::filesystem::create_directories(out);
        t::VulkanTestContext vk;
        std::cout << "Using GPU: " << vk.deviceName() << "\n";
        ImageScopes scopes({{vk.physicalDevice(), vk.device(), nullptr}, 1});
        auto withInput = [&](const std::string& prefix, std::uint32_t W, std::uint32_t H, const Pixels& px, auto body) {
            auto input = vk.createRgbaImage(W, H);
            vk.uploadRgba(input, px);
            DisplayImageView iv{input.view, VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_LAYOUT_GENERAL, W, H};
            ppm(out + "/" + prefix + "_source.ppm", W, H, px);
            auto run = [&](const char* name, std::uint32_t ow, std::uint32_t oh, auto record) {
                auto target = vk.createRgbaImage(ow, oh);
                Pixels z(static_cast<std::size_t>(ow) * oh * 4u, 0);
                vk.uploadRgba(target, z);
                ScopeRenderTarget rt{target.view, VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_LAYOUT_GENERAL, ow, oh};
                vk.beginCommands();
                record(vk.commandBuffer(), rt);
                vk.submitAndWait();
                scopes.retireFrameSlot(0);
                ppm(out + "/" + prefix + "_" + name + ".ppm", ow, oh, vk.downloadRgba(target));
                vk.destroyRgbaImage(target);
            };
            body(iv, run);
            vk.destroyRgbaImage(input);
        };
        // Analytic renderer oracles.
        withInput("constant_gray", 640, 480, solid(640, 480, 128), [&](DisplayImageView iv, auto& run) {
            run("rgb_waveform", 640, 360, [&](VkCommandBuffer c, ScopeRenderTarget rt) {
                DisplayWaveformRecordInfo r{};
                r.commandBuffer = c;
                r.input = iv;
                r.renderTarget = rt;
                r.render.mode = WaveformMode::RgbOverlay;
                r.render.showGrid = false;
                r.samplingMode = SamplingMode::FullReference;
                scopes.recordDisplayWaveform(r);
            });
        });
        withInput("x_ramp", 640, 480, xRamp(640, 480), [&](DisplayImageView iv, auto& run) {
            run("rgb_waveform", 640, 360, [&](VkCommandBuffer c, ScopeRenderTarget rt) {
                DisplayWaveformRecordInfo r{};
                r.commandBuffer = c;
                r.input = iv;
                r.renderTarget = rt;
                r.render.mode = WaveformMode::RgbOverlay;
                r.render.showGrid = false;
                r.samplingMode = SamplingMode::FullReference;
                scopes.recordDisplayWaveform(r);
            });
        });
        withInput("color_target", 720, 480, colorTarget(720, 480), [&](DisplayImageView iv, auto& run) {
            run("vectorscope_256", 512, 512, [&](VkCommandBuffer c, ScopeRenderTarget rt) {
                VectorscopeRecordInfo r{};
                r.commandBuffer = c;
                r.input = iv;
                r.renderTarget = rt;
                r.render.showGrid = true;
                r.render.showTargets = true;
                r.render.densityGain = 1.f;
                r.render.pointSpreadBins = 1;
                r.samplingMode = SamplingMode::FullReference;
                r.grid = VectorscopeGrid::Reference256;
                scopes.recordVectorscope(r);
            });
        });
        withInput("warm_portrait", 900, 700, warmPortrait(900, 700), [&](DisplayImageView iv, auto& run) {
            run("vectorscope_256", 512, 512, [&](VkCommandBuffer c, ScopeRenderTarget rt) {
                VectorscopeRecordInfo r{};
                r.commandBuffer = c;
                r.input = iv;
                r.renderTarget = rt;
                r.samplingMode = SamplingMode::FullReference;
                r.grid = VectorscopeGrid::Reference256;
                scopes.recordVectorscope(r);
            });
        });
        withInput("multicolor_photo", 960, 720, multicolorPhoto(960, 720), [&](DisplayImageView iv, auto& run) {
            run("vectorscope_256", 512, 512, [&](VkCommandBuffer c, ScopeRenderTarget rt) {
                VectorscopeRecordInfo r{};
                r.commandBuffer = c;
                r.input = iv;
                r.renderTarget = rt;
                r.samplingMode = SamplingMode::FullReference;
                r.grid = VectorscopeGrid::Reference256;
                scopes.recordVectorscope(r);
            });
        });
        withInput("hue_sweep", 900, 600, hueSweep(900, 600), [&](DisplayImageView iv, auto& run) {
            run("vectorscope_256", 512, 512, [&](VkCommandBuffer c, ScopeRenderTarget rt) {
                VectorscopeRecordInfo r{};
                r.commandBuffer = c;
                r.input = iv;
                r.renderTarget = rt;
                r.samplingMode = SamplingMode::FullReference;
                r.grid = VectorscopeGrid::Reference256;
                scopes.recordVectorscope(r);
            });
        });
        // Photographic-style fixture: full-reference for visual shape, independent of sampling policy.
        withInput("daylight_hdr", 960, 540, photo(960, 540), [&](DisplayImageView iv, auto& run) {
            run("rgb_waveform", 640, 360, [&](VkCommandBuffer c, ScopeRenderTarget rt) {
                DisplayWaveformRecordInfo r{};
                r.commandBuffer = c;
                r.input = iv;
                r.renderTarget = rt;
                r.render.mode = WaveformMode::RgbOverlay;
                r.samplingMode = SamplingMode::FullReference;
                scopes.recordDisplayWaveform(r);
            });
            run("vectorscope_256", 512, 512, [&](VkCommandBuffer c, ScopeRenderTarget rt) {
                VectorscopeRecordInfo r{};
                r.commandBuffer = c;
                r.input = iv;
                r.renderTarget = rt;
                r.samplingMode = SamplingMode::FullReference;
                r.grid = VectorscopeGrid::Reference256;
                r.render.densityGain = 1.f;
                r.render.pointSpreadBins = 1;
                scopes.recordVectorscope(r);
            });
        });
        std::cout << "IMAGE_SCOPES_VULKAN_ARTIFACTS_PASS " << out << "\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "FAIL: " << e.what() << "\n";
        return 1;
    }
}
