#include <array>
#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

#include "image_scopes/image_scopes.h"
#include "vulkan_test_context.hpp"

using namespace image_scopes;
namespace t = image_scopes::tests;

namespace {
void check(VkResult r, const char* what) {
    if (r != VK_SUCCESS) throw std::runtime_error(what);
}

std::vector<std::uint8_t> pattern(std::uint32_t w, std::uint32_t h) {
    std::vector<std::uint8_t> a(static_cast<std::size_t>(w) * h * 4u);
    for (std::uint32_t y = 0; y < h; ++y)
        for (std::uint32_t x = 0; x < w; ++x) {
            auto* p = &a[(static_cast<std::size_t>(y) * w + x) * 4u];
            p[0] = static_cast<std::uint8_t>((x * 17u + y * 3u) & 255u);
            p[1] = static_cast<std::uint8_t>((x * 5u + y * 29u) & 255u);
            p[2] = static_cast<std::uint8_t>(((x ^ y) * 11u) & 255u);
            p[3] = 255u;
        }
    return a;
}
struct Slot {
    VkCommandBuffer cmd = VK_NULL_HANDLE;
    VkFence fence = VK_NULL_HANDLE;
    t::RgbaImage wave{}, vec{};
};

void runCase(t::VulkanTestContext& vk, std::uint32_t W, std::uint32_t H) {
    constexpr std::uint32_t kSlots = 3, kFrames = 1200;
    auto pixels = pattern(W, H);
    auto input = vk.createRgbaImage(W, H);
    vk.uploadRgba(input, pixels);
    ImageScopes scopes({{vk.physicalDevice(), vk.device(), nullptr}, kSlots});
    DisplayImageView iv{input.view, VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_LAYOUT_GENERAL, W, H};
    std::array<Slot, kSlots> slots{};
    std::array<VkCommandBuffer, kSlots> cmds{};
    VkCommandBufferAllocateInfo cai{};
    cai.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    cai.commandPool = vk.commandPool();
    cai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    cai.commandBufferCount = kSlots;
    check(vkAllocateCommandBuffers(vk.device(), &cai, cmds.data()), "stability command-buffer allocation failed");
    try {
        for (std::uint32_t s = 0; s < kSlots; ++s) {
            slots[s].cmd = cmds[s];
            VkFenceCreateInfo fci{};
            fci.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
            fci.flags = VK_FENCE_CREATE_SIGNALED_BIT;
            check(vkCreateFence(vk.device(), &fci, nullptr, &slots[s].fence), "stability fence creation failed");
            slots[s].wave = vk.createRgbaImage(640, 360);
            slots[s].vec = vk.createRgbaImage(512, 512);
            std::vector<std::uint8_t> wz(640u * 360u * 4u, 0u), vz(512u * 512u * 4u, 0u);
            vk.uploadRgba(slots[s].wave, wz);
            vk.uploadRgba(slots[s].vec, vz);
        }
        for (std::uint32_t frame = 0; frame < kFrames; ++frame) {
            const std::uint32_t s = frame % kSlots;
            auto& slot = slots[s];
            constexpr std::uint64_t timeout = 10'000'000'000ull;
            const VkResult wr = vkWaitForFences(vk.device(), 1, &slot.fence, VK_TRUE, timeout);
            if (wr == VK_TIMEOUT)
                throw std::runtime_error("stability fence timeout frame=" + std::to_string(frame) +
                                         " slot=" + std::to_string(s));
            check(wr, "stability slot wait failed");
            scopes.retireFrameSlot(s);
            check(vkResetFences(vk.device(), 1, &slot.fence), "stability slot fence reset failed");
            check(vkResetCommandBuffer(slot.cmd, 0), "stability command-buffer reset failed");
            VkCommandBufferBeginInfo bi{};
            bi.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
            bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
            check(vkBeginCommandBuffer(slot.cmd, &bi), "stability command-buffer begin failed");
            ScopeRenderTarget wt{slot.wave.view, VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_LAYOUT_GENERAL, 640, 360};
            ScopeRenderTarget vt{slot.vec.view, VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_LAYOUT_GENERAL, 512, 512};
            DisplayWaveformRecordInfo wri{};
            wri.commandBuffer = slot.cmd;
            wri.input = iv;
            wri.renderTarget = wt;
            wri.frameSlot = s;
            wri.render.mode = WaveformMode::RgbOverlay;
            wri.samplingMode = SamplingMode::Production50;
            scopes.recordDisplayWaveform(wri);
            VectorscopeRecordInfo vr{};
            vr.commandBuffer = slot.cmd;
            vr.input = iv;
            vr.renderTarget = vt;
            vr.frameSlot = s;
            vr.samplingMode = SamplingMode::Production25;
            vr.grid = VectorscopeGrid::Reference256;
            scopes.recordVectorscope(vr);
            check(vkEndCommandBuffer(slot.cmd), "stability command-buffer end failed");
            VkSubmitInfo si{};
            si.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
            si.commandBufferCount = 1;
            si.pCommandBuffers = &slot.cmd;
            check(vkQueueSubmit(vk.queue(), 1, &si, slot.fence), "stability queue submit failed");
            if (((frame + 1u) % 120u) == 0u)
                std::cout << "STABILITY_PROGRESS " << W << "x" << H << " frames=" << (frame + 1u) << "/" << kFrames
                          << std::endl;
        }
        for (auto& slot : slots) {
            constexpr std::uint64_t timeout = 10'000'000'000ull;
            const VkResult wr = vkWaitForFences(vk.device(), 1, &slot.fence, VK_TRUE, timeout);
            if (wr == VK_TIMEOUT) throw std::runtime_error("stability final fence timeout");
            check(wr, "stability final wait failed");
        }
        const auto w0 = vk.downloadRgba(slots[0].wave), v0 = vk.downloadRgba(slots[0].vec);
        for (std::uint32_t s = 1; s < kSlots; ++s) {
            if (vk.downloadRgba(slots[s].wave) != w0)
                throw std::runtime_error("waveform output differs across frame slots");
            if (vk.downloadRgba(slots[s].vec) != v0)
                throw std::runtime_error("vectorscope output differs across frame slots");
        }
        std::cout << "THREE_FRAMES_IN_FLIGHT_PASS " << W << "x" << H << " frames=" << kFrames << " slots=" << kSlots
                  << std::endl;
    } catch (...) {
        for (auto& slot : slots) {
            if (slot.fence) vkDestroyFence(vk.device(), slot.fence, nullptr);
            vk.destroyRgbaImage(slot.vec);
            vk.destroyRgbaImage(slot.wave);
        }
        vkFreeCommandBuffers(vk.device(), vk.commandPool(), kSlots, cmds.data());
        vk.destroyRgbaImage(input);
        throw;
    }
    for (auto& slot : slots) {
        if (slot.fence) vkDestroyFence(vk.device(), slot.fence, nullptr);
        vk.destroyRgbaImage(slot.vec);
        vk.destroyRgbaImage(slot.wave);
    }
    vkFreeCommandBuffers(vk.device(), vk.commandPool(), kSlots, cmds.data());
    vk.destroyRgbaImage(input);
}
}  // namespace

int main() {
    try {
        t::VulkanTestContext vk;
        std::cout << "Using GPU: " << vk.deviceName() << std::endl;
        runCase(vk, 2048u, 1536u);
        runCase(vk, 2040u, 1532u);
        runCase(vk, 2040u, 1536u);
        std::cout << "IMAGE_SCOPES_VULKAN_STABILITY_PASS" << std::endl;
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "FAIL: " << e.what() << "\n";
        return 1;
    }
}
