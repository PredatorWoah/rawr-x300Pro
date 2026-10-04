// Standalone tonemap-shadow zebra validation: GPU/CPU parity plus slider linkage.
#include <vulkan/vulkan.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include "monitoring_overlays/cpu_reference.h"
#include "monitoring_overlays/monitoring_overlays.h"

using namespace monitoring_overlays;

namespace {

void check(VkResult r, const char* what) {
    if (r != VK_SUCCESS) throw std::runtime_error(std::string(what) + " VkResult=" + std::to_string(int(r)));
}
std::vector<uint32_t> loadSpv(const std::string& path) {
    std::ifstream f(path, std::ios::binary | std::ios::ate);
    if (!f) throw std::runtime_error("cannot open SPIR-V: " + path);
    auto bytes = f.tellg();
    if (bytes <= 0 || (bytes % 4) != 0) throw std::runtime_error("invalid SPIR-V: " + path);
    std::vector<uint32_t> v(size_t(bytes) / 4);
    f.seekg(0);
    f.read(reinterpret_cast<char*>(v.data()), bytes);
    if (!f) throw std::runtime_error("cannot read SPIR-V: " + path);
    return v;
}
uint32_t memoryType(VkPhysicalDevice pd, uint32_t bits, VkMemoryPropertyFlags required,
                    VkMemoryPropertyFlags preferred = 0) {
    VkPhysicalDeviceMemoryProperties mp{};
    vkGetPhysicalDeviceMemoryProperties(pd, &mp);
    for (int pass = 0; pass < 2; pass++)
        for (uint32_t i = 0; i < mp.memoryTypeCount; i++) {
            if (!(bits & (1u << i))) continue;
            auto f = mp.memoryTypes[i].propertyFlags;
            if ((f & required) != required) continue;
            if (pass == 0 && preferred && (f & preferred) != preferred) continue;
            return i;
        }
    throw std::runtime_error("no compatible Vulkan memory type");
}
struct Buffer {
    VkDevice d = VK_NULL_HANDLE;
    VkBuffer b = VK_NULL_HANDLE;
    VkDeviceMemory m = VK_NULL_HANDLE;
    void destroy() {
        if (b) vkDestroyBuffer(d, b, nullptr);
        if (m) vkFreeMemory(d, m, nullptr);
        b = VK_NULL_HANDLE;
        m = VK_NULL_HANDLE;
    }
    ~Buffer() { destroy(); }
};
Buffer makeBuffer(VkPhysicalDevice pd, VkDevice d, VkDeviceSize size, VkBufferUsageFlags usage) {
    Buffer x;
    x.d = d;
    VkBufferCreateInfo bi{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
    bi.size = size;
    bi.usage = usage;
    bi.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    check(vkCreateBuffer(d, &bi, nullptr, &x.b), "vkCreateBuffer");
    VkMemoryRequirements r{};
    vkGetBufferMemoryRequirements(d, x.b, &r);
    VkMemoryAllocateInfo ai{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
    ai.allocationSize = r.size;
    ai.memoryTypeIndex =
        memoryType(pd, r.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT, VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    check(vkAllocateMemory(d, &ai, nullptr, &x.m), "vkAllocateMemory(buffer)");
    check(vkBindBufferMemory(d, x.b, x.m, 0), "vkBindBufferMemory");
    return x;
}
struct Image {
    VkDevice d = VK_NULL_HANDLE;
    VkImage image = VK_NULL_HANDLE;
    VkDeviceMemory mem = VK_NULL_HANDLE;
    VkImageView view = VK_NULL_HANDLE;
    void destroy() {
        if (view) vkDestroyImageView(d, view, nullptr);
        if (image) vkDestroyImage(d, image, nullptr);
        if (mem) vkFreeMemory(d, mem, nullptr);
        view = VK_NULL_HANDLE;
        image = VK_NULL_HANDLE;
        mem = VK_NULL_HANDLE;
    }
    ~Image() { destroy(); }
};
Image makeImage(VkPhysicalDevice pd, VkDevice d, uint32_t w, uint32_t h, VkFormat fmt, VkImageUsageFlags usage) {
    Image x;
    x.d = d;
    VkImageCreateInfo ci{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
    ci.imageType = VK_IMAGE_TYPE_2D;
    ci.format = fmt;
    ci.extent = {w, h, 1};
    ci.mipLevels = 1;
    ci.arrayLayers = 1;
    ci.samples = VK_SAMPLE_COUNT_1_BIT;
    ci.tiling = VK_IMAGE_TILING_OPTIMAL;
    ci.usage = usage;
    ci.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    ci.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    check(vkCreateImage(d, &ci, nullptr, &x.image), "vkCreateImage");
    VkMemoryRequirements r{};
    vkGetImageMemoryRequirements(d, x.image, &r);
    VkMemoryAllocateInfo ai{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
    ai.allocationSize = r.size;
    ai.memoryTypeIndex = memoryType(pd, r.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    check(vkAllocateMemory(d, &ai, nullptr, &x.mem), "vkAllocateMemory(image)");
    check(vkBindImageMemory(d, x.image, x.mem, 0), "vkBindImageMemory");
    VkImageViewCreateInfo vi{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
    vi.image = x.image;
    vi.viewType = VK_IMAGE_VIEW_TYPE_2D;
    vi.format = fmt;
    vi.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    vi.subresourceRange.levelCount = 1;
    vi.subresourceRange.layerCount = 1;
    check(vkCreateImageView(d, &vi, nullptr, &x.view), "vkCreateImageView");
    return x;
}
struct Vulkan {
    VkInstance instance = VK_NULL_HANDLE;
    VkPhysicalDevice pd = VK_NULL_HANDLE;
    VkDevice device = VK_NULL_HANDLE;
    uint32_t qf = 0;
    VkQueue queue = VK_NULL_HANDLE;
    VkCommandPool pool = VK_NULL_HANDLE;
    ~Vulkan() {
        if (device) vkDeviceWaitIdle(device);
        if (pool) vkDestroyCommandPool(device, pool, nullptr);
        if (device) vkDestroyDevice(device, nullptr);
        if (instance) vkDestroyInstance(instance, nullptr);
    }
};
Vulkan createVulkan() {
    Vulkan v;
    std::vector<const char*> ie;
    VkInstanceCreateFlags flags = 0;
#ifdef VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME
    {
        uint32_t n = 0;
        vkEnumerateInstanceExtensionProperties(nullptr, &n, nullptr);
        std::vector<VkExtensionProperties> p(n);
        vkEnumerateInstanceExtensionProperties(nullptr, &n, p.data());
        for (auto& e : p)
            if (std::strcmp(e.extensionName, VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME) == 0) {
                ie.push_back(VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME);
                flags |= VK_INSTANCE_CREATE_ENUMERATE_PORTABILITY_BIT_KHR;
                break;
            }
    }
#endif
    VkApplicationInfo ai{VK_STRUCTURE_TYPE_APPLICATION_INFO};
    ai.pApplicationName = "monitoring_overlays_shadow_validation";
    ai.apiVersion = VK_API_VERSION_1_1;
    VkInstanceCreateInfo ici{VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};
    ici.flags = flags;
    ici.pApplicationInfo = &ai;
    ici.enabledExtensionCount = uint32_t(ie.size());
    ici.ppEnabledExtensionNames = ie.data();
    check(vkCreateInstance(&ici, nullptr, &v.instance), "vkCreateInstance");
    uint32_t n = 0;
    check(vkEnumeratePhysicalDevices(v.instance, &n, nullptr), "count");
    if (!n) throw std::runtime_error("no Vulkan physical device");
    std::vector<VkPhysicalDevice> d(n);
    check(vkEnumeratePhysicalDevices(v.instance, &n, d.data()), "enum");
    v.pd = d[0];
    uint32_t qn = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(v.pd, &qn, nullptr);
    std::vector<VkQueueFamilyProperties> qp(qn);
    vkGetPhysicalDeviceQueueFamilyProperties(v.pd, &qn, qp.data());
    bool found = false;
    for (uint32_t i = 0; i < qn; i++)
        if (qp[i].queueFlags & VK_QUEUE_COMPUTE_BIT) {
            v.qf = i;
            found = true;
            break;
        }
    if (!found) throw std::runtime_error("no compute queue");
    float pri = 1;
    VkDeviceQueueCreateInfo qi{VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO};
    qi.queueFamilyIndex = v.qf;
    qi.queueCount = 1;
    qi.pQueuePriorities = &pri;
    std::vector<const char*> de;
#ifdef VK_KHR_PORTABILITY_SUBSET_EXTENSION_NAME
    {
        uint32_t n = 0;
        vkEnumerateDeviceExtensionProperties(v.pd, nullptr, &n, nullptr);
        std::vector<VkExtensionProperties> p(n);
        vkEnumerateDeviceExtensionProperties(v.pd, nullptr, &n, p.data());
        for (auto& e : p)
            if (std::strcmp(e.extensionName, VK_KHR_PORTABILITY_SUBSET_EXTENSION_NAME) == 0) {
                de.push_back(VK_KHR_PORTABILITY_SUBSET_EXTENSION_NAME);
                break;
            }
    }
#endif
    VkDeviceCreateInfo di{VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO};
    di.queueCreateInfoCount = 1;
    di.pQueueCreateInfos = &qi;
    di.enabledExtensionCount = uint32_t(de.size());
    di.ppEnabledExtensionNames = de.data();
    check(vkCreateDevice(v.pd, &di, nullptr, &v.device), "vkCreateDevice");
    vkGetDeviceQueue(v.device, v.qf, 0, &v.queue);
    VkCommandPoolCreateInfo pi{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
    pi.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    pi.queueFamilyIndex = v.qf;
    check(vkCreateCommandPool(v.device, &pi, nullptr, &v.pool), "vkCreateCommandPool");
    return v;
}
void barrier(VkCommandBuffer cb, VkImage img, VkImageLayout o, VkImageLayout n, VkAccessFlags sa, VkAccessFlags da,
             VkPipelineStageFlags ss, VkPipelineStageFlags ds) {
    VkImageMemoryBarrier b{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
    b.oldLayout = o;
    b.newLayout = n;
    b.srcAccessMask = sa;
    b.dstAccessMask = da;
    b.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    b.image = img;
    b.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    b.subresourceRange.levelCount = 1;
    b.subresourceRange.layerCount = 1;
    vkCmdPipelineBarrier(cb, ss, ds, 0, 0, nullptr, 0, nullptr, 1, &b);
}
uint8_t q8(float v) { return uint8_t(std::lround(std::clamp(v, 0.0f, 1.0f) * 255.0f)); }
float u8f(uint8_t v) { return float(v) * (1.0f / 255.0f); }

std::vector<uint8_t> makeDisplay(uint32_t w, uint32_t h) {
    std::vector<uint8_t> x(size_t(w) * h * 4);
    for (uint32_t y = 0; y < h; y++)
        for (uint32_t xx = 0; xx < w; xx++) {
            size_t i = (size_t(y) * w + xx) * 4;
            // Horizontal ramp 0..255 plus a dark top band and bright bottom band.
            uint8_t code = uint8_t((uint32_t(xx) * 255u) / (w > 1 ? (w - 1) : 1));
            if (y < h / 5) code = 6;
            if (y >= h * 4 / 5) code = 235;
            x[i] = x[i + 1] = x[i + 2] = code;
            x[i + 3] = 255;
        }
    return x;
}
std::vector<uint8_t> runShadow(Vulkan& v, MonitoringOverlays& proc, uint32_t w, uint32_t h,
                               const std::vector<uint8_t>& display, const TonemapShadowParams& p, uint32_t slot = 0) {
    size_t bytes = size_t(w) * h * 4;
    Buffer up = makeBuffer(v.pd, v.device, bytes, VK_BUFFER_USAGE_TRANSFER_SRC_BIT),
           rb = makeBuffer(v.pd, v.device, bytes, VK_BUFFER_USAGE_TRANSFER_DST_BIT);
    void* m = nullptr;
    check(vkMapMemory(v.device, up.m, 0, VK_WHOLE_SIZE, 0, &m), "map(write)");
    std::memcpy(m, display.data(), bytes);
    vkUnmapMemory(v.device, up.m);
    Image in = makeImage(v.pd, v.device, w, h, VK_FORMAT_R8G8B8A8_UNORM,
                         VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT),
          out = makeImage(v.pd, v.device, w, h, VK_FORMAT_R8G8B8A8_UNORM,
                          VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT);
    VkCommandBufferAllocateInfo ai{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
    ai.commandPool = v.pool;
    ai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    ai.commandBufferCount = 1;
    VkCommandBuffer cb{};
    check(vkAllocateCommandBuffers(v.device, &ai, &cb), "alloc");
    VkCommandBufferBeginInfo bi{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    check(vkBeginCommandBuffer(cb, &bi), "begin");
    barrier(cb, in.image, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 0,
            VK_ACCESS_TRANSFER_WRITE_BIT, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT);
    VkBufferImageCopy c{};
    c.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    c.imageSubresource.layerCount = 1;
    c.imageExtent = {w, h, 1};
    vkCmdCopyBufferToImage(cb, up.b, in.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &c);
    barrier(cb, in.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_GENERAL, VK_ACCESS_TRANSFER_WRITE_BIT,
            VK_ACCESS_SHADER_READ_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT);
    barrier(cb, out.image, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL, 0,
            VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
            VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT);
    TonemapShadowRecordInfo ri{};
    ri.commandBuffer = cb;
    ri.input = {in.view, VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_LAYOUT_GENERAL, w, h};
    ri.output = {out.view, VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_LAYOUT_GENERAL, w, h};
    ri.params = p;
    ri.frameSlot = slot;
    proc.recordTonemapShadow(ri);
    barrier(cb, out.image, VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_ACCESS_SHADER_WRITE_BIT,
            VK_ACCESS_TRANSFER_READ_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT);
    vkCmdCopyImageToBuffer(cb, out.image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, rb.b, 1, &c);
    check(vkEndCommandBuffer(cb), "end");
    VkSubmitInfo si{VK_STRUCTURE_TYPE_SUBMIT_INFO};
    si.commandBufferCount = 1;
    si.pCommandBuffers = &cb;
    check(vkQueueSubmit(v.queue, 1, &si, VK_NULL_HANDLE), "submit");
    check(vkQueueWaitIdle(v.queue), "wait");
    vkFreeCommandBuffers(v.device, v.pool, 1, &cb);
    check(vkMapMemory(v.device, rb.m, 0, VK_WHOLE_SIZE, 0, &m), "map(read)");
    std::vector<uint8_t> outBytes(bytes);
    std::memcpy(outBytes.data(), m, bytes);
    vkUnmapMemory(v.device, rb.m);
    return outBytes;
}
void validateParity(Vulkan& v, MonitoringOverlays& proc, const TonemapShadowParams& p, const char* label) {
    for (auto [w, h] : std::array<std::pair<uint32_t, uint32_t>, 3>{{{63, 65}, {640, 480}, {2048, 1536}}}) {
        auto in = makeDisplay(w, h);
        auto got = runShadow(v, proc, w, h, in, p);
        size_t bad = 0, first = 0;
        int maxDiff = 0;
        for (size_t i = 0; i < size_t(w) * h; i++) {
            uint32_t x = uint32_t(i % w), y = uint32_t(i / w);
            auto e = cpu_reference::tonemapShadowOverlay(int(x), int(y), u8f(in[4 * i]), u8f(in[4 * i + 1]),
                                                         u8f(in[4 * i + 2]), p);
            uint8_t eb[4] = {q8(e.r), q8(e.g), q8(e.b), q8(e.a)};
            bool mm = false;
            for (int k = 0; k < 4; k++) {
                int d = std::abs(int(got[4 * i + k]) - int(eb[k]));
                maxDiff = std::max(maxDiff, d);
                if (d > 1) mm = true;
            }
            if (mm) {
                if (!bad) first = i;
                bad++;
            }
        }
        if (bad) {
            size_t x0 = first % w, y0 = first / w;
            std::cerr << "first mismatch pixel x=" << x0 << " y=" << y0
                      << " code=" << int(in[4 * first]) << " got=(" << int(got[4 * first]) << ","
                      << int(got[4 * first + 1]) << "," << int(got[4 * first + 2]) << ","
                      << int(got[4 * first + 3]) << ")";
            auto e0 = cpu_reference::tonemapShadowOverlay(int(x0), int(y0), u8f(in[4 * first]),
                                                          u8f(in[4 * first + 1]), u8f(in[4 * first + 2]), p);
            std::cerr << " expected=(" << int(q8(e0.r)) << "," << int(q8(e0.g)) << "," << int(q8(e0.b)) << ","
                      << int(q8(e0.a)) << ") mask=" << cpu_reference::tonemapShadowMask(
                             u8f(in[4 * first]), u8f(in[4 * first + 1]), u8f(in[4 * first + 2]), p)
                      << " shadowsUI=" << p.shadowsUI << " blacksUI=" << p.blacksUI
                      << " thresh=" << p.thresholdIre << " period=" << p.stripePeriod
                      << " maxDiff=" << maxDiff << "\n";
            throw std::runtime_error(std::string("SHADOW parity failed ") + label + " " + std::to_string(w) +
                                     "x" + std::to_string(h) + " bad=" + std::to_string(bad) + " first=" +
                                     std::to_string(first) + " maxDiff=" + std::to_string(maxDiff));
        }
        std::cout << "SHADOW_GPU_PARITY_PASS " << label << " " << w << "x" << h << " tolerance_lsb=1 max_diff="
                  << maxDiff << "\n";
    }
}
void validateSliderLinkage(Vulkan& v, MonitoringOverlays& proc) {
    // The user's mental model: darkening (negative Shadows) must grow the zebra,
    // lifting (positive Shadows) must shrink it. The overlay reads the
    // already-tonemapped image, so simulate the slider by scaling the image
    // itself: darker image -> more zebra, brighter image -> less.
    const uint32_t w = 256, h = 128;
    auto base = makeDisplay(w, h);
    std::vector<uint8_t> dark = base, bright = base;
    for (size_t i = 0; i < size_t(w) * h; i++) {
        for (int k = 0; k < 3; k++) {
            dark[4 * i + k] = uint8_t(std::lround(std::clamp(float(base[4 * i + k]) * 0.45f, 0.0f, 255.0f)));
            bright[4 * i + k] = uint8_t(std::lround(std::clamp(float(base[4 * i + k]) * 1.8f, 0.0f, 255.0f)));
        }
    }
    TonemapShadowParams p{};
    p.thresholdIre = 10.0f;
    auto gotDark = runShadow(v, proc, w, h, dark, p);
    auto gotBase = runShadow(v, proc, w, h, base, p);
    auto gotBright = runShadow(v, proc, w, h, bright, p);
    size_t cd = 0, cb = 0, cl = 0;
    for (size_t i = 0; i < size_t(w) * h; i++) {
        if (gotDark[4 * i + 3]) cd++;
        if (gotBase[4 * i + 3]) cb++;
        if (gotBright[4 * i + 3]) cl++;
    }
    std::cout << "SHADOW_SLIDER darkened_coverage=" << double(cd) / double(w * h)
              << " base_coverage=" << double(cb) / double(w * h)
              << " lifted_coverage=" << double(cl) / double(w * h) << "\n";
    if (!(cd >= cb && cb >= cl)) throw std::runtime_error("shadow slider response violated");
    if (cd == 0) throw std::runtime_error("darkened shadow zebra empty");
    if (cl >= cb) throw std::runtime_error("lifting did not shrink the zebra");
    std::cout << "SHADOW_SLIDER_LINKAGE_PASS\n";
}
}  // namespace

int main(int argc, char** argv) try {
    if (argc != 6) {
        std::cerr << "usage: " << argv[0] << " raw.spv focus.spv false.spv shadow.spv combined.spv\n";
        return 2;
    }
    auto raw = loadSpv(argv[1]), focus = loadSpv(argv[2]), fc = loadSpv(argv[3]), shadow = loadSpv(argv[4]),
         combined = loadSpv(argv[5]);
    Vulkan v = createVulkan();
    MonitoringOverlaysCreateInfo ci{};
    ci.context = {v.pd, v.device, nullptr};
    ci.maxFramesInFlight = 2;
    ci.rawStateShader = {raw.data(), raw.size() * 4};
    ci.focusPeakingShader = {focus.data(), focus.size() * 4};
    ci.falseColorShader = {fc.data(), fc.size() * 4};
    ci.tonemapShadowShader = {shadow.data(), shadow.size() * 4};
    ci.combinedShader = {combined.data(), combined.size() * 4};
    MonitoringOverlays proc(ci);
    TonemapShadowParams neutral{};
    validateParity(v, proc, neutral, "neutral");
    TonemapShadowParams lifted{};
    lifted.shadowsUI = 60.0f;
    lifted.blacksUI = -40.0f;
    validateParity(v, proc, lifted, "sliders");
    validateSliderLinkage(v, proc);
    std::cout << "TONEMAP_SHADOW_VULKAN_VALIDATION_PASS\n";
    return 0;
} catch (const std::exception& e) {
    std::cerr << "TONEMAP_SHADOW_VULKAN_VALIDATION_FAIL: " << e.what() << "\n";
    return 1;
}
