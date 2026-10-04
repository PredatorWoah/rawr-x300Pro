#pragma once
// Shared Vulkan bootstrap + image helpers for spektrafilm bench/validation.
// Mirrors vk_common/testing/vk_test_common.hpp conventions.
#include <vulkan/vulkan.h>

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <stdexcept>
#include <string>
#include <vector>

namespace spektra_test {

inline void check(VkResult result, const char* what) {
    if (result != VK_SUCCESS) {
        throw std::runtime_error(std::string("spektra_test: ") + what +
                                 " VkResult=" + std::to_string(result));
    }
}

static VKAPI_ATTR VkBool32 VKAPI_CALL validationCallback(
    VkDebugUtilsMessageSeverityFlagBitsEXT severity, VkDebugUtilsMessageTypeFlagsEXT,
    const VkDebugUtilsMessengerCallbackDataEXT* data, void*) {
    std::fprintf(stderr, "vvl[%u]: %s\n", (unsigned)severity,
                 data && data->pMessage ? data->pMessage : "?");
    return VK_FALSE;
}

struct Ctx {
    VkInstance instance = VK_NULL_HANDLE;
    VkPhysicalDevice physical = VK_NULL_HANDLE;
    VkDevice device = VK_NULL_HANDLE;
    VkQueue queue = VK_NULL_HANDLE;
    uint32_t queueFamily = 0;
};

// useValidationLayers requires VK_ADD_LAYER_PATH pointing at a dir with
// VkLayer_khronos_validation.json (see module CMake).
inline Ctx makeCtx(const char* appName, bool useValidationLayers) {
    Ctx ctx{};
    VkApplicationInfo app{};
    app.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    app.pApplicationName = appName;
    app.apiVersion = VK_API_VERSION_1_1;
    const char* layers[] = {"VK_LAYER_KHRONOS_validation"};
    VkInstanceCreateInfo ii{};
    ii.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    ii.pApplicationInfo = &app;
    ii.enabledLayerCount = useValidationLayers ? 1u : 0u;
    ii.ppEnabledLayerNames = layers;
#ifdef __APPLE__
    ii.flags |= VK_INSTANCE_CREATE_ENUMERATE_PORTABILITY_BIT_KHR;
    const char* instanceExts[] = {"VK_KHR_portability_enumeration"};
    ii.enabledExtensionCount = 1;
    ii.ppEnabledExtensionNames = instanceExts;
#endif
    check(vkCreateInstance(&ii, nullptr, &ctx.instance), "vkCreateInstance");
    if (useValidationLayers) {
        auto createMessenger =
            (PFN_vkCreateDebugUtilsMessengerEXT)vkGetInstanceProcAddr(
                ctx.instance, "vkCreateDebugUtilsMessengerEXT");
        if (createMessenger) {
            VkDebugUtilsMessengerCreateInfoEXT mi{};
            mi.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT;
            mi.messageSeverity =
                VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT |
                VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
            mi.messageType =
                VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
                VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
            mi.pfnUserCallback = validationCallback;
            VkDebugUtilsMessengerEXT messenger = VK_NULL_HANDLE;
            if (createMessenger(ctx.instance, &mi, nullptr, &messenger) !=
                VK_SUCCESS) {
                std::fprintf(stderr, "spektra_test: no debug messenger\n");
            }
        }
    }
    uint32_t count = 0;
    check(vkEnumeratePhysicalDevices(ctx.instance, &count, nullptr), "enum pd");
    if (count == 0) {
        throw std::runtime_error("spektra_test: no physical device");
    }
    std::vector<VkPhysicalDevice> devices(count);
    vkEnumeratePhysicalDevices(ctx.instance, &count, devices.data());
    ctx.physical = devices[0];
    uint32_t queueCount = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(ctx.physical, &queueCount, nullptr);
    std::vector<VkQueueFamilyProperties> queues(queueCount);
    vkGetPhysicalDeviceQueueFamilyProperties(ctx.physical, &queueCount,
                                             queues.data());
    ctx.queueFamily = UINT32_MAX;
    for (uint32_t i = 0; i < queueCount; ++i) {
        if (queues[i].queueFlags & VK_QUEUE_COMPUTE_BIT) {
            ctx.queueFamily = i;
            break;
        }
    }
    if (ctx.queueFamily == UINT32_MAX) {
        throw std::runtime_error("spektra_test: no compute queue");
    }
    float priority = 1.0f;
    VkDeviceQueueCreateInfo qi{};
    qi.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
    qi.queueFamilyIndex = ctx.queueFamily;
    qi.queueCount = 1;
    qi.pQueuePriorities = &priority;
    VkDeviceCreateInfo di{};
    di.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
    di.queueCreateInfoCount = 1;
    di.pQueueCreateInfos = &qi;
#ifdef __APPLE__
    const char* deviceExts[] = {"VK_KHR_portability_subset"};
    di.enabledExtensionCount = 1;
    di.ppEnabledExtensionNames = deviceExts;
#endif
    check(vkCreateDevice(ctx.physical, &di, nullptr, &ctx.device),
          "vkCreateDevice");
    vkGetDeviceQueue(ctx.device, ctx.queueFamily, 0, &ctx.queue);
    return ctx;
}

inline void destroyCtx(Ctx& ctx) {
    if (ctx.device) {
        vkDeviceWaitIdle(ctx.device);
        vkDestroyDevice(ctx.device, nullptr);
    }
    if (ctx.instance) {
        vkDestroyInstance(ctx.instance, nullptr);
    }
    ctx = Ctx{};
}

inline uint32_t memoryType(const Ctx& ctx, uint32_t bits,
                           VkMemoryPropertyFlags flags) {
    VkPhysicalDeviceMemoryProperties props{};
    vkGetPhysicalDeviceMemoryProperties(ctx.physical, &props);
    for (uint32_t i = 0; i < props.memoryTypeCount; ++i) {
        if ((bits & (1u << i)) &&
            (props.memoryTypes[i].propertyFlags & flags) == flags) {
            return i;
        }
    }
    throw std::runtime_error("spektra_test: no memory type");
}

struct Image {
    VkImage image = VK_NULL_HANDLE;
    VkDeviceMemory memory = VK_NULL_HANDLE;
    VkImageView view = VK_NULL_HANDLE;
};

inline Image makeImage(const Ctx& ctx, uint32_t w, uint32_t h, VkFormat format) {
    Image img{};
    VkImageCreateInfo ci{};
    ci.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    ci.imageType = VK_IMAGE_TYPE_2D;
    ci.format = format;
    ci.extent = {w, h, 1};
    ci.mipLevels = 1;
    ci.arrayLayers = 1;
    ci.samples = VK_SAMPLE_COUNT_1_BIT;
    ci.tiling = VK_IMAGE_TILING_OPTIMAL;
    ci.usage = VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT |
               VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    ci.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    ci.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    check(vkCreateImage(ctx.device, &ci, nullptr, &img.image), "vkCreateImage");
    VkMemoryRequirements req{};
    vkGetImageMemoryRequirements(ctx.device, img.image, &req);
    VkMemoryAllocateInfo ai{};
    ai.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    ai.allocationSize = req.size;
    ai.memoryTypeIndex =
        memoryType(ctx, req.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    check(vkAllocateMemory(ctx.device, &ai, nullptr, &img.memory), "alloc image");
    check(vkBindImageMemory(ctx.device, img.image, img.memory, 0), "bind image");
    VkImageViewCreateInfo vi{};
    vi.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    vi.image = img.image;
    vi.viewType = VK_IMAGE_VIEW_TYPE_2D;
    vi.format = format;
    vi.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    check(vkCreateImageView(ctx.device, &vi, nullptr, &img.view), "make view");
    return img;
}

inline void destroyImage(const Ctx& ctx, Image& img) {
    if (img.view) {
        vkDestroyImageView(ctx.device, img.view, nullptr);
    }
    if (img.image) {
        vkDestroyImage(ctx.device, img.image, nullptr);
    }
    if (img.memory) {
        vkFreeMemory(ctx.device, img.memory, nullptr);
    }
    img = Image{};
}

inline VkCommandBuffer beginOneShot(const Ctx& ctx, VkCommandPool pool) {
    VkCommandBufferAllocateInfo cai{};
    cai.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    cai.commandPool = pool;
    cai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    cai.commandBufferCount = 1;
    VkCommandBuffer cmd = VK_NULL_HANDLE;
    check(vkAllocateCommandBuffers(ctx.device, &cai, &cmd), "alloc cmd");
    VkCommandBufferBeginInfo begin{};
    begin.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    check(vkBeginCommandBuffer(cmd, &begin), "begin cmd");
    return cmd;
}

inline void submitOneShot(const Ctx& ctx, VkCommandBuffer cmd) {
    check(vkEndCommandBuffer(cmd), "end cmd");
    VkSubmitInfo submit{};
    submit.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submit.commandBufferCount = 1;
    submit.pCommandBuffers = &cmd;
    check(vkQueueSubmit(ctx.queue, 1, &submit, VK_NULL_HANDLE), "submit");
    check(vkQueueWaitIdle(ctx.queue), "wait idle");
}

inline void transitionToGeneral(const Ctx& ctx, VkCommandPool pool,
                                const Image& img) {
    VkCommandBuffer cmd = beginOneShot(ctx, pool);
    VkImageMemoryBarrier barrier{};
    barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barrier.dstAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
    barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    barrier.newLayout = VK_IMAGE_LAYOUT_GENERAL;
    barrier.srcQueueFamilyIndex = barrier.dstQueueFamilyIndex =
        VK_QUEUE_FAMILY_IGNORED;
    barrier.image = img.image;
    barrier.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                         VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 0, nullptr, 0,
                         nullptr, 1, &barrier);
    submitOneShot(ctx, cmd);
}

inline uint16_t floatToHalf(float v) {
    uint32_t f;
    std::memcpy(&f, &v, 4);
    const uint32_t s = (f >> 16) & 0x8000u;
    const int e = ((f >> 23) & 0xff) - 127 + 15;
    const uint32_t m = f & 0x7fffffu;
    if (e <= 0) {
        return (uint16_t)s;
    }
    if (e >= 31) {
        return (uint16_t)(s | 0x7bffu);
    }
    return (uint16_t)(s | ((uint32_t)e << 10) | (m >> 13));
}

// Uploads half pixels into a GENERAL RGBA16F image (leaves it GENERAL).
inline void uploadRgba16f(const Ctx& ctx, VkCommandPool pool, const Image& img,
                          uint32_t w, uint32_t h,
                          const std::vector<uint16_t>& halfPixels) {
    const VkDeviceSize bytes = halfPixels.size() * sizeof(uint16_t);
    VkBufferCreateInfo bi{};
    bi.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bi.size = bytes;
    bi.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
    bi.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    VkBuffer staging = VK_NULL_HANDLE;
    check(vkCreateBuffer(ctx.device, &bi, nullptr, &staging), "staging");
    VkMemoryRequirements req{};
    vkGetBufferMemoryRequirements(ctx.device, staging, &req);
    VkMemoryAllocateInfo ai{};
    ai.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    ai.allocationSize = req.size;
    ai.memoryTypeIndex = memoryType(ctx, req.memoryTypeBits,
                                    VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                                        VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    VkDeviceMemory memory = VK_NULL_HANDLE;
    check(vkAllocateMemory(ctx.device, &ai, nullptr, &memory), "staging mem");
    vkBindBufferMemory(ctx.device, staging, memory, 0);
    void* mapped = nullptr;
    vkMapMemory(ctx.device, memory, 0, bytes, 0, &mapped);
    std::memcpy(mapped, halfPixels.data(), (size_t)bytes);
    vkUnmapMemory(ctx.device, memory);
    VkCommandBuffer cmd = beginOneShot(ctx, pool);
    VkImageMemoryBarrier toDst{};
    toDst.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    toDst.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    toDst.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    toDst.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    toDst.srcQueueFamilyIndex = toDst.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    toDst.image = img.image;
    toDst.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                         VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0,
                         nullptr, 1, &toDst);
    VkBufferImageCopy copy{};
    copy.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
    copy.imageExtent = {w, h, 1};
    vkCmdCopyBufferToImage(cmd, staging, img.image,
                           VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copy);
    VkImageMemoryBarrier toGeneral{};
    toGeneral.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    toGeneral.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    toGeneral.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    toGeneral.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    toGeneral.newLayout = VK_IMAGE_LAYOUT_GENERAL;
    toGeneral.srcQueueFamilyIndex = toGeneral.dstQueueFamilyIndex =
        VK_QUEUE_FAMILY_IGNORED;
    toGeneral.image = img.image;
    toGeneral.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT,
                         VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 0, nullptr, 0,
                         nullptr, 1, &toGeneral);
    submitOneShot(ctx, cmd);
    vkDestroyBuffer(ctx.device, staging, nullptr);
    vkFreeMemory(ctx.device, memory, nullptr);
}

// Downloads a GENERAL RGBA8 image to host bytes (leaves it GENERAL).
inline std::vector<uint8_t> downloadRgba8(const Ctx& ctx, VkCommandPool pool,
                                          const Image& img, uint32_t w,
                                          uint32_t h) {
    const VkDeviceSize bytes = (VkDeviceSize)w * h * 4;
    VkBufferCreateInfo bi{};
    bi.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bi.size = bytes;
    bi.usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    bi.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    VkBuffer staging = VK_NULL_HANDLE;
    check(vkCreateBuffer(ctx.device, &bi, nullptr, &staging), "staging");
    VkMemoryRequirements req{};
    vkGetBufferMemoryRequirements(ctx.device, staging, &req);
    VkMemoryAllocateInfo ai{};
    ai.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    ai.allocationSize = req.size;
    ai.memoryTypeIndex = memoryType(ctx, req.memoryTypeBits,
                                    VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                                        VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    VkDeviceMemory memory = VK_NULL_HANDLE;
    check(vkAllocateMemory(ctx.device, &ai, nullptr, &memory), "staging mem");
    vkBindBufferMemory(ctx.device, staging, memory, 0);
    VkCommandBuffer cmd = beginOneShot(ctx, pool);
    VkImageMemoryBarrier toTransfer{};
    toTransfer.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    toTransfer.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
    toTransfer.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
    toTransfer.oldLayout = VK_IMAGE_LAYOUT_GENERAL;
    toTransfer.newLayout = VK_IMAGE_LAYOUT_GENERAL;
    toTransfer.srcQueueFamilyIndex = toTransfer.dstQueueFamilyIndex =
        VK_QUEUE_FAMILY_IGNORED;
    toTransfer.image = img.image;
    toTransfer.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                         VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0,
                         nullptr, 1, &toTransfer);
    VkBufferImageCopy copy{};
    copy.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
    copy.imageExtent = {w, h, 1};
    vkCmdCopyImageToBuffer(cmd, img.image, VK_IMAGE_LAYOUT_GENERAL, staging, 1,
                           &copy);
    submitOneShot(ctx, cmd);
    void* mapped = nullptr;
    vkMapMemory(ctx.device, memory, 0, bytes, 0, &mapped);
    std::vector<uint8_t> out((size_t)bytes);
    std::memcpy(out.data(), mapped, (size_t)bytes);
    vkUnmapMemory(ctx.device, memory);
    vkDestroyBuffer(ctx.device, staging, nullptr);
    vkFreeMemory(ctx.device, memory, nullptr);
    return out;
}

// Downloads a GENERAL RGBA16F image to host half words (leaves GENERAL).
inline std::vector<uint16_t> downloadRgba16f(const Ctx& ctx, VkCommandPool pool,
                                             const Image& img, uint32_t w,
                                             uint32_t h) {
    const VkDeviceSize bytes = (VkDeviceSize)w * h * 8;
    VkBufferCreateInfo bi{};
    bi.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bi.size = bytes;
    bi.usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    bi.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    VkBuffer staging = VK_NULL_HANDLE;
    check(vkCreateBuffer(ctx.device, &bi, nullptr, &staging), "staging");
    VkMemoryRequirements req{};
    vkGetBufferMemoryRequirements(ctx.device, staging, &req);
    VkMemoryAllocateInfo ai{};
    ai.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    ai.allocationSize = req.size;
    ai.memoryTypeIndex = memoryType(ctx, req.memoryTypeBits,
                                    VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                                        VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    VkDeviceMemory memory = VK_NULL_HANDLE;
    check(vkAllocateMemory(ctx.device, &ai, nullptr, &memory), "staging mem");
    vkBindBufferMemory(ctx.device, staging, memory, 0);
    VkCommandBuffer cmd = beginOneShot(ctx, pool);
    VkImageMemoryBarrier toTransfer{};
    toTransfer.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    toTransfer.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
    toTransfer.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
    toTransfer.oldLayout = VK_IMAGE_LAYOUT_GENERAL;
    toTransfer.newLayout = VK_IMAGE_LAYOUT_GENERAL;
    toTransfer.srcQueueFamilyIndex = toTransfer.dstQueueFamilyIndex =
        VK_QUEUE_FAMILY_IGNORED;
    toTransfer.image = img.image;
    toTransfer.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                         VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0,
                         nullptr, 1, &toTransfer);
    VkBufferImageCopy copy{};
    copy.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
    copy.imageExtent = {w, h, 1};
    vkCmdCopyImageToBuffer(cmd, img.image, VK_IMAGE_LAYOUT_GENERAL, staging, 1,
                           &copy);
    submitOneShot(ctx, cmd);
    void* mapped = nullptr;
    vkMapMemory(ctx.device, memory, 0, bytes, 0, &mapped);
    std::vector<uint16_t> out((size_t)w * h * 4);
    std::memcpy(out.data(), mapped, (size_t)bytes);
    vkUnmapMemory(ctx.device, memory);
    vkDestroyBuffer(ctx.device, staging, nullptr);
    vkFreeMemory(ctx.device, memory, nullptr);
    return out;
}

inline std::vector<uint8_t> loadFileBytes(const std::string& path) {    FILE* f = std::fopen(path.c_str(), "rb");
    if (!f) {
        throw std::runtime_error("spektra_test: cannot open " + path);
    }
    std::fseek(f, 0, SEEK_END);
    const long size = std::ftell(f);
    std::fseek(f, 0, SEEK_SET);
    std::vector<uint8_t> data((size_t)size);
    if (size > 0 &&
        std::fread(data.data(), 1, data.size(), f) != data.size()) {
        std::fclose(f);
        throw std::runtime_error("spektra_test: short read " + path);
    }
    std::fclose(f);
    return data;
}

}  // namespace spektra_test
