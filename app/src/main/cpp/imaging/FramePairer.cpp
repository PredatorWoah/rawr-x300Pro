#include "FramePairer.h"

#include <unistd.h>
namespace rawrcam::imaging {
FramePairer::~FramePairer() { clear(); }
void FramePairer::release(PendingRawImage& v) noexcept {
    if (v.acquireFenceFd >= 0) close(v.acquireFenceFd);
    if (v.image) AImage_delete(v.image);
    v = {};
}
void FramePairer::putImage(uint64_t ts, PendingRawImage v) {
    auto it = images_.find(ts);
    if (it != images_.end()) {
        release(it->second);
        it->second = v;
    } else
        images_.emplace(ts, v);
}
void FramePairer::putMetadata(uint64_t ts, FrameRenderMetadata v) { metadata_[ts] = std::move(v); }
bool FramePairer::takeMatched(uint64_t ts, MatchedFrame* out) {
    if (!out) return false;
    auto i = images_.find(ts);
    auto m = metadata_.find(ts);
    if (i == images_.end() || m == metadata_.end()) return false;
    out->timestampNs = ts;
    out->image = i->second;
    out->metadata = m->second;
    images_.erase(i);
    metadata_.erase(m);
    return true;
}
uint64_t FramePairer::trim(size_t maxPendingImages, size_t maxPendingMetadata) {
    uint64_t dropped = 0;
    while (images_.size() > maxPendingImages) {
        auto i = images_.begin();
        release(i->second);
        images_.erase(i);
        ++dropped;
    }
    while (metadata_.size() > maxPendingMetadata) metadata_.erase(metadata_.begin());
    return dropped;
}
void FramePairer::clear() {
    for (auto& kv : images_) release(kv.second);
    images_.clear();
    metadata_.clear();
}
}  // namespace rawrcam::imaging
