
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
    ai.pApplicationName = "monitoring_overlays_visual_samples";
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

void writePPM(const std::string& path, uint32_t w, uint32_t h, const std::vector<uint8_t>& rgba) {
    std::ofstream f(path, std::ios::binary);
    if (!f) throw std::runtime_error("cannot write " + path);
    f << "P6\n" << w << " " << h << "\n255\n";
    for (size_t i = 0; i < size_t(w) * h; i++) {
        char rgb[3] = {char(rgba[4 * i]), char(rgba[4 * i + 1]), char(rgba[4 * i + 2])};
        f.write(rgb, 3);
    }
}
void writePAM(const std::string& path, uint32_t w, uint32_t h, const std::vector<uint8_t>& rgba) {
    std::ofstream f(path, std::ios::binary);
    if (!f) throw std::runtime_error("cannot write " + path);
    f << "P7\nWIDTH " << w << "\nHEIGHT " << h << "\nDEPTH 4\nMAXVAL 255\nTUPLTYPE RGB_ALPHA\nENDHDR\n";
    f.write(reinterpret_cast<const char*>(rgba.data()), std::streamsize(rgba.size()));
}
std::vector<uint8_t> composite(const std::vector<uint8_t>& base, const std::vector<uint8_t>& ov) {
    std::vector<uint8_t> out(base.size());
    for (size_t i = 0; i < base.size() / 4; i++) {
        float a = float(ov[4 * i + 3]) / 255.0f;
        for (int k = 0; k < 3; k++) {
            float top = float(ov[4 * i + k]) / 255.0f;
            float b = float(base[4 * i + k]) / 255.0f;
            float v = top + b * (1.0f - a);  // overlay is premultiplied
            out[4 * i + k] = uint8_t(std::lround(std::clamp(v, 0.0f, 1.0f) * 255.0f));
        }
        out[4 * i + 3] = 255;
    }
    return out;
}

struct Fixtures {
    uint32_t w = 960, h = 540;
    std::vector<uint16_t> raw;
    std::vector<uint8_t> display;
};
Fixtures makeFixtures() {
    Fixtures f;
    size_t n = size_t(f.w) * f.h;
    f.raw.resize(n);
    f.display.resize(n * 4);
    for (uint32_t y = 0; y < f.h; y++)
        for (uint32_t x = 0; x < f.w; x++) {
            size_t i = size_t(y) * f.w + x;
            float nx = float(x) / float(f.w - 1), ny = float(y) / float(f.h - 1);

            // Display fixture: horizontal grayscale ramp + colored patches plus
            // sharp focus structures (vertical bars, blurred edge, diagonal, checker).
            float r = nx, g = nx, b = nx;
            if (x < 240) {
                float v = (x % 32 < 16) ? 0.08f : 0.75f;
                r = g = b = v * (0.35f + 0.65f * ny);
            } else if (x < 480) {
                float d = (float(x) - 360.0f) / 14.0f;
                float v = 0.08f + 0.67f * 0.5f * (1.0f + std::erf(d * 0.70710678f));
                r = g = b = v * (0.35f + 0.65f * ny);
            } else if (x < 720) {
                float d = std::fabs((float(x) - 600.0f) - (float(y) - 270.0f));
                float v = d < 2.0f ? 0.85f : 0.10f;
                r = g = b = v;
            } else if (y > 360) {
                if (x < 720) {
                    r = 0.10f;
                    g = 0.25f;
                    b = 0.95f;
                } else {
                    float v = (((x / 4) + (y / 4)) & 1u) ? 0.70f : 0.12f;
                    r = g = b = v;
                }
            }
            // Dark top strip exercises the shadow zebra.
            if (y < 40) {
                r = g = b = 0.02f;
            }
            f.display[4 * i] = uint8_t(std::lround(std::clamp(r, 0.0f, 1.0f) * 255));
            f.display[4 * i + 1] = uint8_t(std::lround(std::clamp(g, 0.0f, 1.0f) * 255));
            f.display[4 * i + 2] = uint8_t(std::lround(std::clamp(b, 0.0f, 1.0f) * 255));
            f.display[4 * i + 3] = 255;

            // RAW state fixture: top bands show highlight severity.
            uint16_t word = 0;
            if (y < 120) {
                if (x < 240)
                    word = 0x0001;  // R clipped -> severity 1
                else if (x < 480)
                    word = 0x0003;  // R+G1 -> logical 2
                else
                    word = 0x000B;  // R+G1+B -> logical 3
            }
            f.raw[i] = word;
        }
    return f;
}

Fixtures makeSunFixtures() {
    Fixtures f;
    f.w = 960;
    f.h = 540;
    size_t n = size_t(f.w) * f.h;
    f.raw.resize(n);
    f.display.resize(n * 4);
    const float sunX = 690.0f, sunY = 125.0f;
    for (uint32_t y = 0; y < f.h; y++)
        for (uint32_t x = 0; x < f.w; x++) {
            size_t i = size_t(y) * f.w + x;
            float ny = float(y) / float(f.h - 1);
            float r = 0.18f + 0.22f * (1.0f - ny);
            float g = 0.35f + 0.30f * (1.0f - ny);
            float b = 0.62f + 0.30f * (1.0f - ny);
            float horizon = std::exp(-std::pow((ny - 0.68f) / 0.16f, 2.0f));
            r += 0.28f * horizon;
            g += 0.14f * horizon;
            b -= 0.10f * horizon;
            float dx = float(x) - sunX, dy = float(y) - sunY;
            float dist = std::sqrt(dx * dx + dy * dy);
            float halo = std::exp(-dist * dist / (2.0f * 85.0f * 85.0f));
            r += 0.55f * halo;
            g += 0.45f * halo;
            b += 0.18f * halo;
            if (dist < 32.0f) {
                r = 1.0f;
                g = 1.0f;
                b = 0.96f;
            }
            float cloud = 0.0f;
            cloud += std::exp(-std::pow((float(y) - 210.0f) / 18.0f, 2.0f)) *
                     std::exp(-std::pow((float(x) - 480.0f) / 220.0f, 2.0f));
            cloud += 0.8f * std::exp(-std::pow((float(y) - 270.0f) / 22.0f, 2.0f)) *
                     std::exp(-std::pow((float(x) - 720.0f) / 180.0f, 2.0f));
            r += 0.40f * cloud;
            g += 0.42f * cloud;
            b += 0.45f * cloud;
            float ridge = 390.0f + 28.0f * std::sin(float(x) * 0.018f) + 14.0f * std::sin(float(x) * 0.051f);
            if (float(y) > ridge) {
                r = 0.035f;
                g = 0.050f;
                b = 0.040f;
            }
            if ((x > 120 && x < 128 && y > 330) || (x > 300 && x < 306 && y > 350) || (x > 820 && x < 827 && y > 320)) {
                r = 0.10f;
                g = 0.12f;
                b = 0.08f;
            }
            r = std::clamp(r, 0.0f, 1.0f);
            g = std::clamp(g, 0.0f, 1.0f);
            b = std::clamp(b, 0.0f, 1.0f);
            f.display[4 * i] = uint8_t(std::lround(r * 255.0f));
            f.display[4 * i + 1] = uint8_t(std::lround(g * 255.0f));
            f.display[4 * i + 2] = uint8_t(std::lround(b * 255.0f));
            f.display[4 * i + 3] = 255;
            uint16_t word = 0;
            if (dist < 28.0f)
                word |= 0x000Fu;
            else if (dist < 46.0f) {
                if (x < uint32_t(sunX))
                    word |= 0x0003u;
                else
                    word |= 0x0008u;
            }
            if (cloud > 0.92f) word |= ((x / 32u) & 1u) ? 0x0003u : 0x0001u;
            f.raw[i] = word;
                word |= 0x0F00u;
            f.raw[i] = word;
        }
    return f;
}

std::vector<uint8_t> runRaw(Vulkan& v, MonitoringOverlays& proc, const Fixtures& f) {
    size_t n = size_t(f.w) * f.h;
    Buffer up = makeBuffer(v.pd, v.device, n * 2, VK_BUFFER_USAGE_TRANSFER_SRC_BIT),
           rb = makeBuffer(v.pd, v.device, n * 4, VK_BUFFER_USAGE_TRANSFER_DST_BIT);
    writeBuffer(v.device, up, f.raw.data(), n * 2);
    Image in = makeImage(v.pd, v.device, f.w, f.h, VK_FORMAT_R16_UINT,
                         VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT),
          out = makeImage(v.pd, v.device, f.w, f.h, VK_FORMAT_R8G8B8A8_UNORM,
                          VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT);
    auto cb = beginCB(v);
    uploadImage(cb, up, in, f.w, f.h);
    initOutput(cb, out);
    RawStateRecordInfo r{};
    r.commandBuffer = cb;
    r.input = {in.view, VK_FORMAT_R16_UINT, VK_IMAGE_LAYOUT_GENERAL, f.w, f.h};
    r.output = {out.view, VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_LAYOUT_GENERAL, f.w, f.h};
    proc.recordRawStateOverlay(r);
    readbackImage(cb, out, rb, f.w, f.h);
    submit(v, cb);
    return readBuffer(v.device, rb, n * 4);
}
std::vector<uint8_t> runFocus(Vulkan& v, MonitoringOverlays& proc, const Fixtures& f) {
    size_t n = size_t(f.w) * f.h;
    Buffer up = makeBuffer(v.pd, v.device, n * 4, VK_BUFFER_USAGE_TRANSFER_SRC_BIT),
           rb = makeBuffer(v.pd, v.device, n * 4, VK_BUFFER_USAGE_TRANSFER_DST_BIT);
    writeBuffer(v.device, up, f.display.data(), n * 4);
    Image in = makeImage(v.pd, v.device, f.w, f.h, VK_FORMAT_R8G8B8A8_UNORM,
                         VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT),
          out = makeImage(v.pd, v.device, f.w, f.h, VK_FORMAT_R8G8B8A8_UNORM,
                          VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT);
    auto cb = beginCB(v);
    uploadImage(cb, up, in, f.w, f.h);
    initOutput(cb, out);
    FocusPeakingRecordInfo r{};
    r.commandBuffer = cb;
    r.input = {in.view, VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_LAYOUT_GENERAL, f.w, f.h};
    r.output = {out.view, VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_LAYOUT_GENERAL, f.w, f.h};
    r.params.sensitivity = 0.55f;
    proc.recordFocusPeaking(r);
    readbackImage(cb, out, rb, f.w, f.h);
    submit(v, cb);
    return readBuffer(v.device, rb, n * 4);
}
std::vector<uint8_t> runFalse(Vulkan& v, MonitoringOverlays& proc, const Fixtures& f) {
    size_t n = size_t(f.w) * f.h;
    Buffer up = makeBuffer(v.pd, v.device, n * 4, VK_BUFFER_USAGE_TRANSFER_SRC_BIT),
           rb = makeBuffer(v.pd, v.device, n * 4, VK_BUFFER_USAGE_TRANSFER_DST_BIT);
    writeBuffer(v.device, up, f.display.data(), n * 4);
    Image in = makeImage(v.pd, v.device, f.w, f.h, VK_FORMAT_R8G8B8A8_UNORM,
                         VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT),
          out = makeImage(v.pd, v.device, f.w, f.h, VK_FORMAT_R8G8B8A8_UNORM,
                          VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT);
    auto cb = beginCB(v);
    uploadImage(cb, up, in, f.w, f.h);
    initOutput(cb, out);
    FalseColorRecordInfo r{};
    r.commandBuffer = cb;
    r.input = {in.view, VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_LAYOUT_GENERAL, f.w, f.h};
    r.output = {out.view, VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_LAYOUT_GENERAL, f.w, f.h};
    proc.recordFalseColor(r);
    readbackImage(cb, out, rb, f.w, f.h);
    submit(v, cb);
    return readBuffer(v.device, rb, n * 4);
}
std::vector<uint8_t> runShadow(Vulkan& v, MonitoringOverlays& proc, const Fixtures& f) {
    size_t n = size_t(f.w) * f.h;
    Buffer up = makeBuffer(v.pd, v.device, n * 4, VK_BUFFER_USAGE_TRANSFER_SRC_BIT),
           rb = makeBuffer(v.pd, v.device, n * 4, VK_BUFFER_USAGE_TRANSFER_DST_BIT);
    writeBuffer(v.device, up, f.display.data(), n * 4);
    Image in = makeImage(v.pd, v.device, f.w, f.h, VK_FORMAT_R8G8B8A8_UNORM,
                         VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT),
          out = makeImage(v.pd, v.device, f.w, f.h, VK_FORMAT_R8G8B8A8_UNORM,
                          VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT);
    auto cb = beginCB(v);
    uploadImage(cb, up, in, f.w, f.h);
    initOutput(cb, out);
    TonemapShadowRecordInfo r{};
    r.commandBuffer = cb;
    r.input = {in.view, VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_LAYOUT_GENERAL, f.w, f.h};
    r.output = {out.view, VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_LAYOUT_GENERAL, f.w, f.h};
    r.params.shadowsUI = 40.0f;
    proc.recordTonemapShadow(r);
    readbackImage(cb, out, rb, f.w, f.h);
    submit(v, cb);
    return readBuffer(v.device, rb, n * 4);
}
std::vector<uint8_t> runCombined(Vulkan& v, MonitoringOverlays& proc, const Fixtures& f) {
    size_t n = size_t(f.w) * f.h;
    Buffer ur = makeBuffer(v.pd, v.device, n * 2, VK_BUFFER_USAGE_TRANSFER_SRC_BIT),
           ud = makeBuffer(v.pd, v.device, n * 4, VK_BUFFER_USAGE_TRANSFER_SRC_BIT),
           rb = makeBuffer(v.pd, v.device, n * 4, VK_BUFFER_USAGE_TRANSFER_DST_BIT);
    writeBuffer(v.device, ur, f.raw.data(), n * 2);
    writeBuffer(v.device, ud, f.display.data(), n * 4);
    Image ir = makeImage(v.pd, v.device, f.w, f.h, VK_FORMAT_R16_UINT,
                         VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT),
          id = makeImage(v.pd, v.device, f.w, f.h, VK_FORMAT_R8G8B8A8_UNORM,
                         VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT),
          out = makeImage(v.pd, v.device, f.w, f.h, VK_FORMAT_R8G8B8A8_UNORM,
                          VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT);
    auto cb = beginCB(v);
    uploadImage(cb, ur, ir, f.w, f.h);
    uploadImage(cb, ud, id, f.w, f.h);
    initOutput(cb, out);
    CombinedRecordInfo r{};
    r.commandBuffer = cb;
    r.rawState = {ir.view, VK_FORMAT_R16_UINT, VK_IMAGE_LAYOUT_GENERAL, f.w, f.h};
    r.display = {id.view, VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_LAYOUT_GENERAL, f.w, f.h};
    r.output = {out.view, VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_LAYOUT_GENERAL, f.w, f.h};
    r.focusParams.sensitivity = 0.55f;
    proc.recordCombined(r);
    readbackImage(cb, out, rb, f.w, f.h);
    submit(v, cb);
    return readBuffer(v.device, rb, n * 4);
}
}  // namespace

int main(int argc, char** argv) try {
    if (argc != 7) {
        std::cerr << "usage: " << argv[0] << " raw.spv focus.spv false.spv shadow.spv combined.spv output_dir\n";
        return 2;
    }
    auto raw = loadSpv(argv[1]), focus = loadSpv(argv[2]), fc = loadSpv(argv[3]), shadow = loadSpv(argv[4]),
         combined = loadSpv(argv[5]);
    std::string outdir = argv[6];
    Vulkan v = createVulkan();
    MonitoringOverlaysCreateInfo ci{};
    ci.context = {v.pd, v.device, nullptr};
    ci.maxFramesInFlight = 1;
    ci.rawStateShader = {raw.data(), raw.size() * 4};
    ci.focusPeakingShader = {focus.data(), focus.size() * 4};
    ci.falseColorShader = {fc.data(), fc.size() * 4};
    ci.tonemapShadowShader = {shadow.data(), shadow.size() * 4};
    ci.combinedShader = {combined.data(), combined.size() * 4};
    MonitoringOverlays proc(ci);
    auto f = makeFixtures();
    auto ro = runRaw(v, proc, f), fo = runFocus(v, proc, f), fco = runFalse(v, proc, f),
         sho = runShadow(v, proc, f), co = runCombined(v, proc, f);
    writePPM(outdir + "/source_preview.ppm", f.w, f.h, f.display);
    writePAM(outdir + "/raw_state_overlay.pam", f.w, f.h, ro);
    writePPM(outdir + "/raw_state_composited.ppm", f.w, f.h, composite(f.display, ro));
    writePAM(outdir + "/focus_peaking_overlay.pam", f.w, f.h, fo);
    writePPM(outdir + "/focus_peaking_composited.ppm", f.w, f.h, composite(f.display, fo));
    writePAM(outdir + "/false_color_overlay.pam", f.w, f.h, fco);
    writePPM(outdir + "/false_color_composited.ppm", f.w, f.h, composite(f.display, fco));
    writePAM(outdir + "/tonemap_shadow_overlay.pam", f.w, f.h, sho);
    writePPM(outdir + "/tonemap_shadow_composited.ppm", f.w, f.h, composite(f.display, sho));
    writePAM(outdir + "/combined_overlay.pam", f.w, f.h, co);
    writePPM(outdir + "/combined_composited.ppm", f.w, f.h, composite(f.display, co));
    auto sun = makeSunFixtures();
    auto sunRo = runRaw(v, proc, sun), sunFo = runFocus(v, proc, sun), sunFc = runFalse(v, proc, sun),
         sunSho = runShadow(v, proc, sun), sunCo = runCombined(v, proc, sun);
    writePPM(outdir + "/sun_source_preview.ppm", sun.w, sun.h, sun.display);
    writePAM(outdir + "/sun_raw_state_overlay.pam", sun.w, sun.h, sunRo);
    writePPM(outdir + "/sun_raw_state_composited.ppm", sun.w, sun.h, composite(sun.display, sunRo));
    writePAM(outdir + "/sun_focus_peaking_overlay.pam", sun.w, sun.h, sunFo);
    writePPM(outdir + "/sun_focus_peaking_composited.ppm", sun.w, sun.h, composite(sun.display, sunFo));
    writePAM(outdir + "/sun_false_color_overlay.pam", sun.w, sun.h, sunFc);
    writePPM(outdir + "/sun_false_color_composited.ppm", sun.w, sun.h, composite(sun.display, sunFc));
    writePAM(outdir + "/sun_tonemap_shadow_overlay.pam", sun.w, sun.h, sunSho);
    writePPM(outdir + "/sun_tonemap_shadow_composited.ppm", sun.w, sun.h, composite(sun.display, sunSho));
    writePAM(outdir + "/sun_combined_overlay.pam", sun.w, sun.h, sunCo);
    writePPM(outdir + "/sun_combined_composited.ppm", sun.w, sun.h, composite(sun.display, sunCo));
    std::cout << "VISUAL_SAMPLES_PASS synthetic=" << f.w << "x" << f.h << " sun=" << sun.w << "x" << sun.h << "\n";
    return 0;
} catch (const std::exception& e) {
    std::cerr << "VISUAL_SAMPLES_FAIL: " << e.what() << "\n";
    return 1;
}
