
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
    if (bytes <= 0 || (bytes % 4) != 0) throw std::runtime_error("invalid SPIR-V size: " + path);
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
    for (int pass = 0; pass < 2; pass++) {
        for (uint32_t i = 0; i < mp.memoryTypeCount; i++) {
            if (!(bits & (1u << i))) continue;
            auto f = mp.memoryTypes[i].propertyFlags;
            if ((f & required) != required) continue;
            if (pass == 0 && preferred && (f & preferred) != preferred) continue;
            return i;
        }
    }
    throw std::runtime_error("no compatible Vulkan memory type");
}

struct Buffer {
    VkDevice d = VK_NULL_HANDLE;
    VkBuffer b = VK_NULL_HANDLE;
    VkDeviceMemory m = VK_NULL_HANDLE;
    VkDeviceSize size = 0;
    void destroy() {
        if (b) vkDestroyBuffer(d, b, nullptr);
        if (m) vkFreeMemory(d, m, nullptr);
        b = VK_NULL_HANDLE;
        m = VK_NULL_HANDLE;
    }
    ~Buffer() { destroy(); }
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
    void destroy() {
        if (view) vkDestroyImageView(d, view, nullptr);
        if (image) vkDestroyImage(d, image, nullptr);
        if (mem) vkFreeMemory(d, mem, nullptr);
        view = VK_NULL_HANDLE;
        image = VK_NULL_HANDLE;
        mem = VK_NULL_HANDLE;
    }
    ~Image() { destroy(); }
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

void imageBarrier(VkCommandBuffer cb, VkImage img, VkImageLayout oldL, VkImageLayout newL, VkAccessFlags srcA,
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

RawStateOverlayParams exactParams() {
    RawStateOverlayParams p{};
    p.highlightSeverity = {{{1, 0, 0, 1}, {0, 1, 0, 1}, {0, 0, 1, 1}}};
    p.shadowWarningSeverity = {{{1, 1, 0, 1}, {1, 0, 1, 1}, {0, 1, 1, 1}}};
    p.shadowClippedSeverity = {{{1, 1, 1, 1}, {0, 0, 0, 1}, {1, 0, 0, 1}}};
    return p;
}

std::array<uint8_t, 4> byteExpected(uint16_t word, const RawStateOverlayParams& p) {
    auto x = cpu_reference::rawStateOverlay(word, p);
    auto cv = [](float v) { return uint8_t(std::lround(std::clamp(v, 0.0f, 1.0f) * 255.0f)); };
    return {cv(x.r), cv(x.g), cv(x.b), cv(x.a)};
}

uint16_t fixtureWord(uint32_t x, uint32_t y) {
    // Cycle all 16 physical combinations independently through actual clipping,
    // shadow-warning and shadow-clipped classes, plus deliberately simultaneous states.
    uint16_t a = uint16_t((x + 3 * y) & 15u);
    uint16_t b = uint16_t((5 * x + y + 7u) & 15u);
    uint16_t c = uint16_t((x * 11u + y * 13u + 3u) & 15u);
    switch ((x + y) & 3u) {
        case 0:
            return a;  // actual clipping only
        case 1:
            return uint16_t(b << 8);  // shadow warning only
        case 2:
            return uint16_t(c << 12);  // at/below black only
        default:
            return uint16_t(a | (b << 8) | (c << 12));  // precedence determinism
    }
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
#ifdef VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME
    VkInstanceCreateFlags flags = 0;
    if (hasInstanceExt(VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME)) {
        ie.push_back(VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME);
        flags |= VK_INSTANCE_CREATE_ENUMERATE_PORTABILITY_BIT_KHR;
    }
#else
    VkInstanceCreateFlags flags = 0;
#endif
    VkApplicationInfo ai{VK_STRUCTURE_TYPE_APPLICATION_INFO};
    ai.pApplicationName = "monitoring_overlays_raw_gpu_validation";
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
    std::vector<VkPhysicalDevice> devs(n);
    check(vkEnumeratePhysicalDevices(v.instance, &n, devs.data()), "vkEnumeratePhysicalDevices");
    v.pd = devs[0];
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
    if (!found) throw std::runtime_error("no compute queue family");

    float priority = 1.0f;
    VkDeviceQueueCreateInfo qi{VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO};
    qi.queueFamilyIndex = v.qf;
    qi.queueCount = 1;
    qi.pQueuePriorities = &priority;
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

VkCommandBuffer beginCB(VkDevice d, VkCommandPool p) {
    VkCommandBufferAllocateInfo ai{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
    ai.commandPool = p;
    ai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    ai.commandBufferCount = 1;
    VkCommandBuffer cb;
    check(vkAllocateCommandBuffers(d, &ai, &cb), "vkAllocateCommandBuffers");
    VkCommandBufferBeginInfo bi{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    check(vkBeginCommandBuffer(cb, &bi), "vkBeginCommandBuffer");
    return cb;
}
void submitWait(VkDevice d, VkQueue q, VkCommandPool p, VkCommandBuffer cb) {
    check(vkEndCommandBuffer(cb), "vkEndCommandBuffer");
    VkSubmitInfo si{VK_STRUCTURE_TYPE_SUBMIT_INFO};
    si.commandBufferCount = 1;
    si.pCommandBuffers = &cb;
    check(vkQueueSubmit(q, 1, &si, VK_NULL_HANDLE), "vkQueueSubmit");
    check(vkQueueWaitIdle(q), "vkQueueWaitIdle");
    vkFreeCommandBuffers(d, p, 1, &cb);
}

void validateDimension(Vulkan& v, MonitoringOverlays& proc, uint32_t w, uint32_t h, const RawStateOverlayParams& params,
                       const char* label) {
    const size_t pixels = size_t(w) * h;
    std::vector<uint16_t> states(pixels);
    for (uint32_t y = 0; y < h; y++)
        for (uint32_t x = 0; x < w; x++) states[size_t(y) * w + x] = fixtureWord(x, y);
    // Force nonzero classifications on the exact right/bottom boundaries so a
    // dropped tail invocation cannot accidentally pass because the expected pixel is transparent.
    for (uint32_t y = 0; y < h; y++) states[size_t(y) * w + (w - 1)] = 0x000Fu;
    for (uint32_t x = 0; x < w; x++) states[size_t(h - 1) * w + x] = 0x0F00u;
    states[size_t(h - 1) * w + (w - 1)] = 0xF000u;

    Buffer upload = makeBuffer(v.pd, v.device, pixels * 2, VK_BUFFER_USAGE_TRANSFER_SRC_BIT);
    Buffer readback = makeBuffer(v.pd, v.device, pixels * 4, VK_BUFFER_USAGE_TRANSFER_DST_BIT);
    void* m = nullptr;
    check(vkMapMemory(v.device, upload.m, 0, VK_WHOLE_SIZE, 0, &m), "vkMapMemory(upload)");
    std::memcpy(m, states.data(), pixels * 2);
    vkUnmapMemory(v.device, upload.m);

    Image in = makeImage(v.pd, v.device, w, h, VK_FORMAT_R16_UINT,
                         VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT);
    Image out =
        makeImage(v.pd, v.device, w, h, VK_FORMAT_R8G8B8A8_UNORM,
                  VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT);

    VkCommandBuffer cb = beginCB(v.device, v.pool);
    imageBarrier(cb, in.image, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 0,
                 VK_ACCESS_TRANSFER_WRITE_BIT, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT);
    VkBufferImageCopy c{};
    c.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    c.imageSubresource.layerCount = 1;
    c.imageExtent = {w, h, 1};
    vkCmdCopyBufferToImage(cb, upload.b, in.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &c);
    imageBarrier(cb, in.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_GENERAL,
                 VK_ACCESS_TRANSFER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
                 VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT);
    imageBarrier(cb, out.image, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL, 0, VK_ACCESS_SHADER_WRITE_BIT,
                 VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT);

    RawStateRecordInfo ri{};
    ri.commandBuffer = cb;
    ri.input = {in.view, VK_FORMAT_R16_UINT, VK_IMAGE_LAYOUT_GENERAL, w, h};
    ri.output = {out.view, VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_LAYOUT_GENERAL, w, h};
    ri.params = params;
    ri.frameSlot = 0;
    ri.composeOverExisting = false;
    proc.recordRawStateOverlay(ri);

    imageBarrier(cb, out.image, VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                 VK_ACCESS_SHADER_WRITE_BIT, VK_ACCESS_TRANSFER_READ_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                 VK_PIPELINE_STAGE_TRANSFER_BIT);
    VkBufferImageCopy r{};
    r.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    r.imageSubresource.layerCount = 1;
    r.imageExtent = {w, h, 1};
    vkCmdCopyImageToBuffer(cb, out.image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, readback.b, 1, &r);
    submitWait(v.device, v.queue, v.pool, cb);

    check(vkMapMemory(v.device, readback.m, 0, VK_WHOLE_SIZE, 0, &m), "vkMapMemory(readback)");
    const uint8_t* got = static_cast<const uint8_t*>(m);
    size_t bad = 0;
    size_t first = 0;
    for (size_t i = 0; i < pixels; i++) {
        auto e = byteExpected(states[i], ri.params);
        bool mismatch = false;
        for (int k = 0; k < 4; k++)
            if (got[4 * i + k] != e[k]) mismatch = true;
        if (mismatch) {
            if (!bad) first = i;
            bad++;
        }
    }
    if (bad) {
        auto e = byteExpected(states[first], ri.params);
        std::cerr << "Mismatch " << w << "x" << h << ": " << bad << "/" << pixels << " pixels; first index " << first
                  << " word=0x" << std::hex << states[first] << std::dec << " got=(" << int(got[4 * first]) << ","
                  << int(got[4 * first + 1]) << "," << int(got[4 * first + 2]) << "," << int(got[4 * first + 3]) << ")"
                  << " expected=(" << int(e[0]) << "," << int(e[1]) << "," << int(e[2]) << "," << int(e[3]) << ")\n";
        vkUnmapMemory(v.device, readback.m);
        throw std::runtime_error("RAW-state GPU parity failed");
    }
    vkUnmapMemory(v.device, readback.m);
    std::cout << "RAW_STATE_GPU_PARITY_PASS " << label << " " << w << "x" << h << " pixels=" << pixels
              << " right_bottom_edges=EXERCISED\n";
}

}  // namespace

int main(int argc, char** argv) try {
    if (argc != 6) {
        std::cerr << "usage: " << argv[0]
                  << " raw_state.spv focus_peaking.spv false_color.spv tonemap_shadow.spv combined_overlay.spv\n";
        return 2;
    }
    auto raw = loadSpv(argv[1]), focus = loadSpv(argv[2]), fc = loadSpv(argv[3]), shadow = loadSpv(argv[4]),
         combined = loadSpv(argv[5]);
    Vulkan v = createVulkan();

    VkFormatProperties r16{}, rgba8{};
    vkGetPhysicalDeviceFormatProperties(v.pd, VK_FORMAT_R16_UINT, &r16);
    vkGetPhysicalDeviceFormatProperties(v.pd, VK_FORMAT_R8G8B8A8_UNORM, &rgba8);
    if (!(r16.optimalTilingFeatures & VK_FORMAT_FEATURE_STORAGE_IMAGE_BIT))
        throw std::runtime_error("R16_UINT storage image unsupported");
    if (!(rgba8.optimalTilingFeatures & VK_FORMAT_FEATURE_STORAGE_IMAGE_BIT))
        throw std::runtime_error("RGBA8_UNORM storage image unsupported");

    MonitoringOverlaysCreateInfo ci{};
    if (ci.rawWorkgroupSizeX != 16 || ci.rawWorkgroupSizeY != 16 || ci.focusWorkgroupSizeX != 8 ||
        ci.focusWorkgroupSizeY != 8 || ci.falseColorWorkgroupSizeX != 16 || ci.falseColorWorkgroupSizeY != 16 ||
        ci.tonemapShadowWorkgroupSizeX != 16 || ci.tonemapShadowWorkgroupSizeY != 16 ||
        ci.combinedWorkgroupSizeX != 8 || ci.combinedWorkgroupSizeY != 8)
        throw std::runtime_error("workgroup default/shader ABI mismatch");
    ci.context = {v.pd, v.device, nullptr};
    ci.maxFramesInFlight = 1;
    ci.rawStateShader = {raw.data(), raw.size() * 4};
    ci.focusPeakingShader = {focus.data(), focus.size() * 4};
    ci.falseColorShader = {fc.data(), fc.size() * 4};
    ci.tonemapShadowShader = {shadow.data(), shadow.size() * 4};
    ci.combinedShader = {combined.data(), combined.size() * 4};
    MonitoringOverlays proc(ci);

    auto all = exactParams();
    auto hi = all;
    hi.shadowWarningEnabled = false;
    hi.shadowClippedEnabled = false;
    auto sw = all;
    sw.highlightEnabled = false;
    sw.shadowClippedEnabled = false;
    auto sc = all;
    sc.highlightEnabled = false;
    sc.shadowWarningEnabled = false;

    // Small awkward image: independently prove every physical state class/offset.
    validateDimension(v, proc, 63, 65, hi, "highlight-only");
    validateDimension(v, proc, 63, 65, sw, "shadow-warning-only");
    validateDimension(v, proc, 63, 65, sc, "shadow-clipped-only");
    validateDimension(v, proc, 63, 65, all, "all-enabled");

    // Generic awkward tail/coordinate stress.
    validateDimension(v, proc, 2039, 1531, all, "all-enabled-stress");

    // Three exact realtime production preview geometries.
    for (auto [w, h] : std::array<std::pair<uint32_t, uint32_t>, 3>{{{2048, 1536}, {2040, 1532}, {2040, 1536}}})
        validateDimension(v, proc, w, h, all, "all-enabled-production");

    std::cout << "PRODUCTION_GEOMETRY_RAW_STATE_PASS 2048x1536 2040x1532 2040x1536 edges=VERIFIED\n";
    std::cout << "RAW_STATE_VULKAN_VALIDATION_PASS\n";
    return 0;
} catch (const std::exception& e) {
    std::cerr << "RAW_STATE_VULKAN_VALIDATION_FAIL: " << e.what() << "\n";
    return 1;
}
