#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

#include "tonemap/TonemapEngine.h"
#include "vk_test_common.hpp"

namespace {
using namespace vktest;

std::vector<uint8_t> readFile(const std::string& path) {
    std::ifstream file(path, std::ios::binary);
    if (!file) throw std::runtime_error("cannot open " + path);
    return {std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
}

void writeBuffer(VkDevice device, const Buf& buffer, const std::string& path) {
    void* mapped = nullptr;
    ck(vkMapMemory(device, buffer.m, 0, buffer.size, 0, &mapped), "map readback");
    std::ofstream out(path, std::ios::binary);
    out.write(static_cast<const char*>(mapped), static_cast<std::streamsize>(buffer.size));
    vkUnmapMemory(device, buffer.m);
    if (!out) throw std::runtime_error("cannot write " + path);
}

void imageBarrier(VkCommandBuffer cmd, VkImage image, VkImageLayout oldLayout, VkImageLayout nextLayout,
                  VkAccessFlags source, VkAccessFlags destination, VkPipelineStageFlags from,
                  VkPipelineStageFlags to) {
    VkImageMemoryBarrier barrier{};
    barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barrier.oldLayout = oldLayout;
    barrier.newLayout = nextLayout;
    barrier.srcAccessMask = source;
    barrier.dstAccessMask = destination;
    barrier.image = image;
    barrier.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    vkCmdPipelineBarrier(cmd, from, to, 0, 0, nullptr, 0, nullptr, 1, &barrier);
}

std::vector<double> runPass(const Ctx& ctx, VkCommandBuffer cmd, VkFence fence, VkQueryPool queries,
                            tonemap::TonemapEngine& engine, bool fused,
                            const tonemap::TonemapRawRecordInfo& raw,
                            const tonemap::TonemapRecordInfo& regular,
                            const Img& output, const Img& monitor, const Buf& readback,
                            const Buf& monitorReadback, uint32_t width, uint32_t height, int repeats) {
    std::vector<double> times;
    for (int iteration = 0; iteration < repeats + 2; ++iteration) {
        ck(vkResetCommandBuffer(cmd, 0), "reset command");
        VkCommandBufferBeginInfo begin{};
        begin.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        ck(vkBeginCommandBuffer(cmd, &begin), "begin command");
        const bool first = iteration == 0;
        imageBarrier(cmd, output.i, first ? VK_IMAGE_LAYOUT_UNDEFINED : VK_IMAGE_LAYOUT_GENERAL,
                     VK_IMAGE_LAYOUT_GENERAL, first ? 0 : VK_ACCESS_TRANSFER_READ_BIT,
                     VK_ACCESS_SHADER_WRITE_BIT, first ? VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT
                                                       : VK_PIPELINE_STAGE_TRANSFER_BIT,
                     VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT);
        if (fused) {
            imageBarrier(cmd, monitor.i, first ? VK_IMAGE_LAYOUT_UNDEFINED : VK_IMAGE_LAYOUT_GENERAL,
                         VK_IMAGE_LAYOUT_GENERAL, first ? 0 : VK_ACCESS_TRANSFER_READ_BIT,
                         VK_ACCESS_SHADER_WRITE_BIT, first ? VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT
                                                           : VK_PIPELINE_STAGE_TRANSFER_BIT,
                         VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT);
        }
        vkCmdResetQueryPool(cmd, queries, 0, 2);
        vkCmdWriteTimestamp(cmd, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, queries, 0);
        if (fused) engine.recordRaw(raw);
        else engine.record(regular);
        vkCmdWriteTimestamp(cmd, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, queries, 1);
        if (iteration == repeats + 1) {
            imageBarrier(cmd, output.i, VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_GENERAL,
                         VK_ACCESS_SHADER_WRITE_BIT, VK_ACCESS_TRANSFER_READ_BIT,
                         VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT);
            VkBufferImageCopy copy{};
            copy.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
            copy.imageExtent = {width, height, 1};
            vkCmdCopyImageToBuffer(cmd, output.i, VK_IMAGE_LAYOUT_GENERAL, readback.b, 1, &copy);
            if (fused) {
                imageBarrier(cmd, monitor.i, VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_GENERAL,
                             VK_ACCESS_SHADER_WRITE_BIT, VK_ACCESS_TRANSFER_READ_BIT,
                             VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT);
                vkCmdCopyImageToBuffer(cmd, monitor.i, VK_IMAGE_LAYOUT_GENERAL, monitorReadback.b, 1, &copy);
            }
        }
        ck(vkEndCommandBuffer(cmd), "end command");
        VkSubmitInfo submit{};
        submit.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        submit.commandBufferCount = 1;
        submit.pCommandBuffers = &cmd;
        ck(vkQueueSubmit(ctx.q, 1, &submit, fence), "submit");
        ck(vkWaitForFences(ctx.dev, 1, &fence, VK_TRUE, UINT64_MAX), "wait fence");
        uint64_t query[2]{};
        ck(vkGetQueryPoolResults(ctx.dev, queries, 0, 2, sizeof(query), query, sizeof(uint64_t),
                                 VK_QUERY_RESULT_64_BIT | VK_QUERY_RESULT_WAIT_BIT), "timestamp result");
        if (iteration >= 2)
            times.push_back(double(query[1] - query[0]) * ctx.prop.limits.timestampPeriod / 1e6);
        ck(vkResetFences(ctx.dev, 1, &fence), "reset fence");
    }
    std::sort(times.begin(), times.end());
    return times;
}

int run(int argc, char** argv) {
    if (argc != 21 && argc != 22) {
        std::cerr << "usage: fused fused.spv tone.spv raw.u16 linear.rgba16f output-prefix "
                     "rawW rawH outW outH white cfa blackR blackG1 blackG2 blackB "
                     "wbR wbG1 wbG2 wbB repeats [monitorEnabled]\n";
        return 2;
    }
    const auto fusedSpv = readFile(argv[1]);
    const auto regularSpv = readFile(argv[2]);
    const auto rawData = readFile(argv[3]);
    const auto linearData = readFile(argv[4]);
    const std::string prefix = argv[5];
    const uint32_t rawW = std::stoul(argv[6]), rawH = std::stoul(argv[7]);
    const uint32_t width = std::stoul(argv[8]), height = std::stoul(argv[9]);
    const float white = std::stof(argv[10]);
    const uint32_t cfa = std::stoul(argv[11]);
    const int repeats = std::max(1, std::stoi(argv[20]));
    if (rawData.size() != size_t(rawW) * rawH * 2u ||
        linearData.size() != size_t(width) * height * 8u ||
        (fusedSpv.size() & 3u) || (regularSpv.size() & 3u))
        throw std::runtime_error("input sizes invalid");

    Ctx ctx = vktest::ctx();
    const VkDevice device = ctx.dev;
    Buf rawBuffer = mkBuf(ctx.pd, device, rawData.size(), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
                          VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    Buf linearUpload = mkBuf(ctx.pd, device, linearData.size(), VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                             VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    Buf fusedReadback = mkBuf(ctx.pd, device, linearData.size(), VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                              VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    Buf baselineReadback = mkBuf(ctx.pd, device, linearData.size(), VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                                 VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    Buf monitorReadback = mkBuf(ctx.pd, device, size_t(width) * height * 4u,
                                VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                                VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    auto upload = [&](const Buf& buffer, const std::vector<uint8_t>& bytes) {
        void* mapped = nullptr;
        ck(vkMapMemory(device, buffer.m, 0, bytes.size(), 0, &mapped), "map upload");
        std::memcpy(mapped, bytes.data(), bytes.size());
        vkUnmapMemory(device, buffer.m);
    };
    upload(rawBuffer, rawData);
    upload(linearUpload, linearData);
    Img dummy = mkImg(ctx.pd, device, 1, 1, VK_FORMAT_R16_UINT, VK_IMAGE_USAGE_STORAGE_BIT);
    Img linear = mkImg(ctx.pd, device, width, height, VK_FORMAT_R16G16B16A16_SFLOAT,
                       VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT);
    Img fusedOutput = mkImg(ctx.pd, device, width, height, VK_FORMAT_R16G16B16A16_SFLOAT,
                            VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT);
    Img regularOutput = mkImg(ctx.pd, device, width, height, VK_FORMAT_R16G16B16A16_SFLOAT,
                              VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT);
    Img monitor = mkImg(ctx.pd, device, width, height, VK_FORMAT_R8G8B8A8_UNORM,
                         VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT);

    VkCommandPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    poolInfo.queueFamilyIndex = ctx.qf;
    poolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    VkCommandPool pool = VK_NULL_HANDLE;
    ck(vkCreateCommandPool(device, &poolInfo, nullptr, &pool), "command pool");
    VkCommandBufferAllocateInfo alloc{};
    alloc.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    alloc.commandPool = pool;
    alloc.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    alloc.commandBufferCount = 1;
    VkCommandBuffer cmd = VK_NULL_HANDLE;
    ck(vkAllocateCommandBuffers(device, &alloc, &cmd), "command buffer");
    VkFenceCreateInfo fenceInfo{};
    fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    VkFence fence = VK_NULL_HANDLE;
    ck(vkCreateFence(device, &fenceInfo, nullptr, &fence), "fence");
    VkQueryPoolCreateInfo queryInfo{};
    queryInfo.sType = VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO;
    queryInfo.queryType = VK_QUERY_TYPE_TIMESTAMP;
    queryInfo.queryCount = 2;
    VkQueryPool queries = VK_NULL_HANDLE;
    ck(vkCreateQueryPool(device, &queryInfo, nullptr, &queries), "query pool");

    VkCommandBufferBeginInfo uploadBegin{};
    uploadBegin.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    ck(vkBeginCommandBuffer(cmd, &uploadBegin), "begin upload");
    imageBarrier(cmd, dummy.i, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL, 0,
                 VK_ACCESS_SHADER_READ_BIT, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                 VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT);
    imageBarrier(cmd, linear.i, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 0,
                 VK_ACCESS_TRANSFER_WRITE_BIT, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                 VK_PIPELINE_STAGE_TRANSFER_BIT);
    VkBufferImageCopy uploadCopy{};
    uploadCopy.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
    uploadCopy.imageExtent = {width, height, 1};
    vkCmdCopyBufferToImage(cmd, linearUpload.b, linear.i, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                           1, &uploadCopy);
    imageBarrier(cmd, linear.i, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_GENERAL,
                 VK_ACCESS_TRANSFER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT,
                 VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT);
    ck(vkEndCommandBuffer(cmd), "end upload");
    VkSubmitInfo uploadSubmit{};
    uploadSubmit.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    uploadSubmit.commandBufferCount = 1;
    uploadSubmit.pCommandBuffers = &cmd;
    ck(vkQueueSubmit(ctx.q, 1, &uploadSubmit, fence), "submit upload");
    ck(vkWaitForFences(device, 1, &fence, VK_TRUE, UINT64_MAX), "wait upload");
    ck(vkResetFences(device, 1, &fence), "reset upload fence");

    const float cameraToAp1[9] = {1, 0, 0, 0, 1, 0, 0, 0, 1};
    tonemap::TonemapCreateInfo fusedInfo{};
    fusedInfo.context = {ctx.pd, device, nullptr};
    fusedInfo.shaderSpirv = reinterpret_cast<const uint32_t*>(fusedSpv.data());
    fusedInfo.shaderSpirvBytes = fusedSpv.size();
    fusedInfo.outputFormat = VK_FORMAT_R16G16B16A16_SFLOAT;
    fusedInfo.rawVideoInput = true;
    fusedInfo.maxFramesInFlight = 1;
    tonemap::TonemapRawRecordInfo rawRecord{};
    rawRecord.commandBuffer = cmd;
    rawRecord.raw.rawImageView = dummy.v;
    rawRecord.raw.rawBuffer = rawBuffer.b;
    rawRecord.raw.rawStridePixels = rawW;
    rawRecord.raw.width = rawW;
    rawRecord.raw.height = rawH;
    rawRecord.raw.cfa = cfa;
    rawRecord.raw.white = white;
    for (int i = 0; i < 4; ++i) {
        rawRecord.raw.black[i] = std::stof(argv[12 + i]);
        rawRecord.raw.whiteBalance[i] = std::stof(argv[16 + i]);
    }
    rawRecord.output = {fusedOutput.v, VK_FORMAT_R16G16B16A16_SFLOAT,
                        VK_IMAGE_LAYOUT_GENERAL, width, height};
    rawRecord.monitor = {monitor.v, VK_FORMAT_R8G8B8A8_UNORM,
                         VK_IMAGE_LAYOUT_GENERAL, width, height};
    rawRecord.monitorEnabled = argc == 21 || std::stoi(argv[21]) != 0;
    rawRecord.cameraToWorkingColumnMajor3x3 = cameraToAp1;
    tonemap::TonemapRecordInfo ordinary{};
    ordinary.commandBuffer = cmd;
    ordinary.input = {linear.v, VK_FORMAT_R16G16B16A16_SFLOAT,
                      VK_IMAGE_LAYOUT_GENERAL, width, height};
    ordinary.output = {regularOutput.v, VK_FORMAT_R16G16B16A16_SFLOAT,
                       VK_IMAGE_LAYOUT_GENERAL, width, height};
    ordinary.cameraToWorkingColumnMajor3x3 = cameraToAp1;

    std::vector<double> fusedTimes, regularTimes;
    {
        tonemap::TonemapEngine fusedEngine(fusedInfo);
        tonemap::TonemapCreateInfo regularInfo = fusedInfo;
        regularInfo.shaderSpirv = reinterpret_cast<const uint32_t*>(regularSpv.data());
        regularInfo.shaderSpirvBytes = regularSpv.size();
        regularInfo.rawVideoInput = false;
        regularInfo.workgroupSizeY = 16;
        tonemap::TonemapEngine regularEngine(regularInfo);
        fusedTimes = runPass(ctx, cmd, fence, queries, fusedEngine, true, rawRecord, ordinary,
                             fusedOutput, monitor, fusedReadback, monitorReadback,
                             width, height, repeats);
        regularTimes = runPass(ctx, cmd, fence, queries, regularEngine, false, rawRecord, ordinary,
                               regularOutput, monitor, baselineReadback, monitorReadback,
                               width, height, repeats);
    }
    writeBuffer(device, fusedReadback, prefix + "_fused.rgba16f");
    writeBuffer(device, baselineReadback, prefix + "_baseline.rgba16f");
    writeBuffer(device, monitorReadback, prefix + "_monitor.rgba8");
    std::cout << "{\"gpu\":\"" << ctx.prop.deviceName << "\",\"size\":[" << width << ',' << height
              << "],\"samples\":" << repeats << ",\"fusedMedianMs\":" << fusedTimes[fusedTimes.size()/2]
              << ",\"fusedP95Ms\":" << fusedTimes[std::min(fusedTimes.size()-1, size_t(fusedTimes.size()*0.95))]
              << ",\"toneOnlyMedianMs\":" << regularTimes[regularTimes.size()/2] << "}\n";

    vkDestroyQueryPool(device, queries, nullptr);
    vkDestroyFence(device, fence, nullptr);
    vkDestroyCommandPool(device, pool, nullptr);
    delImg(device, monitor); delImg(device, regularOutput); delImg(device, fusedOutput);
    delImg(device, linear); delImg(device, dummy);
    delBuf(device, monitorReadback); delBuf(device, baselineReadback); delBuf(device, fusedReadback);
    delBuf(device, linearUpload); delBuf(device, rawBuffer);
    delCtx(ctx);
    return 0;
}
}  // namespace

int main(int argc, char** argv) {
    try { return run(argc, argv); }
    catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
