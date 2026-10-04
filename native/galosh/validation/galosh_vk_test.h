#pragma once
// Shared MoltenVK/desktop init for galosh validation runners (smoke +
// validate). Dense house style (cf. false_color_correction vk_test_common).
// The device enables f16 SSBO storage + float16 arithmetic: the vendored
// shaders cannot legally execute without them (same requirement the app
// device carries via VulkanContext::float16ComputeEnabled).
#include <vulkan/vulkan.h>

#include <cstdint>
#include <cstring>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

#include "galosh/GaloshCommon.hpp"

namespace galoshtest {
inline void ck(VkResult r, const char* m) {
    if (r != VK_SUCCESS) throw std::runtime_error(m);
}
struct Ctx {
    VkInstance inst{};
    VkPhysicalDevice pd{};
    VkDevice dev{};
    VkQueue q{};
    uint32_t qf{};
    float tsPeriod = 1.0f;
};
inline Ctx ctx() {
    Ctx c{};
    VkApplicationInfo a{};
    a.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    a.apiVersion = VK_API_VERSION_1_2;
    VkInstanceCreateInfo ii{};
    ii.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    ii.pApplicationInfo = &a;
#ifdef __APPLE__
    ii.flags |= VK_INSTANCE_CREATE_ENUMERATE_PORTABILITY_BIT_KHR;
    const char* ie[] = {"VK_KHR_portability_enumeration"};
    ii.enabledExtensionCount = 1;
    ii.ppEnabledExtensionNames = ie;
#endif
    ck(vkCreateInstance(&ii, nullptr, &c.inst), "instance");
    uint32_t n = 0;
    ck(vkEnumeratePhysicalDevices(c.inst, &n, nullptr), "pd");
    if (!n) throw std::runtime_error("No Vulkan GPU");
    std::vector<VkPhysicalDevice> ps(n);
    vkEnumeratePhysicalDevices(c.inst, &n, ps.data());
    c.pd = ps[0];
    uint32_t nq = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(c.pd, &nq, nullptr);
    std::vector<VkQueueFamilyProperties> qp(nq);
    vkGetPhysicalDeviceQueueFamilyProperties(c.pd, &nq, qp.data());
    c.qf = UINT32_MAX;
    for (uint32_t i = 0; i < nq; i++)
        if (qp[i].queueFlags & VK_QUEUE_COMPUTE_BIT) {
            c.qf = i;
            break;
        }
    if (c.qf == UINT32_MAX) throw std::runtime_error("No compute queue");
    // f16 probe: fail fast when the lab GPU cannot run the vendored shaders.
    VkPhysicalDevice16BitStorageFeatures s16{};
    s16.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_16BIT_STORAGE_FEATURES;
    VkPhysicalDeviceShaderFloat16Int8Features f16{};
    f16.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_FLOAT16_INT8_FEATURES;
    f16.pNext = &s16;
    VkPhysicalDeviceFeatures2 f2{};
    f2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;
    f2.pNext = &f16;
    vkGetPhysicalDeviceFeatures2(c.pd, &f2);
    if (s16.storageBuffer16BitAccess != VK_TRUE || f16.shaderFloat16 != VK_TRUE)
        throw std::runtime_error("GPU lacks f16 storage/arithmetic");
    float pr = 1;
    VkDeviceQueueCreateInfo qi{};
    qi.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
    qi.queueFamilyIndex = c.qf;
    qi.queueCount = 1;
    qi.pQueuePriorities = &pr;
    VkPhysicalDevice16BitStorageFeatures s16e{};
    s16e.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_16BIT_STORAGE_FEATURES;
    s16e.storageBuffer16BitAccess = VK_TRUE;
    VkPhysicalDeviceShaderFloat16Int8Features f16e{};
    f16e.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_FLOAT16_INT8_FEATURES;
    f16e.pNext = &s16e;
    f16e.shaderFloat16 = VK_TRUE;
    VkDeviceCreateInfo di{};
    di.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
    di.pNext = &f16e;
    di.queueCreateInfoCount = 1;
    di.pQueueCreateInfos = &qi;
#ifdef __APPLE__
    const char* de[] = {"VK_KHR_portability_subset"};
    di.enabledExtensionCount = 1;
    di.ppEnabledExtensionNames = de;
#endif
    ck(vkCreateDevice(c.pd, &di, nullptr, &c.dev), "device");
    vkGetDeviceQueue(c.dev, c.qf, 0, &c.q);
    VkPhysicalDeviceProperties prop{};
    vkGetPhysicalDeviceProperties(c.pd, &prop);
    c.tsPeriod = prop.limits.timestampPeriod;
    std::cout << "device: " << prop.deviceName << " tsPeriod=" << c.tsPeriod << "ns\n";
    return c;
}
inline void delCtx(Ctx& c) {
    if (c.dev) {
        vkDeviceWaitIdle(c.dev);
        vkDestroyDevice(c.dev, nullptr);
    }
    if (c.inst) vkDestroyInstance(c.inst, nullptr);
    c = {};
}
struct Buf {
    VkBuffer b{};
    VkDeviceMemory m{};
};
inline Buf mkBuf(Ctx& c, VkDeviceSize n, VkBufferUsageFlags u, VkMemoryPropertyFlags p) {
    Buf x{};
    VkBufferCreateInfo ci{};
    ci.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    ci.size = n;
    ci.usage = u;
    ci.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    ck(vkCreateBuffer(c.dev, &ci, nullptr, &x.b), "buffer");
    VkMemoryRequirements mr{};
    vkGetBufferMemoryRequirements(c.dev, x.b, &mr);
    VkMemoryAllocateInfo ai{};
    ai.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    ai.allocationSize = mr.size;
    VkPhysicalDeviceMemoryProperties mp{};
    vkGetPhysicalDeviceMemoryProperties(c.pd, &mp);
    uint32_t idx = UINT32_MAX;
    for (uint32_t i = 0; i < mp.memoryTypeCount; i++)
        if ((mr.memoryTypeBits & (1u << i)) && (mp.memoryTypes[i].propertyFlags & p) == p) idx = i;
    if (idx == UINT32_MAX) throw std::runtime_error("No memory type");
    ai.memoryTypeIndex = idx;
    ck(vkAllocateMemory(c.dev, &ai, nullptr, &x.m), "buf mem");
    ck(vkBindBufferMemory(c.dev, x.b, x.m, 0), "buf bind");
    return x;
}
inline void delBuf(Ctx& c, Buf& x) {
    if (x.b) vkDestroyBuffer(c.dev, x.b, nullptr);
    if (x.m) vkFreeMemory(c.dev, x.m, nullptr);
    x = {};
}
struct Img {
    VkImage i{};
    VkDeviceMemory m{};
    VkImageView v{};
};
inline Img mkImg(Ctx& c, uint32_t w, uint32_t h, VkFormat f, VkImageUsageFlags u) {
    Img x{};
    VkImageCreateInfo ci{};
    ci.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    ci.imageType = VK_IMAGE_TYPE_2D;
    ci.format = f;
    ci.extent = {w, h, 1};
    ci.mipLevels = 1;
    ci.arrayLayers = 1;
    ci.samples = VK_SAMPLE_COUNT_1_BIT;
    ci.tiling = VK_IMAGE_TILING_OPTIMAL;
    ci.usage = u;
    ci.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    ci.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    ck(vkCreateImage(c.dev, &ci, nullptr, &x.i), "image");
    VkMemoryRequirements mr{};
    vkGetImageMemoryRequirements(c.dev, x.i, &mr);
    VkMemoryAllocateInfo ai{};
    ai.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    ai.allocationSize = mr.size;
    VkPhysicalDeviceMemoryProperties mp{};
    vkGetPhysicalDeviceMemoryProperties(c.pd, &mp);
    uint32_t idx = UINT32_MAX;
    for (uint32_t i = 0; i < mp.memoryTypeCount; i++)
        if ((mr.memoryTypeBits & (1u << i)) &&
            (mp.memoryTypes[i].propertyFlags & VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT))
            idx = i;
    if (idx == UINT32_MAX) throw std::runtime_error("No memory type");
    ai.memoryTypeIndex = idx;
    ck(vkAllocateMemory(c.dev, &ai, nullptr, &x.m), "img mem");
    ck(vkBindImageMemory(c.dev, x.i, x.m, 0), "img bind");
    VkImageViewCreateInfo vi{};
    vi.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    vi.image = x.i;
    vi.viewType = VK_IMAGE_VIEW_TYPE_2D;
    vi.format = f;
    vi.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    ck(vkCreateImageView(c.dev, &vi, nullptr, &x.v), "view");
    return x;
}
inline void delImg(Ctx& c, Img& x) {
    if (x.v) vkDestroyImageView(c.dev, x.v, nullptr);
    if (x.i) vkDestroyImage(c.dev, x.i, nullptr);
    if (x.m) vkFreeMemory(c.dev, x.m, nullptr);
    x = {};
}
// Kernel names the ports look up (mirrors kKernels[]; optional ones
// included so absence handling is exercised too).
inline std::vector<std::string> rawSpvNames() {
    return {"o32_ne_block_stats", "o32_ne_finalize", "o32_ne_dark_thresh_hist",
            "o32_ne_dark_thresh_finalize", "o32_ne_dark_lap_hist", "o32_ne_dark_finalize",
            "o32_build_inv_lut", "o32_lut_finalize", "o32_gat_forward_full", "o32_sigma_per_cfa",
            "o32_unified_sigma", "o32_normalize_apply", "o32_dark_ref_reduce_mwg",
            "o32_dark_ref_finalize_mwg", "o32_dark_resid_reduce_mwg", "o32_dark_resid_finalize_mwg",
            "o32_dark_sub_full", "o32_forward_l_stride1", "o32_chroma_extract_halfres", "o32_pass12",
            "o32_lpixel_lh_den_fused", "o32_box_downsample_2x", "o32_box_downsample_2x_3p",
            "o32_loess_chroma_3p_tiled", "o32_crop_2d_topleft", "o32_k16_jbu_3p",
            "o32_smoothstep_blend_3p", "o32_pass12_wht4", "o32_fastup_3p",
            "o32_box_downsample_2x_h16", "o32_crop_2d_topleft_h16",
            "o32_loess_chroma_3p_tiled_g16", "o32_k16_jbu_3p_f16", "o32_fastup_3p_f16",
            "o32_sigma_hist_mwg", "o32_sigma_fin_mwg", "o32_pad_2d_edge_3p",
            "o32_k16_inverse_fused", "o32_fastup_inverse_fused", "galosh_bridge_norm",
            "galosh_bridge_quant",
            // YUV engine (O-variant + linear bridges).
            "yuv_lap_mad", "yuv_lap_mad_h16", "yuv_synth_alpha", "yuv_gat_fwd",
            "yuv_sigma_norm", "yuv_sigma_denorm", "yuv_makitalo", "yuv_loess",
            "yuv_env_block_stats", "yuv_env_select", "yuv_env_dark_thresh_hist",
            "yuv_env_dark_lap_hist", "galosh_yuv_bridge_in", "galosh_yuv_bridge_out"};
}
inline galosh::GaloshShaderMap spvMap(const std::string& dir, std::vector<std::vector<char>>& keep) {
    galosh::GaloshShaderMap map;
    for (const auto& n : rawSpvNames()) {
        std::ifstream f(dir + "/" + n + ".spv", std::ios::binary);
        if (!f) throw std::runtime_error("cannot open " + dir + "/" + n + ".spv");
        keep.emplace_back((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
        galosh::GaloshShader s;
        s.words = reinterpret_cast<const uint32_t*>(keep.back().data());
        s.wordCount = keep.back().size() / 4;
        map[n] = s;
    }
    return map;
}
}  // namespace galoshtest
