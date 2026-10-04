#include <rawr/zsl_ring/RawImageRing.h>

#include <algorithm>
#include <stdexcept>
#include <string>
namespace rawr::zsl_ring {
namespace {
void check(VkResult r, const char* w) {
    if (r != VK_SUCCESS) throw std::runtime_error(std::string(w) + " VkResult=" + std::to_string(r));
}
}  // namespace
RawImageRing::RawImageRing(VkPhysicalDevice p, VkDevice d, std::uint32_t w, std::uint32_t h, std::size_t n)
    : physical_(p), device_(d), width_(w), height_(h), maxFrames_(n) {
    if (!p || !d || !w || !h || !n) throw std::invalid_argument("raw image ring: invalid configuration");
    entries_.resize(n);
    try {
        for (auto& e : entries_) createEntry(e);
    } catch (...) {
        clear();
        throw;
    }
}
RawImageRing::~RawImageRing() { clear(); }
std::uint32_t RawImageRing::memoryType(std::uint32_t bits, VkMemoryPropertyFlags flags) const {
    VkPhysicalDeviceMemoryProperties p{};
    vkGetPhysicalDeviceMemoryProperties(physical_, &p);
    for (std::uint32_t i = 0; i < p.memoryTypeCount; ++i)
        if ((bits & (1u << i)) && (p.memoryTypes[i].propertyFlags & flags) == flags) return i;
    throw std::runtime_error("raw image ring: no memory type");
}
void RawImageRing::createEntry(Entry& e) {
    VkImageCreateInfo ci{};
    ci.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    ci.imageType = VK_IMAGE_TYPE_2D;
    ci.format = VK_FORMAT_R16_UINT;
    ci.extent = {width_, height_, 1};
    ci.mipLevels = 1;
    ci.arrayLayers = 1;
    ci.samples = VK_SAMPLE_COUNT_1_BIT;
    ci.tiling = VK_IMAGE_TILING_OPTIMAL;
    ci.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_STORAGE_BIT;
    ci.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    ci.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    check(vkCreateImage(device_, &ci, nullptr, &e.image), "raw image ring create image");
    VkMemoryRequirements req{};
    vkGetImageMemoryRequirements(device_, e.image, &req);
    VkMemoryAllocateInfo ai{};
    ai.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    ai.allocationSize = req.size;
    ai.memoryTypeIndex = memoryType(req.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    check(vkAllocateMemory(device_, &ai, nullptr, &e.memory), "raw image ring allocate");
    check(vkBindImageMemory(device_, e.image, e.memory, 0), "raw image ring bind");
    e.bytes = req.size;
    usedBytes_ += req.size;
    VkImageViewCreateInfo vi{};
    vi.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    vi.image = e.image;
    vi.viewType = VK_IMAGE_VIEW_TYPE_2D;
    vi.format = VK_FORMAT_R16_UINT;
    vi.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    vi.subresourceRange.levelCount = 1;
    vi.subresourceRange.layerCount = 1;
    check(vkCreateImageView(device_, &vi, nullptr, &e.view), "raw image ring view");
}
void RawImageRing::destroyEntry(Entry& e) noexcept {
    if (e.view) vkDestroyImageView(device_, e.view, nullptr);
    if (e.image) vkDestroyImage(device_, e.image, nullptr);
    if (e.memory) vkFreeMemory(device_, e.memory, nullptr);
    e = {};
}
std::optional<RawImageRef> RawImageRing::recordPush(VkCommandBuffer c, RawFrameId id, std::uint64_t ts, VkImage src,
                                                    VkImageLayout srcLayout) {
    std::lock_guard<std::mutex> l(mutex_);
    if (!c || !src) return std::nullopt;
    const std::size_t active =
        std::count_if(entries_.begin(), entries_.end(), [](const Entry& e) { return e.occupied; });
    std::size_t chosen = entries_.size();
    if (active < maxFrames_) {
        for (std::size_t k = 0; k < entries_.size(); ++k) {
            const std::size_t i = (next_ + k) % entries_.size();
            if (!entries_[i].occupied) {
                chosen = i;
                break;
            }
        }
    }
    if (chosen == entries_.size()) {
        for (std::size_t k = 0; k < entries_.size(); ++k) {
            const std::size_t i = (next_ + k) % entries_.size();
            if (entries_[i].pins == 0 && (!entries_[i].occupied || entries_[i].ready)) {
                chosen = i;
                break;
            }
        }
    }
    if (chosen == entries_.size()) {
        // All images are pinned by accepted captures or are still in flight.
        // Drop only this optional ZSL update; never allocate or wait on preview.
        return std::nullopt;
    }
    auto& e = entries_[chosen];
    VkImageMemoryBarrier b{};
    b.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    b.srcAccessMask = e.occupied ? VK_ACCESS_SHADER_READ_BIT : 0;
    b.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    b.oldLayout = e.occupied ? VK_IMAGE_LAYOUT_GENERAL : VK_IMAGE_LAYOUT_UNDEFINED;
    b.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    b.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    b.image = e.image;
    b.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    b.subresourceRange.levelCount = 1;
    b.subresourceRange.layerCount = 1;
    vkCmdPipelineBarrier(c, e.occupied ? VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT : VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                         VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, 1, &b);
    VkImageCopy cp{};
    cp.srcSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    cp.srcSubresource.layerCount = 1;
    cp.dstSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    cp.dstSubresource.layerCount = 1;
    cp.extent = {width_, height_, 1};
    vkCmdCopyImage(c, src, srcLayout, e.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &cp);
    b.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    b.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    b.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    b.newLayout = VK_IMAGE_LAYOUT_GENERAL;
    vkCmdPipelineBarrier(c, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 0, nullptr, 0,
                         nullptr, 1, &b);
    e.ref = {id, ts, width_, height_};
    e.occupied = true;
    e.ready = false;
    next_ = (chosen + 1) % entries_.size();
    return e.ref;
}
void RawImageRing::discardUnsubmitted(RawFrameId id) noexcept {
    std::lock_guard<std::mutex> lock(mutex_);
    for (auto& entry : entries_) {
        if (entry.occupied && !entry.ready && entry.pins == 0 && entry.ref.frameId == id) {
            entry.occupied = false;
            entry.ref = {};
            return;
        }
    }
}
void RawImageRing::markReady(RawFrameId id) noexcept {
    std::lock_guard<std::mutex> l(mutex_);
    for (auto& e : entries_)
        if (e.occupied && e.ref.frameId == id) {
            e.ready = true;
            break;
        }
}
std::size_t RawImageRing::frameCount() const {
    std::lock_guard<std::mutex> l(mutex_);
    return std::count_if(entries_.begin(), entries_.end(), [](const Entry& e) { return e.occupied && e.ready; });
}
struct RawImageRing::Snapshot::Impl {
    std::shared_ptr<RawImageRing> owner;
    RawImageRing* ring = nullptr;
    std::vector<RawFrameId> ids;
};
RawImageRing::Snapshot::Snapshot(std::unique_ptr<Impl> p) : impl_(std::move(p)) {}
RawImageRing::Snapshot::~Snapshot() {
    if (!impl_ || !impl_->ring) return;
    std::lock_guard<std::mutex> l(impl_->ring->mutex_);
    for (auto id : impl_->ids)
        for (auto& e : impl_->ring->entries_)
            if (e.occupied && e.pins > 0 && e.ref.frameId == id) {
                --e.pins;
                break;
            }
}
std::vector<RawImageRef> RawImageRing::Snapshot::refs() const {
    std::vector<RawImageRef> r;
    if (!impl_ || !impl_->ring) return r;
    std::lock_guard<std::mutex> l(impl_->ring->mutex_);
    for (auto id : impl_->ids)
        for (const auto& e : impl_->ring->entries_)
            if (e.occupied && e.pins > 0 && e.ref.frameId == id) {
                r.push_back(e.ref);
                break;
            }
    return r;
}
std::optional<GpuRawImageView> RawImageRing::Snapshot::gpuImage(RawFrameId id) const {
    if (!impl_ || !impl_->ring) return std::nullopt;
    std::lock_guard<std::mutex> l(impl_->ring->mutex_);
    if (std::find(impl_->ids.begin(), impl_->ids.end(), id) == impl_->ids.end()) return std::nullopt;
    for (const auto& e : impl_->ring->entries_)
        if (e.occupied && e.ready && e.pins > 0 && e.ref.frameId == id)
            return GpuRawImageView{e.ref, e.image, e.view, VK_FORMAT_R16_UINT, e.bytes};
    return std::nullopt;
}
void RawImageRing::Snapshot::release(RawFrameId id) noexcept {
    if (!impl_ || !impl_->ring || std::find(impl_->ids.begin(), impl_->ids.end(), id) == impl_->ids.end()) return;
    std::lock_guard<std::mutex> l(impl_->ring->mutex_);
    for (auto& e : impl_->ring->entries_)
        if (e.occupied && e.pins > 0 && e.ref.frameId == id) {
            --e.pins;
            break;
        }
    impl_->ids.erase(std::remove(impl_->ids.begin(), impl_->ids.end(), id), impl_->ids.end());
}
std::unique_ptr<RawImageRing::Snapshot> RawImageRing::snapshot(std::size_t maxFrames) {
    std::lock_guard<std::mutex> l(mutex_);
    auto p = std::make_unique<Snapshot::Impl>();
    p->owner = weak_from_this().lock();
    p->ring = this;
    std::vector<Entry*> ready;
    for (auto& e : entries_)
        if (e.occupied && e.ready) ready.push_back(&e);
    std::sort(ready.begin(), ready.end(),
              [](const Entry* a, const Entry* b) { return a->ref.frameId < b->ref.frameId; });
    if (maxFrames && ready.size() > maxFrames) ready.erase(ready.begin(), ready.end() - maxFrames);
    p->ids.reserve(ready.size());
    for (auto* e : ready) {
        ++e->pins;
        p->ids.push_back(e->ref.frameId);
    }
    return std::unique_ptr<Snapshot>(new Snapshot(std::move(p)));
}
void RawImageRing::clear() noexcept {
    std::lock_guard<std::mutex> l(mutex_);
    for (auto& e : entries_) destroyEntry(e);
    entries_.clear();
    usedBytes_ = 0;
}
}  // namespace rawr::zsl_ring
