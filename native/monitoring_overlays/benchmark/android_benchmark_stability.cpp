#include <vulkan/vulkan.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <numeric>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include "monitoring_overlays/monitoring_overlays.h"

using namespace monitoring_overlays;

namespace {
constexpr uint32_t kSlots = 3;
constexpr uint32_t kWarmup = 60;
constexpr uint32_t kMeasured = 300;
constexpr uint32_t kStressFrames = 5000;

void check(VkResult r, const char* what) {
    if (r != VK_SUCCESS) throw std::runtime_error(std::string(what) + " VkResult=" + std::to_string(int(r)));
}
std::vector<uint32_t> loadSpv(const std::string& path) {
    std::ifstream f(path, std::ios::binary | std::ios::ate);
    if (!f) throw std::runtime_error("cannot open SPIR-V: " + path);
    auto n = f.tellg();
    if (n <= 0 || (n % 4) != 0) throw std::runtime_error("invalid SPIR-V: " + path);
    std::vector<uint32_t> v(size_t(n) / 4);
    f.seekg(0);
    f.read(reinterpret_cast<char*>(v.data()), n);
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
            auto p = mp.memoryTypes[i].propertyFlags;
            if ((p & required) != required) continue;
            if (pass == 0 && preferred && (p & preferred) != preferred) continue;
            return i;
        }
    throw std::runtime_error("no compatible memory type");
}
struct Image {
    VkDevice d = VK_NULL_HANDLE;
    VkImage image = VK_NULL_HANDLE;
    VkDeviceMemory memory = VK_NULL_HANDLE;
    VkImageView view = VK_NULL_HANDLE;
    VkDeviceSize allocation = 0;
    Image() = default;
    Image(const Image&) = delete;
    Image& operator=(const Image&) = delete;
    Image(Image&& o) noexcept { *this = std::move(o); }
    Image& operator=(Image&& o) noexcept {
        if (this != &o) {
            destroy();
            d = o.d;
            image = o.image;
            memory = o.memory;
            view = o.view;
            allocation = o.allocation;
            o.image = VK_NULL_HANDLE;
            o.memory = VK_NULL_HANDLE;
            o.view = VK_NULL_HANDLE;
        }
        return *this;
    }
    void destroy() {
        if (view) vkDestroyImageView(d, view, nullptr);
        if (image) vkDestroyImage(d, image, nullptr);
        if (memory) vkFreeMemory(d, memory, nullptr);
        view = VK_NULL_HANDLE;
        image = VK_NULL_HANDLE;
        memory = VK_NULL_HANDLE;
    }
    ~Image() { destroy(); }
};
Image makeImage(VkPhysicalDevice pd, VkDevice d, uint32_t w, uint32_t h, VkFormat fmt) {
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
    ci.usage = VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    ci.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    ci.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    check(vkCreateImage(d, &ci, nullptr, &x.image), "vkCreateImage");
    VkMemoryRequirements r{};
    vkGetImageMemoryRequirements(d, x.image, &r);
    x.allocation = r.size;
    VkMemoryAllocateInfo ai{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
    ai.allocationSize = r.size;
    ai.memoryTypeIndex = memoryType(pd, r.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    check(vkAllocateMemory(d, &ai, nullptr, &x.memory), "vkAllocateMemory(image)");
    check(vkBindImageMemory(d, x.image, x.memory, 0), "vkBindImageMemory");
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
void imageBarrier(VkCommandBuffer cb, VkImage image, VkImageLayout oldL, VkImageLayout newL, VkAccessFlags srcA,
                  VkAccessFlags dstA, VkPipelineStageFlags srcS, VkPipelineStageFlags dstS) {
    VkImageMemoryBarrier b{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
    b.oldLayout = oldL;
    b.newLayout = newL;
    b.srcAccessMask = srcA;
    b.dstAccessMask = dstA;
    b.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    b.image = image;
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
    float timestampPeriod = 0;
    uint32_t timestampBits = 0;
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
            timestampPeriod = o.timestampPeriod;
            timestampBits = o.timestampBits;
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
    VkApplicationInfo ai{VK_STRUCTURE_TYPE_APPLICATION_INFO};
    ai.pApplicationName = "monitoring_overlays_android_benchmark";
    ai.apiVersion = VK_API_VERSION_1_1;
    VkInstanceCreateInfo ici{VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};
    ici.pApplicationInfo = &ai;
    check(vkCreateInstance(&ici, nullptr, &v.instance), "vkCreateInstance");
    uint32_t n = 0;
    check(vkEnumeratePhysicalDevices(v.instance, &n, nullptr), "vkEnumeratePhysicalDevices(count)");
    if (!n) throw std::runtime_error("no Vulkan physical device");
    std::vector<VkPhysicalDevice> ds(n);
    check(vkEnumeratePhysicalDevices(v.instance, &n, ds.data()), "vkEnumeratePhysicalDevices");
    v.pd = ds[0];
    VkPhysicalDeviceProperties prop{};
    VkPhysicalDeviceFeatures feat{};
    vkGetPhysicalDeviceProperties(v.pd, &prop);
    vkGetPhysicalDeviceFeatures(v.pd, &feat);
    v.timestampPeriod = prop.limits.timestampPeriod;
    std::cout << "DEVICE name=\"" << prop.deviceName << "\" vendor=0x" << std::hex << prop.vendorID << " device=0x"
              << prop.deviceID << std::dec << " api=" << VK_VERSION_MAJOR(prop.apiVersion) << "."
              << VK_VERSION_MINOR(prop.apiVersion) << "." << VK_VERSION_PATCH(prop.apiVersion)
              << " timestampPeriodNs=" << v.timestampPeriod << "\n";
    uint32_t qn = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(v.pd, &qn, nullptr);
    std::vector<VkQueueFamilyProperties> qp(qn);
    vkGetPhysicalDeviceQueueFamilyProperties(v.pd, &qn, qp.data());
    bool found = false;
    for (uint32_t i = 0; i < qn; i++)
        if ((qp[i].queueFlags & VK_QUEUE_COMPUTE_BIT) && qp[i].timestampValidBits) {
            v.qf = i;
            v.timestampBits = qp[i].timestampValidBits;
            found = true;
            break;
        }
    if (!found) throw std::runtime_error("no compute queue family with timestamp support");
    float pri = 1;
    VkDeviceQueueCreateInfo qi{VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO};
    qi.queueFamilyIndex = v.qf;
    qi.queueCount = 1;
    qi.pQueuePriorities = &pri;
    VkDeviceCreateInfo di{VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO};
    di.queueCreateInfoCount = 1;
    di.pQueueCreateInfos = &qi;
    check(vkCreateDevice(v.pd, &di, nullptr, &v.device), "vkCreateDevice");
    vkGetDeviceQueue(v.device, v.qf, 0, &v.queue);
    VkCommandPoolCreateInfo pi{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
    pi.queueFamilyIndex = v.qf;
    pi.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    check(vkCreateCommandPool(v.device, &pi, nullptr, &v.pool), "vkCreateCommandPool");
    return v;
}
struct Frame {
    Image raw, focus, display, out;
    VkCommandBuffer cb = VK_NULL_HANDLE;
    VkFence fence = VK_NULL_HANDLE;
    bool submitted = false;
    bool measured = false;
    uint32_t measureIndex = 0;
};
struct FrameSet {
    Vulkan* v = nullptr;
    uint32_t w = 0, h = 0;
    std::array<Frame, kSlots> f{};
    VkQueryPool queries = VK_NULL_HANDLE;
    VkDeviceSize imageBytes = 0;
    ~FrameSet() {
        if (!v) return;
        for (auto& s : f)
            if (s.fence) vkDestroyFence(v->device, s.fence, nullptr);
        if (queries) vkDestroyQueryPool(v->device, queries, nullptr);
    }
};
void setupFrames(FrameSet& fs, Vulkan& v, uint32_t w, uint32_t h, float displayValue, uint32_t rawWord) {
    fs.v = &v;
    fs.w = w;
    fs.h = h;
    VkQueryPoolCreateInfo q{VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO};
    q.queryType = VK_QUERY_TYPE_TIMESTAMP;
    q.queryCount = 2 * kSlots;
    check(vkCreateQueryPool(v.device, &q, nullptr, &fs.queries), "vkCreateQueryPool");
    std::array<VkCommandBuffer, kSlots> cbs{};
    VkCommandBufferAllocateInfo ca{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
    ca.commandPool = v.pool;
    ca.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    ca.commandBufferCount = kSlots;
    check(vkAllocateCommandBuffers(v.device, &ca, cbs.data()), "vkAllocateCommandBuffers");
    for (uint32_t s = 0; s < kSlots; s++) {
        auto& f = fs.f[s];
        f.cb = cbs[s];
        f.raw = makeImage(v.pd, v.device, w, h, VK_FORMAT_R16_UINT);
        f.focus = makeImage(v.pd, v.device, w, h, VK_FORMAT_R8G8B8A8_UNORM);
        f.display = makeImage(v.pd, v.device, w, h, VK_FORMAT_R8G8B8A8_UNORM);
        f.out = makeImage(v.pd, v.device, w, h, VK_FORMAT_R8G8B8A8_UNORM);
        fs.imageBytes += f.raw.allocation + f.focus.allocation + f.display.allocation + f.out.allocation;
        VkFenceCreateInfo fi{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
        fi.flags = VK_FENCE_CREATE_SIGNALED_BIT;
        check(vkCreateFence(v.device, &fi, nullptr, &f.fence), "vkCreateFence");
        VkCommandBufferBeginInfo bi{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
        bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        check(vkBeginCommandBuffer(f.cb, &bi), "vkBeginCommandBuffer(setup)");
        auto prep = [&](Image& im, VkClearColorValue cv) {
            imageBarrier(f.cb, im.image, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 0,
                         VK_ACCESS_TRANSFER_WRITE_BIT, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                         VK_PIPELINE_STAGE_TRANSFER_BIT);
            VkImageSubresourceRange rr{};
            rr.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            rr.levelCount = 1;
            rr.layerCount = 1;
            vkCmdClearColorImage(f.cb, im.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &cv, 1, &rr);
            imageBarrier(f.cb, im.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_GENERAL,
                         VK_ACCESS_TRANSFER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT,
                         VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT);
        };
        VkClearColorValue r{};
        r.uint32[0] = rawWord;
        prep(f.raw, r);
        VkClearColorValue fc{};
        fc.float32[0] = fc.float32[1] = fc.float32[2] = 0.5f;
        fc.float32[3] = 1.0f;
        prep(f.focus, fc);
        VkClearColorValue dc{};
        dc.float32[0] = dc.float32[1] = dc.float32[2] = displayValue;
        dc.float32[3] = 1;
        prep(f.display, dc);
        VkClearColorValue oc{};
        prep(f.out, oc);
        check(vkEndCommandBuffer(f.cb), "vkEndCommandBuffer(setup)");
        check(vkResetFences(v.device, 1, &f.fence), "vkResetFences(setup)");
        VkSubmitInfo si{VK_STRUCTURE_TYPE_SUBMIT_INFO};
        si.commandBufferCount = 1;
        si.pCommandBuffers = &f.cb;
        check(vkQueueSubmit(v.queue, 1, &si, f.fence), "vkQueueSubmit(setup)");
        check(vkWaitForFences(v.device, 1, &f.fence, VK_TRUE, UINT64_MAX), "vkWaitForFences(setup)");
        check(vkResetCommandBuffer(f.cb, 0), "vkResetCommandBuffer(setup)");
    }
}
enum class Case { Raw, Focus, FalseColor, Shadow, CombinedTypical, CombinedWorst };
const char* caseName(Case c) {
    switch (c) {
        case Case::Raw:
            return "RAW_STATE";
        case Case::Focus:
            return "FOCUS";
        case Case::FalseColor:
            return "FALSE_COLOR";
        case Case::Shadow:
            return "TONEMAP_SHADOW";
        case Case::CombinedTypical:
            return "COMBINED_TYPICAL";
        default:
            return "COMBINED_WORST";
    }
}
FalseColorParams worstFalse() {
    FalseColorParams p{};
    p.enabled = true;
    p.rangeCount = 16;
    for (uint32_t i = 0; i < 16; i++) {
        p.ranges[i].lowIre = float(i) * 6.25f;
        p.ranges[i].highIre = (i == 15) ? 100.001f : float(i + 1) * 6.25f;
        p.ranges[i].color = {float(i & 1), float((i >> 1) & 1), float((i >> 2) & 1), 0.75f};
    }
    return p;
}
void recordCase(MonitoringOverlays& proc, Frame& f, uint32_t slot, uint32_t w, uint32_t h, Case c, VkQueryPool qp,
                uint32_t qbase, float sensitivity = 0.55f) {
    check(vkResetCommandBuffer(f.cb, 0), "vkResetCommandBuffer");
    VkCommandBufferBeginInfo bi{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    check(vkBeginCommandBuffer(f.cb, &bi), "vkBeginCommandBuffer");
    vkCmdResetQueryPool(f.cb, qp, qbase, 2);
    vkCmdWriteTimestamp(f.cb, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, qp, qbase);
    OverlayImageView out{f.out.view, VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_LAYOUT_GENERAL, w, h};
    RawStateImageView raw{f.raw.view, VK_FORMAT_R16_UINT, VK_IMAGE_LAYOUT_GENERAL, w, h};
    DisplayImageView focusDisplay{f.focus.view, VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_LAYOUT_GENERAL, w, h};
    DisplayImageView display{f.display.view, VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_LAYOUT_GENERAL, w, h};
    if (c == Case::Raw) {
        RawStateRecordInfo r{};
        r.commandBuffer = f.cb;
        r.input = raw;
        r.output = out;
        r.frameSlot = slot;
        proc.recordRawStateOverlay(r);
    } else if (c == Case::Focus) {
        FocusPeakingRecordInfo r{};
        r.commandBuffer = f.cb;
        r.input = focusDisplay;
        r.output = out;
        r.frameSlot = slot;
        r.params.sensitivity = sensitivity;
        proc.recordFocusPeaking(r);
    } else if (c == Case::FalseColor) {
        FalseColorRecordInfo r{};
        r.commandBuffer = f.cb;
        r.input = display;
        r.output = out;
        r.frameSlot = slot;
        proc.recordFalseColor(r);
    } else if (c == Case::Shadow) {
        TonemapShadowRecordInfo r{};
        r.commandBuffer = f.cb;
        r.input = display;
        r.output = out;
        r.frameSlot = slot;
        proc.recordTonemapShadow(r);
    } else {
        CombinedRecordInfo r{};
        r.commandBuffer = f.cb;
        r.rawState = raw;
        r.display = display;
        r.output = out;
        r.frameSlot = slot;
        r.focusParams.sensitivity = sensitivity;
        if (c == Case::CombinedWorst) r.falseColorParams = worstFalse();
        proc.recordCombined(r);
    }
    vkCmdWriteTimestamp(f.cb, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, qp, qbase + 1);
    check(vkEndCommandBuffer(f.cb), "vkEndCommandBuffer");
}
struct Stats {
    double best = 0, median = 0, mean = 0, p95 = 0, p99 = 0, worst = 0, cpuMedianUs = 0;
};
double pct(std::vector<double> v, double p) {
    std::sort(v.begin(), v.end());
    double idx = (v.size() - 1) * p;
    size_t a = size_t(std::floor(idx)), b = size_t(std::ceil(idx));
    double t = idx - a;
    return v[a] * (1 - t) + v[b] * t;
}
Stats summarize(const std::vector<double>& ms, const std::vector<double>& cpu) {
    Stats s;
    s.best = *std::min_element(ms.begin(), ms.end());
    s.worst = *std::max_element(ms.begin(), ms.end());
    s.mean = std::accumulate(ms.begin(), ms.end(), 0.0) / ms.size();
    s.median = pct(ms, .5);
    s.p95 = pct(ms, .95);
    s.p99 = pct(ms, .99);
    s.cpuMedianUs = pct(cpu, .5);
    return s;
}
void printBudget(Case c, const Stats& s) {
    std::cout << "BUDGET " << caseName(c);
    if (c == Case::Raw || c == Case::FalseColor || c == Case::Shadow) {
        std::cout << " median_target_ms=0.2000 result=" << (s.median <= .20 ? "PASS" : "MISS")
                  << " p95_target_ms=0.3000 result=" << (s.p95 <= .30 ? "PASS" : "MISS");
    } else if (c == Case::Focus) {
        std::cout << " median_target_ms=0.5000 result=" << (s.median <= .50 ? "PASS" : "MISS")
                  << " p95_target_ms=0.7500 result=" << (s.p95 <= .75 ? "PASS" : "MISS");
    } else if (c == Case::CombinedTypical) {
        std::cout << " median_target_ms=0.7500 result=" << (s.median <= .75 ? "PASS" : "MISS")
                  << " p95_target_ms=1.0000 result=" << (s.p95 <= 1.00 ? "PASS" : "MISS")
                  << " release_ceiling_p95_1.50=" << (s.p95 <= 1.50 ? "PASS" : "MISS");
    } else {
        std::cout << " p95_desirable_ms=1.0000 result=" << (s.p95 <= 1.00 ? "PASS" : "MISS")
                  << " release_ceiling_p95_1.50=" << (s.p95 <= 1.50 ? "PASS" : "MISS");
    }
    std::cout << "\n";
}
Stats benchmarkCase(Vulkan& v, MonitoringOverlays& proc, uint32_t w, uint32_t h, Case c) {
    float disp = (c == Case::CombinedWorst) ? 0.99f : 0.50f;
    uint32_t rw = (c == Case::CombinedWorst) ? 0x0F00u : 0x000Fu;
    FrameSet fs;
    setupFrames(fs, v, w, h, disp, rw);
    std::vector<double> gpu, cpu;
    gpu.reserve(kMeasured);
    cpu.reserve(kMeasured);
    uint32_t total = kWarmup + kMeasured;
    auto harvest = [&](Frame& f) {
        if (!f.submitted) return;
        check(vkWaitForFences(v.device, 1, &f.fence, VK_TRUE, UINT64_MAX), "vkWaitForFences(benchmark)");
        if (f.measured) {
            uint64_t q[2]{};
            check(vkGetQueryPoolResults(v.device, fs.queries, 2 * (f.measureIndex % kSlots), 2, sizeof(q), q,
                                        sizeof(uint64_t), VK_QUERY_RESULT_64_BIT),
                  "vkGetQueryPoolResults");
            uint64_t mask = v.timestampBits >= 64 ? ~uint64_t(0) : ((uint64_t(1) << v.timestampBits) - 1);
            uint64_t ticks = (q[1] - q[0]) & mask;
            gpu.push_back(double(ticks) * double(v.timestampPeriod) * 1e-6);
        }
        f.submitted = false;
    };
    for (uint32_t i = 0; i < total; i++) {
        uint32_t s = i % kSlots;
        auto& f = fs.f[s];
        harvest(f);
        check(vkResetFences(v.device, 1, &f.fence), "vkResetFences(benchmark)");
        auto t0 = std::chrono::steady_clock::now();
        recordCase(proc, f, s, w, h, c, fs.queries, 2 * s);
        auto t1 = std::chrono::steady_clock::now();
        double us = std::chrono::duration<double, std::micro>(t1 - t0).count();
        f.measured = i >= kWarmup;
        f.measureIndex = s;
        if (f.measured) cpu.push_back(us);
        VkSubmitInfo si{VK_STRUCTURE_TYPE_SUBMIT_INFO};
        si.commandBufferCount = 1;
        si.pCommandBuffers = &f.cb;
        check(vkQueueSubmit(v.queue, 1, &si, f.fence), "vkQueueSubmit(benchmark)");
        f.submitted = true;
    }
    for (auto& f : fs.f) harvest(f);
    if (gpu.size() != kMeasured || cpu.size() != kMeasured) throw std::runtime_error("benchmark sample count mismatch");
    auto st = summarize(gpu, cpu);
    std::cout << std::fixed << std::setprecision(4) << "BENCH " << caseName(c) << " " << w << "x" << h
              << " frames=" << kMeasured << " best_ms=" << st.best << " median_ms=" << st.median
              << " mean_ms=" << st.mean << " p95_ms=" << st.p95 << " p99_ms=" << st.p99 << " worst_ms=" << st.worst
              << " cpu_record_median_us=" << st.cpuMedianUs << " test_image_alloc_bytes=" << fs.imageBytes << "\n";
    printBudget(c, st);
    return st;
}
void stress(Vulkan& v, MonitoringOverlays& proc, uint32_t w, uint32_t h) {
    FrameSet fs;
    setupFrames(fs, v, w, h, 0.5f, 0x0F00u);
    auto fc = FalseColorParams::defaultPreset();
    for (uint32_t i = 0; i < kStressFrames; i++) {
        uint32_t s = i % kSlots;
        auto& f = fs.f[s];
        if (f.submitted) {
            check(vkWaitForFences(v.device, 1, &f.fence, VK_TRUE, UINT64_MAX), "vkWaitForFences(stress)");
            f.submitted = false;
        }
        check(vkResetFences(v.device, 1, &f.fence), "vkResetFences(stress)");
        check(vkResetCommandBuffer(f.cb, 0), "vkResetCommandBuffer(stress)");
        VkCommandBufferBeginInfo bi{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
        bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        check(vkBeginCommandBuffer(f.cb, &bi), "vkBeginCommandBuffer(stress)");
        CombinedRecordInfo r{};
        r.commandBuffer = f.cb;
        r.rawState = {f.raw.view, VK_FORMAT_R16_UINT, VK_IMAGE_LAYOUT_GENERAL, w, h};
        r.display = {f.display.view, VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_LAYOUT_GENERAL, w, h};
        r.output = {f.out.view, VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_LAYOUT_GENERAL, w, h};
        r.frameSlot = s;
        r.focusParams.sensitivity = 0.35f + 0.6f * float(i % 17) / 16.0f;
        r.focusParams.color.a = 0.55f + 0.3f * float(i % 11) / 10.0f;
        r.falseColorParams = fc;
        r.falseColorParams.ranges[3].color.a = 0.40f + 0.4f * float(i % 13) / 12.0f;
        proc.recordCombined(r);
        check(vkEndCommandBuffer(f.cb), "vkEndCommandBuffer(stress)");
        VkSubmitInfo si{VK_STRUCTURE_TYPE_SUBMIT_INFO};
        si.commandBufferCount = 1;
        si.pCommandBuffers = &f.cb;
        check(vkQueueSubmit(v.queue, 1, &si, f.fence), "vkQueueSubmit(stress)");
        f.submitted = true;
    }
    for (auto& f : fs.f)
        if (f.submitted)
            check(vkWaitForFences(v.device, 1, &f.fence, VK_TRUE, UINT64_MAX), "vkWaitForFences(stress drain)");
    std::cout << "STABILITY_PASS resolution=" << w << "x" << h << " frames=" << kStressFrames
              << " frames_in_flight=" << kSlots << " dynamic_recording=YES\n";
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
    ci.maxFramesInFlight = kSlots;
    ci.rawStateShader = {raw.data(), raw.size() * 4};
    ci.focusPeakingShader = {focus.data(), focus.size() * 4};
    ci.falseColorShader = {fc.data(), fc.size() * 4};
    ci.tonemapShadowShader = {shadow.data(), shadow.size() * 4};
    ci.combinedShader = {combined.data(), combined.size() * 4};
    MonitoringOverlays proc(ci);
    for (auto wh : std::array<std::pair<uint32_t, uint32_t>, 3>{{{2040, 1532}, {2040, 1536}, {2048, 1536}}}) {
        uint32_t w = wh.first, h = wh.second;
        std::cout << "\n=== BENCHMARK " << w << "x" << h << " ===\n";
        benchmarkCase(v, proc, w, h, Case::Raw);
        benchmarkCase(v, proc, w, h, Case::Focus);
        benchmarkCase(v, proc, w, h, Case::FalseColor);
        benchmarkCase(v, proc, w, h, Case::Shadow);
        benchmarkCase(v, proc, w, h, Case::CombinedTypical);
        benchmarkCase(v, proc, w, h, Case::CombinedWorst);
    }
    std::cout << "\n=== SUSTAINED MULTI-FRAME-IN-FLIGHT STABILITY ===\n";
    stress(v, proc, 2040, 1532);
    stress(v, proc, 2040, 1536);
    stress(v, proc, 2048, 1536);  // exact 4096x3072 -> 2048x1536 production path
    std::cout << "PRODUCTION_GEOMETRY_STABILITY_PASS 2048x1536 2040x1532 2040x1536 frames_per_geometry="
              << kStressFrames << " frames_in_flight=" << kSlots << "\n";
    std::cout << "ANDROID_BENCHMARK_STABILITY_PASS\n";
    return 0;
} catch (const std::exception& e) {
    std::cerr << "ANDROID_BENCHMARK_STABILITY_FAIL: " << e.what() << "\n";
    return 1;
}
