#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

#include "image_scopes/image_scopes.h"
#include "vulkan_test_context.hpp"
using namespace image_scopes;
using namespace image_scopes::tests;
namespace {
struct Pixels {
    std::uint32_t w = 0, h = 0;
    std::vector<std::uint8_t> rgba;
};
std::string tok(std::istream& f) {
    std::string s;
    while (f >> s) {
        if (!s.empty() && s[0] == '#') {
            std::getline(f, s);
            continue;
        }
        return s;
    }
    throw std::runtime_error("ppm eof");
}
Pixels ppm(const std::filesystem::path& p) {
    std::ifstream f(p, std::ios::binary);
    if (!f || tok(f) != "P6") throw std::runtime_error("bad ppm " + p.string());
    Pixels x{};
    x.w = std::stoul(tok(f));
    x.h = std::stoul(tok(f));
    if (std::stoul(tok(f)) != 255u) throw std::runtime_error("bad maxval");
    f.get();
    std::vector<std::uint8_t> rgb(size_t(x.w) * x.h * 3u);
    f.read(reinterpret_cast<char*>(rgb.data()), std::streamsize(rgb.size()));
    if (size_t(f.gcount()) != rgb.size()) throw std::runtime_error("short ppm");
    x.rgba.resize(size_t(x.w) * x.h * 4u);
    for (size_t i = 0; i < size_t(x.w) * x.h; ++i) {
        x.rgba[i * 4] = rgb[i * 3];
        x.rgba[i * 4 + 1] = rgb[i * 3 + 1];
        x.rgba[i * 4 + 2] = rgb[i * 3 + 2];
        x.rgba[i * 4 + 3] = 255;
    }
    return x;
}
void save(const std::filesystem::path& p, const std::vector<std::uint8_t>& rgba) {
    std::ofstream f(p, std::ios::binary);
    f << "P6\n512 512\n255\n";
    for (size_t i = 0; i < 512u * 512u; ++i) f.write(reinterpret_cast<const char*>(&rgba[i * 4]), 3);
}
}  // namespace
int main(int argc, char** argv) {
    try {
        if (argc != 4)
            throw std::invalid_argument("usage: image_scopes_vectorscope_photo_artifacts FIXTURE_DIR OUT_DIR basename");
        const std::filesystem::path fixtures = argv[1], out = argv[2];
        const std::string base = argv[3];
        std::filesystem::create_directories(out);
        const auto px = ppm(fixtures / (base + ".ppm"));
        VulkanTestContext vk;
        CreateInfo ci{};
        ci.context.physicalDevice = vk.physicalDevice();
        ci.context.device = vk.device();
        ci.maxFramesInFlight = 1;
        ImageScopes scopes(ci);
        auto in = vk.createRgbaImage(px.w, px.h);
        vk.uploadRgba(in, px.rgba);
        auto dst = vk.createRgbaImage(512, 512);
        std::vector<std::uint8_t> zero(512u * 512u * 4u, 0);
        vk.uploadRgba(dst, zero);
        DisplayImageView iv{in.view, VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_LAYOUT_GENERAL, px.w, px.h};
        ScopeRenderTarget rt{dst.view, VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_LAYOUT_GENERAL, 512, 512};
        vk.beginCommands();
        VectorscopeRecordInfo r{};
        r.commandBuffer = vk.commandBuffer();
        r.input = iv;
        r.renderTarget = rt;
        r.samplingMode = SamplingMode::FullReference;
        r.grid = VectorscopeGrid::Reference256;
        r.render.showGrid = true;
        r.render.showTargets = true;
        r.render.densityGain = 1.f;
        scopes.recordVectorscope(r);
        vk.submitAndWait();
        save(out / (base + "_vectorscope.ppm"), vk.downloadRgba(dst));

        // Data-only presentation from the already-measured slot. This is machine
        // checked by the release gate; the graticule version above remains the human
        // visual-acceptance artifact. Use a separate target so the test does not
        // depend on re-upload semantics after a shader-written/read-back image.
        auto dataDst = vk.createRgbaImage(512, 512);
        vk.uploadRgba(dataDst, zero);
        ScopeRenderTarget dataRt{dataDst.view, VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_LAYOUT_GENERAL, 512, 512};
        vk.beginCommands();
        VectorscopeRenderInfo rr{};
        rr.commandBuffer = vk.commandBuffer();
        rr.renderTarget = dataRt;
        rr.render.showGrid = false;
        rr.render.showTargets = false;
        rr.render.showBackground = false;
        rr.render.densityGain = 1.f;
        scopes.recordVectorscopeRender(rr);
        vk.submitAndWait();
        save(out / (base + "_vectorscope_data.ppm"), vk.downloadRgba(dataDst));

        vk.destroyRgbaImage(dataDst);
        vk.destroyRgbaImage(dst);
        vk.destroyRgbaImage(in);
        std::cout << "V22_PHOTO_ARTIFACT_PASS " << base << "\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "V22_PHOTO_ARTIFACT_FAIL: " << e.what() << "\n";
        return 1;
    }
}
