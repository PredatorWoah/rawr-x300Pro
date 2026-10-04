
#include <vulkan/vulkan.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <iostream>
#include <limits>
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
bool hasInstanceExt(const char* name) {
    uint32_t n = 0;
    vkEnumerateInstanceExtensionProperties(nullptr, &n, nullptr);
    std::vector<VkExtensionProperties> p(n);
    vkEnumerateInstanceExtensionProperties(nullptr, &n, p.data());
    for (auto& e : p)
        if (std::strcmp(e.extensionName, name) == 0) return true;
    return false;
}
[[maybe_unused]] bool hasDeviceExt(VkPhysicalDevice pd, const char* name) {
    uint32_t n = 0;
    vkEnumerateDeviceExtensionProperties(pd, nullptr, &n, nullptr);
    std::vector<VkExtensionProperties> p(n);
    vkEnumerateDeviceExtensionProperties(pd, nullptr, &n, p.data());
    for (auto& e : p)
        if (std::strcmp(e.extensionName, name) == 0) return true;
    return false;
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
    VkDeviceSize size = 0;
    Buffer() = default;
    Buffer(const Buffer&) = delete;
    Buffer& operator=(const Buffer&) = delete;
    Buffer(Buffer&& o) noexcept { *this = std::move(o); }
    Buffer& operator=(Buffer&& o) noexcept {
        if (this != &o) {
            destroy();
            d = o.d;
            b = o.b;
            m = o.m;
            size = o.size;
            o.b = VK_NULL_HANDLE;
            o.m = VK_NULL_HANDLE;
        }
        return *this;
    }
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
    x.size = size;
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
    Image() = default;
    Image(const Image&) = delete;
    Image& operator=(const Image&) = delete;
    Image(Image&& o) noexcept { *this = std::move(o); }
    Image& operator=(Image&& o) noexcept {
        if (this != &o) {
            destroy();
            d = o.d;
            image = o.image;
            mem = o.mem;
            view = o.view;
            o.image = VK_NULL_HANDLE;
            o.mem = VK_NULL_HANDLE;
            o.view = VK_NULL_HANDLE;
        }
        return *this;
    }
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
void barrier(VkCommandBuffer cb, VkImage img, VkImageLayout oldL, VkImageLayout newL, VkAccessFlags srcA,
             VkAccessFlags dstA, VkPipelineStageFlags srcS, VkPipelineStageFlags dstS) {
    VkImageMemoryBarrier b{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
    b.oldLayout = oldL;
    b.newLayout = newL;
    b.srcAccessMask = srcA;
    b.dstAccessMask = dstA;
    b.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    b.image = img;
    b.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    b.subresourceRange.levelCount = 1;
    b.subresourceRange.layerCount = 1;
    vkCmdPipelineBarrier(cb, srcS, dstS, 0, 0, nullptr, 0, nullptr, 1, &b);
}
struct Vulkan {
    VkInstance instance = VK_NULL_HANDLE;
    VkPhysicalDevice pd = VK_NULL_HANDLE;
    VkDevice device = VK_NULL_HANDLE;
    uint32_t qf = 0;
    VkQueue queue = VK_NULL_HANDLE;
    VkCommandPool pool = VK_NULL_HANDLE;
    Vulkan() = default;
    Vulkan(const Vulkan&) = delete;
    Vulkan& operator=(const Vulkan&) = delete;
    Vulkan(Vulkan&& o) noexcept { *this = std::move(o); }
    Vulkan& operator=(Vulkan&& o) noexcept {
        if (this != &o) {
            instance = o.instance;
            pd = o.pd;
            device = o.device;
            qf = o.qf;
            queue = o.queue;
            pool = o.pool;
            o.instance = VK_NULL_HANDLE;
            o.pd = VK_NULL_HANDLE;
            o.device = VK_NULL_HANDLE;
            o.queue = VK_NULL_HANDLE;
            o.pool = VK_NULL_HANDLE;
        }
        return *this;
    }
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
    if (hasInstanceExt(VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME)) {
        ie.push_back(VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME);
        flags |= VK_INSTANCE_CREATE_ENUMERATE_PORTABILITY_BIT_KHR;
    }
#endif
    VkApplicationInfo ai{VK_STRUCTURE_TYPE_APPLICATION_INFO};
    ai.pApplicationName = "monitoring_overlays_remaining_validation";
    ai.apiVersion = VK_API_VERSION_1_1;
    VkInstanceCreateInfo ici{VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};
    ici.flags = flags;
    ici.pApplicationInfo = &ai;
    ici.enabledExtensionCount = uint32_t(ie.size());
    ici.ppEnabledExtensionNames = ie.data();
    check(vkCreateInstance(&ici, nullptr, &v.instance), "vkCreateInstance");
    uint32_t n = 0;
    check(vkEnumeratePhysicalDevices(v.instance, &n, nullptr), "vkEnumeratePhysicalDevices(count)");
    if (!n) throw std::runtime_error("no Vulkan physical device");
    std::vector<VkPhysicalDevice> d(n);
    check(vkEnumeratePhysicalDevices(v.instance, &n, d.data()), "vkEnumeratePhysicalDevices");
    v.pd = d[0];
    VkPhysicalDeviceProperties prop{};
    vkGetPhysicalDeviceProperties(v.pd, &prop);
    std::cout << "Using GPU: " << prop.deviceName << "\n";
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
    if (hasDeviceExt(v.pd, VK_KHR_PORTABILITY_SUBSET_EXTENSION_NAME))
        de.push_back(VK_KHR_PORTABILITY_SUBSET_EXTENSION_NAME);
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
VkCommandBuffer beginCB(Vulkan& v) {
    VkCommandBufferAllocateInfo ai{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
    ai.commandPool = v.pool;
    ai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    ai.commandBufferCount = 1;
    VkCommandBuffer cb{};
    check(vkAllocateCommandBuffers(v.device, &ai, &cb), "vkAllocateCommandBuffers");
    VkCommandBufferBeginInfo bi{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    check(vkBeginCommandBuffer(cb, &bi), "vkBeginCommandBuffer");
    return cb;
}
void submit(Vulkan& v, VkCommandBuffer cb) {
    check(vkEndCommandBuffer(cb), "vkEndCommandBuffer");
    VkSubmitInfo si{VK_STRUCTURE_TYPE_SUBMIT_INFO};
    si.commandBufferCount = 1;
    si.pCommandBuffers = &cb;
    check(vkQueueSubmit(v.queue, 1, &si, VK_NULL_HANDLE), "vkQueueSubmit");
    check(vkQueueWaitIdle(v.queue), "vkQueueWaitIdle");
    vkFreeCommandBuffers(v.device, v.pool, 1, &cb);
}
void writeBuffer(VkDevice d, Buffer& b, const void* data, size_t bytes) {
    void* m = nullptr;
    check(vkMapMemory(d, b.m, 0, VK_WHOLE_SIZE, 0, &m), "vkMapMemory(write)");
    std::memcpy(m, data, bytes);
    vkUnmapMemory(d, b.m);
}
std::vector<uint8_t> readBuffer(VkDevice d, Buffer& b, size_t bytes) {
    void* m = nullptr;
    check(vkMapMemory(d, b.m, 0, VK_WHOLE_SIZE, 0, &m), "vkMapMemory(read)");
    std::vector<uint8_t> out(bytes);
    std::memcpy(out.data(), m, bytes);
    vkUnmapMemory(d, b.m);
    return out;
}
void uploadImage(VkCommandBuffer cb, Buffer& src, Image& img, uint32_t w, uint32_t h) {
    barrier(cb, img.image, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 0,
            VK_ACCESS_TRANSFER_WRITE_BIT, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT);
    VkBufferImageCopy c{};
    c.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    c.imageSubresource.layerCount = 1;
    c.imageExtent = {w, h, 1};
    vkCmdCopyBufferToImage(cb, src.b, img.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &c);
    barrier(cb, img.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_GENERAL, VK_ACCESS_TRANSFER_WRITE_BIT,
            VK_ACCESS_SHADER_READ_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT);
}
void initOutput(VkCommandBuffer cb, Image& img) {
    barrier(cb, img.image, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL, 0,
            VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
            VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT);
}
void readbackImage(VkCommandBuffer cb, Image& img, Buffer& dst, uint32_t w, uint32_t h) {
    barrier(cb, img.image, VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_ACCESS_SHADER_WRITE_BIT,
            VK_ACCESS_TRANSFER_READ_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT);
    VkBufferImageCopy c{};
    c.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    c.imageSubresource.layerCount = 1;
    c.imageExtent = {w, h, 1};
    vkCmdCopyImageToBuffer(cb, img.image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, dst.b, 1, &c);
}

uint8_t q8(float v) { return uint8_t(std::lround(std::clamp(v, 0.0f, 1.0f) * 255.0f)); }
float u8f(uint8_t v) { return float(v) * (1.0f / 255.0f); }

struct FocusFixture {
    uint32_t w = 0, h = 0;
    std::vector<uint8_t> rgba8;
};
FocusFixture makeFocus(uint32_t w, uint32_t h, int kind, float blurSigma = 0, float brightness = 1.0f,
                       float noiseAmp = 0.0f) {
    FocusFixture f;
    f.w = w;
    f.h = h;
    f.rgba8.resize(size_t(w) * h * 4);
    uint32_t rng = 0x12345678u;
    auto rnd = [&]() {
        rng = 1664525u * rng + 1013904223u;
        return (float((rng >> 8) & 0xffffffu) / 16777215.0f) * 2.0f - 1.0f;
    };
    const float cx = float(w - 1) * 0.5f, cy = float(h - 1) * 0.5f, invSqrt2 = 0.70710678118f;
    for (uint32_t y = 0; y < h; y++)
        for (uint32_t x = 0; x < w; x++) {
            float base = 0.0f;
            if (kind == 0) {  // vertical edge
                float d = float(x) - cx;
                base = blurSigma <= 0 ? (d >= 0 ? 0.9f : 0.1f)
                                      : (0.1f + 0.8f * 0.5f * (1.0f + std::erf(d * invSqrt2 / blurSigma)));
            } else if (kind == 1) {  // horizontal
                float d = float(y) - cy;
                base = d >= 0 ? 0.9f : 0.1f;
            } else if (kind == 2) {  // diagonal
                float d = (float(x) - cx) + (float(y) - cy);
                base = d >= 0 ? 0.9f : 0.1f;
            } else if (kind == 3) {  // fine checkerboard
                base = ((x / 2 + y / 2) & 1u) ? 0.85f : 0.15f;
            } else {  // flat
                base = 0.40f;
            }
            base = std::clamp(base * brightness + noiseAmp * rnd(), 0.0f, 1.0f);
            uint8_t code = uint8_t(std::lround(base * 255.0f));
            size_t i = (size_t(y) * w + x) * 4;
            f.rgba8[i] = code;
            f.rgba8[i + 1] = code;
            f.rgba8[i + 2] = code;
            f.rgba8[i + 3] = 255;
        }
    return f;
}
std::array<float, 9> focusNeighborhood(const FocusFixture& f, uint32_t x, uint32_t y) {
    std::array<float, 9> n{};
    int k = 0;
    for (int yy = -1; yy <= 1; yy++)
        for (int xx = -1; xx <= 1; xx++) {
            int px = std::clamp(int(x) + xx, 0, int(f.w) - 1), py = std::clamp(int(y) + yy, 0, int(f.h) - 1);
            size_t i = (size_t(py) * f.w + uint32_t(px)) * 4;
            float r = u8f(f.rgba8[i]), g = u8f(f.rgba8[i + 1]), b = u8f(f.rgba8[i + 2]);
            n[k++] = 0.2126f * r + 0.7152f * g + 0.0722f * b;
        }
    return n;
}
struct FocusResult {
    std::vector<uint8_t> rgba;
    double meanAlpha = 0, coverage = 0;
};
FocusResult runFocus(Vulkan& v, MonitoringOverlays& proc, const FocusFixture& f, const FocusPeakingParams& p,
                     uint32_t slot = 0) {
    size_t pixels = size_t(f.w) * f.h;
    Buffer up = makeBuffer(v.pd, v.device, pixels * 4, VK_BUFFER_USAGE_TRANSFER_SRC_BIT),
           rb = makeBuffer(v.pd, v.device, pixels * 4, VK_BUFFER_USAGE_TRANSFER_DST_BIT);
    writeBuffer(v.device, up, f.rgba8.data(), pixels * 4);
    Image in = makeImage(v.pd, v.device, f.w, f.h, VK_FORMAT_R8G8B8A8_UNORM,
                         VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT);
    Image out = makeImage(v.pd, v.device, f.w, f.h, VK_FORMAT_R8G8B8A8_UNORM,
                          VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT);
    VkCommandBuffer cb = beginCB(v);
    uploadImage(cb, up, in, f.w, f.h);
    initOutput(cb, out);
    FocusPeakingRecordInfo ri{};
    ri.commandBuffer = cb;
    ri.input = {in.view, VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_LAYOUT_GENERAL, f.w, f.h};
    ri.output = {out.view, VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_LAYOUT_GENERAL, f.w, f.h};
    ri.params = p;
    ri.frameSlot = slot;
    proc.recordFocusPeaking(ri);
    readbackImage(cb, out, rb, f.w, f.h);
    submit(v, cb);
    FocusResult rr;
    rr.rgba = readBuffer(v.device, rb, pixels * 4);
    size_t active = 0;
    double sum = 0;
    for (size_t i = 0; i < pixels; i++) {
        uint8_t a = rr.rgba[4 * i + 3];
        sum += a;
        if (a) active++;
    }
    rr.meanAlpha = sum / (255.0 * double(pixels));
    rr.coverage = double(active) / double(pixels);
    return rr;
}
void validateFocusParity(Vulkan& v, MonitoringOverlays& proc) {
    FocusPeakingParams p{};
    p.sensitivity = 0.56f;
    p.color = {1.0f, 0.0f, 1.0f, 0.83f};
    for (auto dims :
         std::array<std::pair<uint32_t, uint32_t>, 4>{{{63, 65}, {2040, 1532}, {2040, 1536}, {2048, 1536}}}) {
        const uint32_t w = dims.first, h = dims.second;
        auto f = makeFocus(w, h, 3, 0, 0.45f, 0.0f);
        // Explicit right/bottom edge structure exercises clamp-to-edge halo reads and tail groups.
        auto setEdge = [&](uint32_t x, uint32_t y, float v) {
            uint8_t code = uint8_t(std::lround(std::clamp(v, 0.0f, 1.0f) * 255.0f));
            size_t q = (size_t(y) * w + x) * 4;
            f.rgba8[q] = code;
            f.rgba8[q + 1] = code;
            f.rgba8[q + 2] = code;
            f.rgba8[q + 3] = 255;
        };
        for (uint32_t y = 0; y < h; y++) {
            setEdge(w - 1, y, (y & 1u) ? 0.92f : 0.08f);
            if (w > 1) setEdge(w - 2, y, (y & 1u) ? 0.08f : 0.92f);
        }
        for (uint32_t x = 0; x < w; x++) {
            setEdge(x, h - 1, (x & 1u) ? 0.88f : 0.12f);
            if (h > 1) setEdge(x, h - 2, (x & 1u) ? 0.12f : 0.88f);
        }
        auto got = runFocus(v, proc, f, p);
        // Dilation-aware CPU reference: per-pixel alphas then 3x3 max.
        size_t pixels = size_t(w) * h;
        std::vector<float> cpuA(pixels), cpuD(pixels);
        for (uint32_t y = 0; y < h; y++)
            for (uint32_t x = 0; x < w; x++)
                cpuA[size_t(y) * w + x] =
                    cpu_reference::focusAlphaFromScore(cpu_reference::focusScore3x3(focusNeighborhood(f, x, y), p),
                                                      p);
        cpu_reference::dilateFocusAlpha(cpuA.data(), cpuD.data(), w, h);
        size_t bad = 0;
        int maxDiff = 0;
        size_t first = 0;
        for (uint32_t y = 0; y < h; y++)
            for (uint32_t x = 0; x < w; x++) {
                size_t pix = size_t(y) * w + x;
                float a = cpuD[pix];
                cpu_reference::PremulRgba e{std::clamp(p.color.r, 0.0f, 1.0f) * a, std::clamp(p.color.g, 0.0f, 1.0f) * a,
                             std::clamp(p.color.b, 0.0f, 1.0f) * a, a};
                uint8_t eb[4] = {q8(e.r), q8(e.g), q8(e.b), q8(e.a)};
                bool mismatch = false;
                for (int k = 0; k < 4; k++) {
                    int d = std::abs(int(got.rgba[4 * pix + k]) - int(eb[k]));
                    maxDiff = std::max(maxDiff, d);
                    if (d > 2) mismatch = true;
                }
                if (mismatch) {
                    if (!bad) first = pix;
                    bad++;
                }
            }
        if (bad)
            throw std::runtime_error("FOCUS GPU/CPU parity failed " + std::to_string(w) + "x" + std::to_string(h) +
                                     " bad=" + std::to_string(bad) + " maxDiff=" + std::to_string(maxDiff) +
                                     " first=" + std::to_string(first));
        std::cout << "FOCUS_GPU_PARITY_PASS " << w << "x" << h << " tolerance_lsb=2 max_diff=" << maxDiff
                  << " dilated=3x3 right_bottom_edges=EXERCISED\n";
    }
}
void validateFocusBehavior(Vulkan& v, MonitoringOverlays& proc) {
    FocusPeakingParams p{};
    p.sensitivity = 0.55f;
    std::array<float, 5> sig{{0.0f, 0.5f, 1.0f, 2.0f, 4.0f}};
    std::array<double, 5> m{};
    std::array<double, 5> peakScore{};
    std::array<double, 5> conc{};
    for (size_t i = 0; i < sig.size(); i++) {
        auto f = makeFocus(512, 256, 0, sig[i]);
        double ps = 0.0;
        for (uint32_t y = 0; y < f.h; y++)
            for (uint32_t x = 0; x < f.w; x++)
                ps = std::max(ps, double(cpu_reference::focusScore3x3(focusNeighborhood(f, x, y), p)));
        peakScore[i] = ps;
        auto r = runFocus(v, proc, f, p);
        m[i] = r.meanAlpha;
        // Edge concentration: fraction of active response within +/-4px of the
        // true edge center. Sharp edges concentrate; blur spreads (dilated).
        const double cx = (double(f.w) - 1.0) * 0.5;
        size_t inBand = 0, total = 0;
        for (uint32_t y = 0; y < f.h; y++)
            for (uint32_t x = 0; x < f.w; x++) {
                size_t pix = size_t(y) * f.w + x;
                if (r.rgba[4 * pix + 3] == 0) continue;
                total++;
                if (std::fabs(double(x) - cx) <= 4.0) inBand++;
            }
        conc[i] = total ? double(inBand) / double(total) : 0.0;
        std::cout << "FOCUS_BLUR sigma=" << sig[i] << " peak_score=" << peakScore[i] << " mean_alpha=" << m[i]
                  << " coverage=" << r.coverage << " concentration=" << conc[i] << "\n";
    }
    for (size_t i = 1; i < peakScore.size(); i++)
        if (!(peakScore[i] < peakScore[i - 1])) throw std::runtime_error("focus raw-score blur ordering violated");
    for (size_t i = 1; i < conc.size(); i++)
        if (conc[i] > conc[i - 1] + 1e-6) throw std::runtime_error("focus concentration blur ordering violated");
    if (!(m[0] > 0.0)) throw std::runtime_error("sharp edge produced no focus response");
    if (!(m[0] > m[4] + 1e-5)) throw std::runtime_error("focus blur discrimination too weak");
    auto noise = runFocus(v, proc, makeFocus(512, 256, 4, 0, 1.0f, 0.004f), p);
    std::cout << "FOCUS_DARK_NOISE coverage=" << noise.coverage << " mean_alpha=" << noise.meanAlpha << "\n";
    if (noise.coverage > 0.05) throw std::runtime_error("focus dark-noise false positives excessive");
    for (int kind = 0; kind < 4; kind++) {
        auto r = runFocus(v, proc, makeFocus(384, 256, kind), p);
        std::cout << "FOCUS_ORIENTATION kind=" << kind << " coverage=" << r.coverage << " mean_alpha=" << r.meanAlpha
                  << "\n";
        if (r.coverage <= 0.0) throw std::runtime_error("focus orientation/detail fixture produced no response");
    }
    std::array<float, 3> b{{0.25f, 0.5f, 1.0f}};
    for (float br : b) {
        auto r = runFocus(v, proc, makeFocus(384, 256, 0, 0, br), p);
        std::cout << "FOCUS_BRIGHTNESS scale=" << br << " coverage=" << r.coverage << " mean_alpha=" << r.meanAlpha
                  << "\n";
        if (r.coverage <= 0.0) throw std::runtime_error("focus brightness fixture produced no response");
    }
    std::cout << "FOCUS_BEHAVIORAL_VALIDATION_PASS\n";
}

FalseColorParams exactFalseParams() {
    FalseColorParams p{};
    p.enabled = true;
    p.rangeCount = 5;
    p.ranges[0] = {0.0f, 10.0f, {1, 0, 0, 1}};
    p.ranges[1] = {10.0f, 40.1f, {0, 1, 0, 1}};
    p.ranges[2] = {40.1f, 60.1f, {0, 0, 1, 1}};
    p.ranges[3] = {60.1f, 90.0f, {1, 1, 0, 1}};
    p.ranges[4] = {90.0f, 100.001f, {1, 0, 1, 1}};
    return p;
}
std::vector<uint8_t> makeDisplay(uint32_t w, uint32_t h) {
    std::vector<uint8_t> x(size_t(w) * h * 4);
    uint32_t s = 0x91e10da5u;
    for (size_t i = 0; i < size_t(w) * h; i++) {
        s = 1664525u * s + 1013904223u;
        x[4 * i] = uint8_t(s >> 24);
        s = 1664525u * s + 1013904223u;
        x[4 * i + 1] = uint8_t(s >> 24);
        s = 1664525u * s + 1013904223u;
        x[4 * i + 2] = uint8_t(s >> 24);
        x[4 * i + 3] = 255;
    }
    // Force exact grayscale codes repeatedly so configured boundary neighborhoods are exercised.
    for (uint32_t code = 0; code < 256 && code < size_t(w) * h; code++) {
        x[4 * code] = x[4 * code + 1] = x[4 * code + 2] = uint8_t(code);
    }
    return x;
}
std::vector<uint8_t> runFalse(Vulkan& v, MonitoringOverlays& proc, uint32_t w, uint32_t h,
                              const std::vector<uint8_t>& display, const FalseColorParams& p, uint32_t slot = 0) {
    size_t bytes = size_t(w) * h * 4;
    Buffer up = makeBuffer(v.pd, v.device, bytes, VK_BUFFER_USAGE_TRANSFER_SRC_BIT),
           rb = makeBuffer(v.pd, v.device, bytes, VK_BUFFER_USAGE_TRANSFER_DST_BIT);
    writeBuffer(v.device, up, display.data(), bytes);
    Image in = makeImage(v.pd, v.device, w, h, VK_FORMAT_R8G8B8A8_UNORM,
                         VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT),
          out = makeImage(v.pd, v.device, w, h, VK_FORMAT_R8G8B8A8_UNORM,
                          VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT);
    auto cb = beginCB(v);
    uploadImage(cb, up, in, w, h);
    initOutput(cb, out);
    FalseColorRecordInfo ri{};
    ri.commandBuffer = cb;
    ri.input = {in.view, VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_LAYOUT_GENERAL, w, h};
    ri.output = {out.view, VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_LAYOUT_GENERAL, w, h};
    ri.params = p;
    ri.frameSlot = slot;
    proc.recordFalseColor(ri);
    readbackImage(cb, out, rb, w, h);
    submit(v, cb);
    return readBuffer(v.device, rb, bytes);
}
void validateFalseColor(Vulkan& v, MonitoringOverlays& proc) {
    auto p = exactFalseParams();
    for (auto [w, h] : std::array<std::pair<uint32_t, uint32_t>, 5>{
             {{63, 65}, {2039, 1531}, {2040, 1532}, {2040, 1536}, {2048, 1536}}}) {
        auto in = makeDisplay(w, h);
        // Make right/bottom boundaries classify nontrivially and differently.
        for (uint32_t y = 0; y < h; y++) {
            size_t q = (size_t(y) * w + (w - 1)) * 4;
            in[q] = in[q + 1] = in[q + 2] = uint8_t((37u + y) % 256u);
            in[q + 3] = 255;
        }
        for (uint32_t x = 0; x < w; x++) {
            size_t q = (size_t(h - 1) * w + x) * 4;
            in[q] = in[q + 1] = in[q + 2] = uint8_t((211u + x) % 256u);
            in[q + 3] = 255;
        }
        auto got = runFalse(v, proc, w, h, in, p);
        size_t bad = 0, first = 0;
        int maxDiff = 0;
        for (size_t i = 0; i < size_t(w) * h; i++) {
            auto e = cpu_reference::falseColorOverlay(u8f(in[4 * i]), u8f(in[4 * i + 1]), u8f(in[4 * i + 2]), p);
            uint8_t eb[4] = {q8(e.r), q8(e.g), q8(e.b), q8(e.a)};
            bool mm = false;
            for (int k = 0; k < 4; k++) {
                int d = std::abs(int(got[4 * i + k]) - int(eb[k]));
                maxDiff = std::max(maxDiff, d);
                if (d > 0) mm = true;
            }
            if (mm) {
                if (!bad) first = i;
                bad++;
            }
        }
        if (bad)
            throw std::runtime_error("FALSE_COLOR GPU parity failed " + std::to_string(w) + "x" + std::to_string(h) +
                                     " bad=" + std::to_string(bad) + " first=" + std::to_string(first) +
                                     " maxDiff=" + std::to_string(maxDiff));
        std::cout << "FALSE_COLOR_GPU_PARITY_PASS " << w << "x" << h << " exact_rgba8 right_bottom_edges=EXERCISED\n";
    }
    // Custom palette/ranges prove runtime config is data, not baked shader semantics.
    FalseColorParams custom{};
    custom.enabled = true;
    custom.rangeCount = 3;
    custom.ranges[0] = {0, 33, {0, 1, 1, 1}};
    custom.ranges[1] = {33, 66, {1, 0, 1, 1}};
    custom.ranges[2] = {66, 100.001f, {1, 1, 1, 1}};
    auto in = makeDisplay(257, 129), got = runFalse(v, proc, 257, 129, in, custom);
    size_t bad = 0;
    for (size_t i = 0; i < size_t(257) * 129; i++) {
        auto e = cpu_reference::falseColorOverlay(u8f(in[4 * i]), u8f(in[4 * i + 1]), u8f(in[4 * i + 2]), custom);
        uint8_t eb[4] = {q8(e.r), q8(e.g), q8(e.b), q8(e.a)};
        for (int k = 0; k < 4; k++)
            if (got[4 * i + k] != eb[k]) {
                bad++;
                break;
            }
    }
    if (bad) throw std::runtime_error("FALSE_COLOR custom-config parity failed");
    std::cout << "FALSE_COLOR_CUSTOM_CONFIG_PASS\nFALSE_COLOR_VULKAN_VALIDATION_PASS\n";
}

RawStateOverlayParams combinedRawParams() {
    RawStateOverlayParams p{};
    p.highlightSeverity = {{{1, 0, 0, 0.85f}, {1, 0.5f, 0, 0.9f}, {1, 1, 0, 0.95f}}};
    p.shadowWarningEnabled = false;
    p.shadowClippedEnabled = false;
    return p;
}
TonemapShadowParams combinedShadowParams() {
    TonemapShadowParams p{};
    p.enabled = true;
    p.shadowsUI = 40.0f;
    p.blacksUI = 0.0f;
    p.thresholdIre = 10.0f;
    p.stripePeriod = 8.0f;
    return p;
}
uint16_t rawWord(uint32_t x, uint32_t y) {
    uint16_t a = uint16_t((x + 3 * y) & 15u), b = uint16_t((5 * x + y + 7) & 15u),
             c = uint16_t((11 * x + 13 * y + 3) & 15u);
    return ((x + y) % 5 == 0) ? uint16_t(a | (b << 8) | (c << 12))
                              : uint16_t(((x + y) % 3 == 0) ? a : ((x + y) % 3 == 1 ? b << 8 : c << 12));
}
struct CombinedInputs {
    uint32_t w, h;
    std::vector<uint16_t> raw;
    std::vector<uint8_t> display;
};
CombinedInputs makeCombined(uint32_t w, uint32_t h) {
    CombinedInputs x{};
    x.w = w;
    x.h = h;
    x.raw.resize(size_t(w) * h);
    for (uint32_t y = 0; y < h; y++)
        for (uint32_t xx = 0; xx < w; xx++) x.raw[size_t(y) * w + xx] = rawWord(xx, y);
    // Display carries focus detail (checker), false-color ramp coverage, and
    // dark shadow regions for the zebra.
    auto focusPart = makeFocus(w, h, 3, 0, 0.55f, 0.0f);
    x.display = makeDisplay(w, h);
    for (size_t i = 0; i < size_t(w) * h; i++) {
        // Blend checker detail into the display ramp so focus has strong edges.
        for (int k = 0; k < 3; k++)
            x.display[4 * i + k] = uint8_t((uint32_t(x.display[4 * i + k]) + uint32_t(focusPart.rgba8[4 * i])) / 2);
    }
    // Force a dark band so the shadow zebra is nontrivial.
    for (uint32_t y = 0; y < h / 4; y++)
        for (uint32_t xx = 0; xx < w; xx++) {
            size_t q = (size_t(y) * w + xx) * 4;
            x.display[q] = x.display[q + 1] = x.display[q + 2] = 8;
            x.display[q + 3] = 255;
        }

    // Force all source domains to be nontrivial on the exact right/bottom
    // boundaries so a dropped tail invocation cannot accidentally pass.
    for (uint32_t y = 0; y < h; y++) x.raw[size_t(y) * w + (w - 1)] = uint16_t((y & 1u) ? 0x000Fu : 0x000Bu);
    for (uint32_t xx = 0; xx < w; xx++) x.raw[size_t(h - 1) * w + xx] = uint16_t((xx & 1u) ? 0x000Fu : 0x0003u);

    for (uint32_t y = 0; y < h; y++) {
        size_t q = (size_t(y) * w + (w - 1)) * 4;
        x.display[q] = x.display[q + 1] = x.display[q + 2] = uint8_t((41u + y) % 256u);
        x.display[q + 3] = 255;
    }
    for (uint32_t xx = 0; xx < w; xx++) {
        size_t q = (size_t(h - 1) * w + xx) * 4;
        x.display[q] = x.display[q + 1] = x.display[q + 2] = uint8_t((197u + xx) % 256u);
        x.display[q + 3] = 255;
    }
    return x;
}
std::vector<uint8_t> runCombinedSequence(Vulkan& v, MonitoringOverlays& proc, const CombinedInputs& x,
                                         const RawStateOverlayParams& rpIn, const FocusPeakingParams& fpIn,
                                         const FalseColorParams& fcIn, const TonemapShadowParams& shIn, bool combined,
                                         uint32_t slot, bool rawOn, bool focusOn, bool falseOn, bool shadowOn) {
    size_t pix = size_t(x.w) * x.h;
    Buffer ur = makeBuffer(v.pd, v.device, pix * 2, VK_BUFFER_USAGE_TRANSFER_SRC_BIT),
           ud = makeBuffer(v.pd, v.device, pix * 4, VK_BUFFER_USAGE_TRANSFER_SRC_BIT),
           rb = makeBuffer(v.pd, v.device, pix * 4, VK_BUFFER_USAGE_TRANSFER_DST_BIT);
    writeBuffer(v.device, ur, x.raw.data(), pix * 2);
    writeBuffer(v.device, ud, x.display.data(), pix * 4);
    Image ir = makeImage(v.pd, v.device, x.w, x.h, VK_FORMAT_R16_UINT,
                         VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT),
          id = makeImage(v.pd, v.device, x.w, x.h, VK_FORMAT_R8G8B8A8_UNORM,
                         VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT),
          out = makeImage(v.pd, v.device, x.w, x.h, VK_FORMAT_R8G8B8A8_UNORM,
                          VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT);
    auto cb = beginCB(v);
    uploadImage(cb, ur, ir, x.w, x.h);
    uploadImage(cb, ud, id, x.w, x.h);
    initOutput(cb, out);
    RawStateImageView rv{ir.view, VK_FORMAT_R16_UINT, VK_IMAGE_LAYOUT_GENERAL, x.w, x.h};
    DisplayImageView dv{id.view, VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_LAYOUT_GENERAL, x.w, x.h};
    OverlayImageView ov{out.view, VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_LAYOUT_GENERAL, x.w, x.h};
    auto rp = rpIn;
    auto fp = fpIn;
    auto fc = fcIn;
    auto sh = shIn;
    if (!rawOn) {
        rp.highlightEnabled = false;
    }
    rp.shadowWarningEnabled = false;
    rp.shadowClippedEnabled = false;
    fp.enabled = focusOn;
    fc.enabled = falseOn;
    sh.enabled = shadowOn;
    if (combined) {
        CombinedRecordInfo r{};
        r.commandBuffer = cb;
        // Deliberately leave disabled-mode views null. This is the public API safety gate.
        if (rawOn) r.rawState = rv;
        if (focusOn || falseOn || shadowOn) r.display = dv;
        r.output = ov;
        r.rawParams = rp;
        r.focusParams = fp;
        r.falseColorParams = fc;
        r.tonemapShadowParams = sh;
        r.frameSlot = slot;
        proc.recordCombined(r);
    } else {
        bool have = false;
        if (falseOn) {
            FalseColorRecordInfo a{};
            a.commandBuffer = cb;
            a.input = dv;
            a.output = ov;
            a.params = fc;
            a.frameSlot = slot;
            a.composeOverExisting = false;
            proc.recordFalseColor(a);
            have = true;
        }
        if (focusOn) {
            FocusPeakingRecordInfo b{};
            b.commandBuffer = cb;
            b.input = dv;
            b.output = ov;
            b.params = fp;
            b.frameSlot = slot;
            b.composeOverExisting = have;
            proc.recordFocusPeaking(b);
            have = true;
        }
        if (shadowOn) {
            TonemapShadowRecordInfo s{};
            s.commandBuffer = cb;
            s.input = dv;
            s.output = ov;
            s.params = sh;
            s.frameSlot = slot;
            s.composeOverExisting = have;
            proc.recordTonemapShadow(s);
            have = true;
        }
        if (rawOn) {
            RawStateRecordInfo c{};
            c.commandBuffer = cb;
            c.input = rv;
            c.output = ov;
            c.params = rp;
            c.frameSlot = slot;
            c.composeOverExisting = have;
            proc.recordRawStateOverlay(c);
        }
    }
    readbackImage(cb, out, rb, x.w, x.h);
    submit(v, cb);
    return readBuffer(v.device, rb, pix * 4);
}
void validateCombinedEquivalence(Vulkan& v, MonitoringOverlays& proc) {
    auto rp = combinedRawParams();
    FocusPeakingParams fp{};
    fp.sensitivity = 0.57f;
    fp.color = {0, 1, 0, 0.77f};
    auto fc = FalseColorParams::defaultPreset();
    auto sh = combinedShadowParams();
    struct Combo {
        const char* name;
        bool raw, focus, fc, shadow;
    };
    const std::array<Combo, 5> combos{{{"false+focus", false, true, true, false},
                                       {"false+shadow", false, false, true, true},
                                       {"focus+shadow", false, true, false, true},
                                       {"focus+raw", true, true, false, false},
                                       {"all-four", true, true, true, true}}};
    const std::array<std::pair<uint32_t, uint32_t>, 5> dims{
        {{63, 65}, {641, 479}, {2040, 1532}, {2040, 1536}, {2048, 1536}}};
    for (const auto& combo : combos)
        for (auto [w, h] : dims) {
            auto x = makeCombined(w, h);
            auto a =
                runCombinedSequence(v, proc, x, rp, fp, fc, sh, true, 0, combo.raw, combo.focus, combo.fc,
                                    combo.shadow);
            auto b =
                runCombinedSequence(v, proc, x, rp, fp, fc, sh, false, 1, combo.raw, combo.focus, combo.fc,
                                    combo.shadow);
            size_t bad = 0, first = 0;
            int maxDiff = 0;
            for (size_t i = 0; i < a.size(); i++) {
                int d = std::abs(int(a[i]) - int(b[i]));
                maxDiff = std::max(maxDiff, d);
                if (d) {
                    if (!bad) first = i;
                    bad++;
                }
            }
            if (bad) {
                size_t pix = first / 4, ch = first % 4;
                throw std::runtime_error(
                    std::string("COMBINED changed standalone semantics/output combo=") + combo.name + " " +
                    std::to_string(w) + "x" + std::to_string(h) + " differing_bytes=" + std::to_string(bad) +
                    " first_byte=" + std::to_string(first) + " pixel=" + std::to_string(pix) +
                    " channel=" + std::to_string(ch) + " got=" + std::to_string(int(a[first])) +
                    " expected=" + std::to_string(int(b[first])) + " maxDiff=" + std::to_string(maxDiff));
            }
            std::cout << "COMBINED_STANDALONE_EQUIVALENCE_PASS combo=" << combo.name << " " << w << "x" << h
                      << " byte_exact full_frame_and_edges=VERIFIED\n";
        }
    std::cout << "COMBINED_VULKAN_VALIDATION_PASS\n";
    std::cout << "PRODUCTION_GEOMETRY_COMBINED_PASS 2048x1536 2040x1532 2040x1536 "
                 "all_pairwise_and_all_four=BYTE_EXACT edges=VERIFIED\n";
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
    VkFormatProperties f8{}, r16{};
    vkGetPhysicalDeviceFormatProperties(v.pd, VK_FORMAT_R8G8B8A8_UNORM, &f8);
    vkGetPhysicalDeviceFormatProperties(v.pd, VK_FORMAT_R16_UINT, &r16);
    if (!(f8.optimalTilingFeatures & VK_FORMAT_FEATURE_STORAGE_IMAGE_BIT))
        throw std::runtime_error("RGBA8 storage image unsupported");
    if (!(r16.optimalTilingFeatures & VK_FORMAT_FEATURE_STORAGE_IMAGE_BIT))
        throw std::runtime_error("R16_UINT storage image unsupported");
    MonitoringOverlaysCreateInfo ci{};
    if (ci.rawWorkgroupSizeX != 16 || ci.rawWorkgroupSizeY != 16 || ci.focusWorkgroupSizeX != 8 ||
        ci.focusWorkgroupSizeY != 8 || ci.falseColorWorkgroupSizeX != 16 || ci.falseColorWorkgroupSizeY != 16 ||
        ci.tonemapShadowWorkgroupSizeX != 16 || ci.tonemapShadowWorkgroupSizeY != 16 ||
        ci.combinedWorkgroupSizeX != 8 || ci.combinedWorkgroupSizeY != 8)
        throw std::runtime_error("workgroup default/shader ABI mismatch");
    ci.context = {v.pd, v.device, nullptr};
    ci.maxFramesInFlight = 2;
    ci.rawStateShader = {raw.data(), raw.size() * 4};
    ci.focusPeakingShader = {focus.data(), focus.size() * 4};
    ci.falseColorShader = {fc.data(), fc.size() * 4};
    ci.tonemapShadowShader = {shadow.data(), shadow.size() * 4};
    ci.combinedShader = {combined.data(), combined.size() * 4};
    MonitoringOverlays proc(ci);

    std::cout << "\n=== FOCUS PEAKING: standalone parity ===\n";
    validateFocusParity(v, proc);
    std::cout << "\n=== FOCUS PEAKING: behavioral quality ===\n";
    validateFocusBehavior(v, proc);
    std::cout << "\n=== FALSE COLOR: standalone parity ===\n";
    validateFalseColor(v, proc);
    std::cout << "\n=== COMBINED: equivalence to standalone sequence ===\n";
    validateCombinedEquivalence(v, proc);
    std::cout << "\nREMAINING_MODES_VULKAN_VALIDATION_PASS\n";
    return 0;
} catch (const std::exception& e) {
    std::cerr << "REMAINING_MODES_VULKAN_VALIDATION_FAIL: " << e.what() << "\n";
    return 1;
}
