#include "rawr/zsl_ring/ZslRing.h"

#include <algorithm>
#include <cstring>
#include <stdexcept>
#include <string>
#include <utility>

namespace rawr::zsl_ring {
namespace {
void check(VkResult r, const char* what) {
    if (r != VK_SUCCESS) throw std::runtime_error(std::string(what) + " VkResult=" + std::to_string(r));
}
}  // namespace

ZslRing::ZslRing(VkPhysicalDevice physical, VkDevice device, std::uint32_t queueFamily, Submit submit,
                 std::size_t maxFrames)
    : physical_(physical),
      device_(device),
      queueFamily_(queueFamily),
      submit_(std::move(submit)),
      maxFrames_(maxFrames) {
    if (!physical_ || !device_ || !submit_ || maxFrames_ == 0)
        throw std::invalid_argument("zsl_ring: invalid Vulkan configuration");
    VkCommandPoolCreateInfo ci{};
    ci.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    ci.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    ci.queueFamilyIndex = queueFamily_;
    check(vkCreateCommandPool(device_, &ci, nullptr, &commandPool_), "zsl_ring command pool");
}

ZslRing::~ZslRing() {
    clear();
    if (commandPool_) vkDestroyCommandPool(device_, commandPool_, nullptr);
}

std::uint32_t ZslRing::memoryType(std::uint32_t bits, VkMemoryPropertyFlags flags) const {
    VkPhysicalDeviceMemoryProperties p{};
    vkGetPhysicalDeviceMemoryProperties(physical_, &p);
    for (std::uint32_t i = 0; i < p.memoryTypeCount; ++i) {
        if ((bits & (1u << i)) && (p.memoryTypes[i].propertyFlags & flags) == flags) return i;
    }
    throw std::runtime_error("zsl_ring: no matching memory type");
}

ZslRing::Buffer ZslRing::makeBuffer(VkDeviceSize bytes, VkBufferUsageFlags usage, VkMemoryPropertyFlags properties) {
    Buffer b{};
    b.bytes = bytes;
    VkBufferCreateInfo ci{};
    ci.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    ci.size = bytes;
    ci.usage = usage;
    ci.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    check(vkCreateBuffer(device_, &ci, nullptr, &b.buffer), "zsl_ring buffer");
    VkMemoryRequirements req{};
    vkGetBufferMemoryRequirements(device_, b.buffer, &req);
    VkMemoryAllocateInfo ai{};
    ai.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    ai.allocationSize = req.size;
    ai.memoryTypeIndex = memoryType(req.memoryTypeBits, properties);
    check(vkAllocateMemory(device_, &ai, nullptr, &b.memory), "zsl_ring memory");
    check(vkBindBufferMemory(device_, b.buffer, b.memory, 0), "zsl_ring bind");
    return b;
}

void ZslRing::destroyBuffer(Buffer& b) noexcept {
    if (b.buffer) vkDestroyBuffer(device_, b.buffer, nullptr);
    if (b.memory) vkFreeMemory(device_, b.memory, nullptr);
    b = {};
}

void ZslRing::destroyEntry(Entry& e) noexcept {
    if (e.fence) {
        (void)vkWaitForFences(device_, 1, &e.fence, VK_TRUE, UINT64_MAX);
        vkDestroyFence(device_, e.fence, nullptr);
    }
    if (e.command) vkFreeCommandBuffers(device_, commandPool_, 1, &e.command);
    destroyBuffer(e.packet);
    e = {};
}

void ZslRing::retireCompleted() {
    for (auto& e : entries_) {
        if (e.ready || !e.fence) continue;
        const VkResult r = vkGetFenceStatus(device_, e.fence);
        if (r == VK_SUCCESS)
            e.ready = true;
        else if (r != VK_NOT_READY)
            check(r, "zsl_ring fence status");
    }
}

std::optional<PacketRef> ZslRing::push(FrameId frameId, std::uint64_t timestampNs, const PacketSource& s) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!s.meta || !s.sizes || !s.offsets || !s.payload || s.streams == 0 || s.tableBytes == 0) return std::nullopt;
    if (std::any_of(entries_.begin(), entries_.end(), [frameId](const Entry& e) { return e.ref.frameId == frameId; }))
        return std::nullopt;
    const std::uint64_t total = s.tableBytes * 3u + s.payloadBytes;
    if (total == 0) return std::nullopt;

    Buffer reusable{};
    VkCommandBuffer reusableCommand = VK_NULL_HANDLE;
    VkFence reusableFence = VK_NULL_HANDLE;
    if (entries_.size() >= maxFrames_) {
        retireCompleted();
        const auto it = std::find_if(entries_.begin(), entries_.end(),
                                     [](const ZslRing::Entry& e) { return e.ready && e.pins == 0; });
        if (it == entries_.end()) return std::nullopt;
        Entry old = std::move(*it);
        entries_.erase(it);
        reusableCommand = old.command;
        old.command = VK_NULL_HANDLE;
        reusableFence = old.fence;
        old.fence = VK_NULL_HANDLE;
        reusable = old.packet;
        old.packet = {};
        if (reusable.bytes < total) destroyBuffer(reusable);
    }

    Entry e{};
    e.ref = PacketRef{frameId,  timestampNs, s.streams,    s.width,        s.height,
                      s.tilesX, s.tilesY,    s.tableBytes, s.payloadBytes, total};
    e.packet = reusable.buffer ? reusable
                               : makeBuffer(total, VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                                            VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

    if (reusableCommand) {
        e.command = reusableCommand;
        check(vkResetCommandBuffer(e.command, 0), "zsl_ring reset command");
    } else {
        VkCommandBufferAllocateInfo cai{};
        cai.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        cai.commandPool = commandPool_;
        cai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        cai.commandBufferCount = 1;
        check(vkAllocateCommandBuffers(device_, &cai, &e.command), "zsl_ring alloc command");
    }
    if (reusableFence) {
        e.fence = reusableFence;
        check(vkResetFences(device_, 1, &e.fence), "zsl_ring reset fence");
    } else {
        VkFenceCreateInfo fi{};
        fi.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
        check(vkCreateFence(device_, &fi, nullptr, &e.fence), "zsl_ring fence");
    }

    VkCommandBufferBeginInfo bi{};
    bi.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    check(vkBeginCommandBuffer(e.command, &bi), "zsl_ring begin");
    const VkBuffer srcs[4] = {s.meta, s.sizes, s.offsets, s.payload};
    const VkDeviceSize sizes[4] = {s.tableBytes, s.tableBytes, s.tableBytes, s.payloadBytes};
    VkBufferMemoryBarrier srcBarriers[4]{};
    for (int i = 0; i < 4; ++i) {
        srcBarriers[i].sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
        srcBarriers[i].srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
        srcBarriers[i].dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
        srcBarriers[i].srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        srcBarriers[i].dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        srcBarriers[i].buffer = srcs[i];
        srcBarriers[i].offset = 0;
        srcBarriers[i].size = sizes[i];
    }
    vkCmdPipelineBarrier(e.command, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr,
                         4, srcBarriers, 0, nullptr);
    VkDeviceSize dstOffset = 0;
    for (int i = 0; i < 4; ++i) {
        if (sizes[i] == 0) continue;
        VkBufferCopy copy{0, dstOffset, sizes[i]};
        vkCmdCopyBuffer(e.command, srcs[i], e.packet.buffer, 1, &copy);
        dstOffset += sizes[i];
    }
    VkBufferMemoryBarrier post[5]{};
    post[0].sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
    post[0].srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    post[0].dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
    post[0].srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    post[0].dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    post[0].buffer = e.packet.buffer;
    post[0].offset = 0;
    post[0].size = VK_WHOLE_SIZE;
    for (int i = 0; i < 4; ++i) {
        post[i + 1].sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
        post[i + 1].srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
        post[i + 1].dstAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
        post[i + 1].srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        post[i + 1].dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        post[i + 1].buffer = srcs[i];
        post[i + 1].offset = 0;
        post[i + 1].size = sizes[i];
    }
    vkCmdPipelineBarrier(e.command, VK_PIPELINE_STAGE_TRANSFER_BIT,
                         VK_PIPELINE_STAGE_TRANSFER_BIT | VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 0, nullptr, 5, post,
                         0, nullptr);
    check(vkEndCommandBuffer(e.command), "zsl_ring end");
    VkSubmitInfo si{};
    si.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    si.commandBufferCount = 1;
    si.pCommandBuffers = &e.command;
    submit_(si, e.fence);
    entries_.push_back(std::move(e));
    return entries_.back().ref;
}

std::vector<PacketRef> ZslRing::snapshot() {
    std::lock_guard<std::mutex> lock(mutex_);
    retireCompleted();
    std::vector<PacketRef> out;
    for (auto& e : entries_) {
        if (!e.ready) continue;
        ++e.pins;
        out.push_back(e.ref);
    }
    return out;
}


void ZslRing::releaseSnapshot(const std::vector<PacketRef>& refs) noexcept {
    std::lock_guard<std::mutex> lock(mutex_);
    for (const auto& ref : refs) {
        auto it = std::find_if(entries_.begin(), entries_.end(),
                               [&](const Entry& e) { return e.ref.frameId == ref.frameId; });
        if (it != entries_.end() && it->pins) --it->pins;
    }
}

struct ZslRing::PacketReaderSession::Impl {
    ZslRing* ring = nullptr;
    ZslRing::Buffer staging{};
    void* mapped = nullptr;
    VkCommandPool pool = VK_NULL_HANDLE;
    VkCommandBuffer cmd = VK_NULL_HANDLE;
    VkFence fence = VK_NULL_HANDLE;
};

ZslRing::PacketReaderSession::PacketReaderSession(std::unique_ptr<Impl> impl) : impl_(std::move(impl)) {}

ZslRing::PacketReaderSession::~PacketReaderSession() {
    if (!impl_) return;
    auto& i = *impl_;
    if (i.fence) vkDestroyFence(i.ring->device_, i.fence, nullptr);
    if (i.pool) vkDestroyCommandPool(i.ring->device_, i.pool, nullptr);
    if (i.mapped) vkUnmapMemory(i.ring->device_, i.staging.memory);
    i.ring->destroyBuffer(i.staging);
}

std::unique_ptr<ZslRing::PacketReaderSession> ZslRing::makePacketReaderSession(const std::vector<PacketRef>& refs) {
    if (refs.empty()) return {};
    VkDeviceSize maxBytes = 0;
    for (const auto& ref : refs)
        maxBytes = std::max<VkDeviceSize>(maxBytes, static_cast<VkDeviceSize>(ref.packedBytes));
    if (!maxBytes) return {};
    auto impl = std::make_unique<PacketReaderSession::Impl>();
    impl->ring = this;
    try {
        impl->staging = makeBuffer(maxBytes, VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                                   VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
        check(vkMapMemory(device_, impl->staging.memory, 0, VK_WHOLE_SIZE, 0, &impl->mapped),
              "zsl_ring session map staging");
        VkCommandPoolCreateInfo pci{};
        pci.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
        pci.flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT | VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
        pci.queueFamilyIndex = queueFamily_;
        check(vkCreateCommandPool(device_, &pci, nullptr, &impl->pool), "zsl_ring session pool");
        VkCommandBufferAllocateInfo cai{};
        cai.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        cai.commandPool = impl->pool;
        cai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        cai.commandBufferCount = 1;
        check(vkAllocateCommandBuffers(device_, &cai, &impl->cmd), "zsl_ring session alloc");
        VkFenceCreateInfo fi{};
        fi.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
        check(vkCreateFence(device_, &fi, nullptr, &impl->fence), "zsl_ring session fence");
        return std::unique_ptr<PacketReaderSession>(new PacketReaderSession(std::move(impl)));
    } catch (...) {
        if (impl->fence) vkDestroyFence(device_, impl->fence, nullptr);
        if (impl->pool) vkDestroyCommandPool(device_, impl->pool, nullptr);
        if (impl->mapped) vkUnmapMemory(device_, impl->staging.memory);
        destroyBuffer(impl->staging);
        throw;
    }
}

bool ZslRing::PacketReaderSession::read(FrameId frameId, std::vector<std::uint8_t>& out) {
    if (!impl_) return false;
    auto& i = *impl_;
    VkBuffer sourceBuffer = VK_NULL_HANDLE;
    VkDeviceSize bytes = 0;
    {
        std::lock_guard<std::mutex> lock(i.ring->mutex_);
        i.ring->retireCompleted();
        auto it = std::find_if(i.ring->entries_.begin(), i.ring->entries_.end(),
                               [frameId](const Entry& e) { return e.ref.frameId == frameId && e.ready && e.pins > 0; });
        if (it == i.ring->entries_.end()) return false;
        sourceBuffer = it->packet.buffer;
        // packet.bytes is allocation capacity and may be larger after reusing a
        // buffer from an older frame. The PacketRef carries the exact logical
        // packet length for this frame. Export only those valid bytes.
        bytes = static_cast<VkDeviceSize>(it->ref.packedBytes);
        if (bytes == 0 || bytes > it->packet.bytes) throw std::runtime_error("zsl_ring session invalid packet size");
    }
    if (bytes > i.staging.bytes) throw std::runtime_error("zsl_ring session staging too small");
    check(vkResetFences(i.ring->device_, 1, &i.fence), "zsl_ring session reset fence");
    check(vkResetCommandBuffer(i.cmd, 0), "zsl_ring session reset cmd");
    VkCommandBufferBeginInfo bi{};
    bi.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    check(vkBeginCommandBuffer(i.cmd, &bi), "zsl_ring session begin");
    VkBufferMemoryBarrier src{};
    src.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
    src.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    src.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
    src.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    src.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    src.buffer = sourceBuffer;
    src.offset = 0;
    src.size = bytes;
    vkCmdPipelineBarrier(i.cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 1, &src,
                         0, nullptr);
    VkBufferCopy copy{0, 0, bytes};
    vkCmdCopyBuffer(i.cmd, sourceBuffer, i.staging.buffer, 1, &copy);
    check(vkEndCommandBuffer(i.cmd), "zsl_ring session end");
    VkSubmitInfo si{};
    si.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    si.commandBufferCount = 1;
    si.pCommandBuffers = &i.cmd;
    i.ring->submit_(si, i.fence);
    check(vkWaitForFences(i.ring->device_, 1, &i.fence, VK_TRUE, UINT64_MAX), "zsl_ring session wait");
    out.resize(static_cast<std::size_t>(bytes));
    std::memcpy(out.data(), i.mapped, out.size());
    return true;
}

void ZslRing::clear() noexcept {
    std::lock_guard<std::mutex> lock(mutex_);
    for (auto& e : entries_) destroyEntry(e);
    entries_.clear();
}

}  // namespace rawr::zsl_ring
