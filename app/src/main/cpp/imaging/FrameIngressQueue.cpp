#include "imaging/FrameIngressQueue.h"

#include <algorithm>
#include <utility>

#include "imaging/FrameLimits.h"
namespace rawrcam::imaging {
FrameIngressQueue::FrameIngressQueue(Consume consume) : consume_(std::move(consume)) {}
FrameIngressQueue::~FrameIngressQueue() { stop(); }
void FrameIngressQueue::start() {
    worker_ = std::thread([this] { run(); });
}
void FrameIngressQueue::stop() {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        stopped_ = true;
    }
    changed_.notify_all();
    if (worker_.joinable()) worker_.join();
    std::lock_guard<std::mutex> lock(mutex_);
    queue_.clear();
}
uint64_t FrameIngressQueue::latestTimestamp(uint64_t generation) const {
    std::lock_guard<std::mutex> lock(mutex_);
    return generation == generation_ ? latestTimestamp_ : 0;
}
void FrameIngressQueue::enqueue(AcquiredRawFrame frame) {
    RawFrameLease lease(frame);
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (stopped_) return;
        if (generation_ < frame.generation) {
            generation_ = frame.generation;
            latestTimestamp_ = 0;
        }
        if (generation_ == frame.generation) latestTimestamp_ = std::max(latestTimestamp_, frame.timestampNs);
        auto isImage = [](const Event& event) { return std::holds_alternative<RawFrameLease>(event.payload); };
        if (std::count_if(queue_.begin(), queue_.end(), isImage) >= static_cast<ptrdiff_t>(kMaxIngressImages)) {
            queue_.erase(std::find_if(queue_.begin(), queue_.end(), isImage));
            ++drops_.images;
        }
        queue_.push_back({std::move(lease), std::chrono::steady_clock::now()});
    }
    changed_.notify_one();
}
bool FrameIngressQueue::enqueue(const metadata::FrameMetadataSnapshot& metadata) {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (stopped_) return false;
        auto isMetadata = [](const Event& event) {
            return std::holds_alternative<rawrcam::metadata::FrameMetadataSnapshot>(event.payload);
        };
        if (std::count_if(queue_.begin(), queue_.end(), isMetadata) >= static_cast<ptrdiff_t>(kMaxIngressMetadata)) {
            queue_.erase(std::find_if(queue_.begin(), queue_.end(), isMetadata));
            ++drops_.metadata;
        }
        queue_.push_back({metadata, std::chrono::steady_clock::now()});
    }
    changed_.notify_one();
    return true;
}
void FrameIngressQueue::run() {
    for (;;) {
        Event event{};
        Drops drops{};
        {
            std::unique_lock<std::mutex> lock(mutex_);
            changed_.wait_for(lock, std::chrono::milliseconds(10), [this] { return stopped_ || !queue_.empty(); });
            if (stopped_ && queue_.empty()) return;
            if (!queue_.empty()) {
                event = std::move(queue_.front());
                queue_.pop_front();
            }
            drops = std::exchange(drops_, {});
        }
        consume_(std::move(event), drops);
    }
}
}  // namespace rawrcam::imaging
