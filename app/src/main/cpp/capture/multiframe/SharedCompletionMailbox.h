#pragma once

#include <deque>
#include <mutex>
#include <string>

namespace rawrcam::capture::multiframe {

// Thread-safe completion mailbox. Replaces the duplicated
// multiframeDng/JpegCompletions_ deques (+ publish/fail/poll) in
// FrameSubmitCoordinator and the parallel queues in SingleFrameCoordinator.
class StringMailbox {
   public:
    void push(std::string value) {
        std::lock_guard<std::mutex> lock(mutex_);
        queue_.push_back(std::move(value));
    }
    std::string poll() {
        std::lock_guard<std::mutex> lock(mutex_);
        if (queue_.empty()) return {};
        std::string value = std::move(queue_.front());
        queue_.pop_front();
        return value;
    }
    bool empty() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return queue_.empty();
    }

   private:
    mutable std::mutex mutex_;
    std::deque<std::string> queue_;
};

}  // namespace rawrcam::capture::multiframe
