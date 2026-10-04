// GPU correctness gate: runs the production RawSharpness scorer on synthetic
// Bayer bursts and checks bit-level agreement with the host reference plus
// the production selector. Requires a Vulkan device (MoltenVK on macOS).
//
// Build with the module (RAW_SHARPNESS_BUILD_VULKAN=ON,
// RAW_SHARPNESS_BUILD_TESTS=ON) and run via ctest or directly.

#include "raw_sharpness/raw_sharpness.hpp"
#include "raw_sharpness/raw_sharpness_types.hpp"

#include <vulkan/vulkan.h>

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

constexpr std::uint32_t kWidth = 512;
constexpr std::uint32_t kHeight = 512;

void require(bool condition, const std::string& message) {
    if (!condition) throw std::runtime_error("RAW_SHARPNESS_VALIDATION_FAIL " + message);
    std::printf("PASS %s\n", message.c_str());
}

void vkCheck(VkResult result, const char* operation) {
    if (result != VK_SUCCESS) throw std::runtime_error(operation);
}

std::uint32_t memoryType(VkPhysicalDevice physical, std::uint32_t bits, VkMemoryPropertyFlags flags) {
    VkPhysicalDeviceMemoryProperties properties{};
    vkGetPhysicalDeviceMemoryProperties(physical, &properties);
    for (std::uint32_t i = 0; i < properties.memoryTypeCount; ++i) {
        if ((bits & (1u << i)) && (properties.memoryTypes[i].propertyFlags & flags) == flags) return i;
    }
    throw std::runtime_error("no matching memory type");
}

struct Context {
    VkInstance instance{};
    VkPhysicalDevice physical{};
    VkDevice device{};
    VkQueue queue{};
    std::uint32_t queueFamily = 0;
    VkCommandPool pool{};
    VkCommandBuffer command{};
    VkFence fence{};
};

Context makeContext() {
    Context context{};
    VkApplicationInfo app{};
    app.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    app.pApplicationName = "raw_sharpness_validation";
    app.apiVersion = VK_API_VERSION_1_1;
    VkInstanceCreateInfo instanceInfo{};
    instanceInfo.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    instanceInfo.pApplicationInfo = &app;
#ifdef __APPLE__
    instanceInfo.flags |= VK_INSTANCE_CREATE_ENUMERATE_PORTABILITY_BIT_KHR;
    const char* instanceExtensions[] = {"VK_KHR_portability_enumeration"};
    instanceInfo.enabledExtensionCount = 1;
    instanceInfo.ppEnabledExtensionNames = instanceExtensions;
#endif
    vkCheck(vkCreateInstance(&instanceInfo, nullptr, &context.instance), "instance");
    std::uint32_t deviceCount = 0;
    vkCheck(vkEnumeratePhysicalDevices(context.instance, &deviceCount, nullptr), "enumerate");
    require(deviceCount > 0, "vulkan device present");
    std::vector<VkPhysicalDevice> devices(deviceCount);
    vkEnumeratePhysicalDevices(context.instance, &deviceCount, devices.data());
    context.physical = devices.front();
    std::uint32_t queueCount = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(context.physical, &queueCount, nullptr);
    std::vector<VkQueueFamilyProperties> queues(queueCount);
    vkGetPhysicalDeviceQueueFamilyProperties(context.physical, &queueCount, queues.data());
    context.queueFamily = UINT32_MAX;
    for (std::uint32_t i = 0; i < queueCount; ++i) {
        if (queues[i].queueFlags & VK_QUEUE_COMPUTE_BIT) {
            context.queueFamily = i;
            break;
        }
    }
    require(context.queueFamily != UINT32_MAX, "compute queue present");
    const float priority = 1.0f;
    VkDeviceQueueCreateInfo queueInfo{};
    queueInfo.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
    queueInfo.queueFamilyIndex = context.queueFamily;
    queueInfo.queueCount = 1;
    queueInfo.pQueuePriorities = &priority;
    VkDeviceCreateInfo deviceInfo{};
    deviceInfo.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
    deviceInfo.queueCreateInfoCount = 1;
    deviceInfo.pQueueCreateInfos = &queueInfo;
#ifdef __APPLE__
    const char* deviceExtensions[] = {"VK_KHR_portability_subset"};
    deviceInfo.enabledExtensionCount = 1;
    deviceInfo.ppEnabledExtensionNames = deviceExtensions;
#endif
    vkCheck(vkCreateDevice(context.physical, &deviceInfo, nullptr, &context.device), "device");
    vkGetDeviceQueue(context.device, context.queueFamily, 0, &context.queue);
    VkCommandPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    poolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    poolInfo.queueFamilyIndex = context.queueFamily;
    vkCheck(vkCreateCommandPool(context.device, &poolInfo, nullptr, &context.pool), "command pool");
    VkCommandBufferAllocateInfo bufferInfo{};
    bufferInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    bufferInfo.commandPool = context.pool;
    bufferInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    bufferInfo.commandBufferCount = 1;
    vkCheck(vkAllocateCommandBuffers(context.device, &bufferInfo, &context.command), "command buffer");
    VkFenceCreateInfo fenceInfo{};
    fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    vkCheck(vkCreateFence(context.device, &fenceInfo, nullptr, &context.fence), "fence");
    return context;
}

void destroyContext(Context& context) {
    if (context.fence) vkDestroyFence(context.device, context.fence, nullptr);
    if (context.pool) vkDestroyCommandPool(context.device, context.pool, nullptr);
    if (context.device) {
        vkDeviceWaitIdle(context.device);
        vkDestroyDevice(context.device, nullptr);
    }
    if (context.instance) vkDestroyInstance(context.instance, nullptr);
    context = {};
}

template <typename F>
void submit(Context& context, F&& record) {
    vkCheck(vkResetCommandBuffer(context.command, 0), "reset command");
    VkCommandBufferBeginInfo begin{};
    begin.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkCheck(vkBeginCommandBuffer(context.command, &begin), "begin command");
    record(context.command);
    vkCheck(vkEndCommandBuffer(context.command), "end command");
    VkSubmitInfo submit{};
    submit.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submit.commandBufferCount = 1;
    submit.pCommandBuffers = &context.command;
    vkCheck(vkResetFences(context.device, 1, &context.fence), "reset fence");
    vkCheck(vkQueueSubmit(context.queue, 1, &submit, context.fence), "queue submit");
    vkCheck(vkWaitForFences(context.device, 1, &context.fence, VK_TRUE, UINT64_MAX), "fence wait");
}

void imageBarrier(VkCommandBuffer command, VkImage image, VkImageLayout oldLayout, VkImageLayout newLayout,
                  VkAccessFlags srcAccess, VkAccessFlags dstAccess, VkPipelineStageFlags srcStage,
                  VkPipelineStageFlags dstStage) {
    VkImageMemoryBarrier barrier{};
    barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barrier.srcAccessMask = srcAccess;
    barrier.dstAccessMask = dstAccess;
    barrier.oldLayout = oldLayout;
    barrier.newLayout = newLayout;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = image;
    barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    barrier.subresourceRange.levelCount = 1;
    barrier.subresourceRange.layerCount = 1;
    vkCmdPipelineBarrier(command, srcStage, dstStage, 0, 0, nullptr, 0, nullptr, 1, &barrier);
}

struct TestImage {
    VkImage image{};
    VkImageView view{};
    VkDeviceMemory memory{};
};

// Uploads host RAW16 into a device-local R16_UINT STORAGE image left in
// GENERAL, mirroring the multiframe ring's steady state.
TestImage uploadFrame(Context& context, const std::uint16_t* pixels) {
    TestImage out{};
    VkImageCreateInfo imageInfo{};
    imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    imageInfo.imageType = VK_IMAGE_TYPE_2D;
    imageInfo.format = VK_FORMAT_R16_UINT;
    imageInfo.extent = {kWidth, kHeight, 1};
    imageInfo.mipLevels = 1;
    imageInfo.arrayLayers = 1;
    imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
    imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
    imageInfo.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_STORAGE_BIT;
    imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    vkCheck(vkCreateImage(context.device, &imageInfo, nullptr, &out.image), "image");
    VkMemoryRequirements requirements{};
    vkGetImageMemoryRequirements(context.device, out.image, &requirements);
    VkMemoryAllocateInfo allocation{};
    allocation.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocation.allocationSize = requirements.size;
    allocation.memoryTypeIndex =
        memoryType(context.physical, requirements.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    vkCheck(vkAllocateMemory(context.device, &allocation, nullptr, &out.memory), "image memory");
    vkCheck(vkBindImageMemory(context.device, out.image, out.memory, 0), "bind image");
    VkImageViewCreateInfo viewInfo{};
    viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    viewInfo.image = out.image;
    viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
    viewInfo.format = VK_FORMAT_R16_UINT;
    viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    viewInfo.subresourceRange.levelCount = 1;
    viewInfo.subresourceRange.layerCount = 1;
    vkCheck(vkCreateImageView(context.device, &viewInfo, nullptr, &out.view), "image view");

    const VkDeviceSize bytes = VkDeviceSize(kWidth) * kHeight * 2u;
    VkBufferCreateInfo bufferInfo{};
    bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufferInfo.size = bytes;
    bufferInfo.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
    bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    VkBuffer staging{};
    vkCheck(vkCreateBuffer(context.device, &bufferInfo, nullptr, &staging), "staging");
    VkMemoryRequirements stagingRequirements{};
    vkGetBufferMemoryRequirements(context.device, staging, &stagingRequirements);
    VkMemoryAllocateInfo stagingAllocation{};
    stagingAllocation.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    stagingAllocation.allocationSize = stagingRequirements.size;
    stagingAllocation.memoryTypeIndex =
        memoryType(context.physical, stagingRequirements.memoryTypeBits,
                   VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    VkDeviceMemory stagingMemory{};
    vkCheck(vkAllocateMemory(context.device, &stagingAllocation, nullptr, &stagingMemory), "staging memory");
    vkCheck(vkBindBufferMemory(context.device, staging, stagingMemory, 0), "bind staging");
    void* mapped = nullptr;
    vkCheck(vkMapMemory(context.device, stagingMemory, 0, bytes, 0, &mapped), "map staging");
    std::memcpy(mapped, pixels, static_cast<std::size_t>(bytes));
    vkUnmapMemory(context.device, stagingMemory);
    submit(context, [&](VkCommandBuffer command) {
        imageBarrier(command, out.image, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 0,
                     VK_ACCESS_TRANSFER_WRITE_BIT, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                     VK_PIPELINE_STAGE_TRANSFER_BIT);
        VkBufferImageCopy copy{};
        copy.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        copy.imageSubresource.layerCount = 1;
        copy.imageExtent = {kWidth, kHeight, 1};
        vkCmdCopyBufferToImage(command, staging, out.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copy);
        imageBarrier(command, out.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_GENERAL,
                     VK_ACCESS_TRANSFER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
                     VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT);
    });
    vkDestroyBuffer(context.device, staging, nullptr);
    vkFreeMemory(context.device, stagingMemory, nullptr);
    return out;
}

}  // namespace

int main() {
    try {
        Context context = makeContext();
        VkPhysicalDeviceProperties properties{};
        vkGetPhysicalDeviceProperties(context.physical, &properties);
        std::printf("device=%s\n", properties.deviceName);

        std::vector<std::uint16_t> sharp(kWidth * kHeight);
        std::vector<std::uint16_t> blur(kWidth * kHeight);
        std::vector<std::uint16_t> flat(kWidth * kHeight, 2000);
        std::vector<std::uint16_t> saturated(kWidth * kHeight, 4000);
        for (std::uint32_t y = 0; y < kHeight; ++y) {
            for (std::uint32_t x = 0; x < kWidth; ++x) {
                const std::uint16_t v = (((x / 2u) + (y / 2u)) & 1u) ? 4000 : 1000;
                sharp[y * kWidth + x] = v;
                blur[y * kWidth + x] = static_cast<std::uint16_t>(2500 + (int(v) - 2500) / 4);
            }
        }
        const std::uint16_t* hosts[4] = {sharp.data(), blur.data(), flat.data(), saturated.data()};
        const float whites[4] = {65535.f, 65535.f, 65535.f, 3000.f};

        std::vector<TestImage> images;
        std::vector<raw_sharpness::SharpnessFrame> frames;
        for (int i = 0; i < 4; ++i) {
            images.push_back(uploadFrame(context, hosts[i]));
            raw_sharpness::SharpnessFrame frame{};
            frame.image = images.back().image;
            frame.view = images.back().view;
            frame.width = kWidth;
            frame.height = kHeight;
            frame.pattern = raw_sharpness::BayerPattern::RGGB;
            frame.whiteLevel = whites[i];
            frames.push_back(frame);
        }

        raw_sharpness::RawSharpness scorer;
        raw_sharpness::RawSharpnessCreateInfo createInfo{};
        createInfo.physicalDevice = context.physical;
        createInfo.device = context.device;
        createInfo.queueFamily = context.queueFamily;
        scorer.initialize(createInfo, [&](const VkSubmitInfo& info, VkFence fence) {
            vkCheck(vkQueueSubmit(context.queue, 1, &info, fence), "sharpness submit");
        });
        require(scorer.ready(), "scorer initializes");

        const auto gpu = scorer.score(frames);
        std::vector<float> cpu;
        for (int i = 0; i < 4; ++i) {
            cpu.push_back(raw_sharpness::reference::scoreCpu(reinterpret_cast<const std::uint8_t*>(hosts[i]),
                                                             kWidth, kHeight,
                                                             raw_sharpness::BayerPattern::RGGB, whites[i]));
        }
        for (int i = 0; i < 4; ++i) {
            std::printf("frame=%d gpu=%.6f cpu=%.6f\n", i, gpu[i], cpu[i]);
        }

        require(gpu.size() == 4, "four scores returned");
        require(gpu[0] > gpu[1] && gpu[1] > 0.f, "gpu ordering sharp > blur > 0");
        require(gpu[2] == 0.f, "gpu flat field scores zero");
        require(gpu[3] == 0.f, "gpu saturated field scores zero");
        for (int i = 0; i < 2; ++i) {
            const double rel = std::fabs(gpu[i] - cpu[i]) / std::max(1e-6, double(cpu[i]));
            char message[128];
            std::snprintf(message, sizeof(message), "gpu/cpu agreement frame=%d rel=%.4f", i, rel);
            require(rel < 0.05, message);
        }

        // Burst order puts the sharp frame away from the middle (middle=2):
        // the production selector must still find it.
        require(raw_sharpness::reference::selectSharpest(gpu, 2) == 0, "selector picks sharp frame at index 0");

        bool threw = false;
        try {
            scorer.score({frames.front()});
        } catch (...) {
            threw = true;
        }
        require(threw, "single frame rejected");
        threw = false;
        try {
            scorer.score(std::vector<raw_sharpness::SharpnessFrame>(
                raw_sharpness::RawSharpness::kMaxFrames + 1, frames.front()));
        } catch (...) {
            threw = true;
        }
        require(threw, "oversized burst rejected");

        for (auto& image : images) {
            vkDestroyImageView(context.device, image.view, nullptr);
            vkDestroyImage(context.device, image.image, nullptr);
            vkFreeMemory(context.device, image.memory, nullptr);
        }
        scorer.reset();
        destroyContext(context);
        std::printf("RAW_SHARPNESS_VULKAN_VALIDATION_OK\n");
        return 0;
    } catch (const std::exception& e) {
        std::fprintf(stderr, "ERROR %s\n", e.what());
        return 1;
    }
}
