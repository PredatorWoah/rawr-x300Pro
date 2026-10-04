#pragma once

#include <functional>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <utility>

namespace rawrcam::develop::demosaic {

// Single-flight async worker shared by the RCD/VNG4/Dual still adapters.
// Owns the worker thread, busy/completion state, and diagnostic emit; the
// pipeline-specific record work stays in each adapter. Claim/spawn/finish
// sequencing matches the previously triplicated hand-rolled state machines
// exactly: claim under lock, join outside the lock, finish under lock.
template <typename Completion>
class SingleFlightWorker final {
   public:
    using Diagnostic = std::function<void(const std::string&)>;

    explicit SingleFlightWorker(Diagnostic diagnostic = {}) : diagnostic_(std::move(diagnostic)) {}
    ~SingleFlightWorker() { joinWorker(); }
    SingleFlightWorker(const SingleFlightWorker&) = delete;
    SingleFlightWorker& operator=(const SingleFlightWorker&) = delete;

    // Claims the single flight. False when busy or a completion is pending.
    // Call joinWorker() after a successful claim, mirroring the adapters'
    // original claim-then-join order.
    [[nodiscard]] bool tryClaim() {
        std::lock_guard<std::mutex> lock(mutex_);
        if (busy_ || completion_) return false;
        busy_ = true;
        return true;
    }

    // Spawns the worker thread; the work tail must call finish().
    template <typename Work>
    void spawn(Work&& work) {
        worker_ = std::thread(std::forward<Work>(work));
    }

    // Publishes completion from the worker thread.
    void finish(Completion done) {
        std::lock_guard<std::mutex> lock(mutex_);
        completion_ = std::move(done);
        busy_ = false;
    }

    [[nodiscard]] bool busy() const noexcept {
        std::lock_guard<std::mutex> lock(mutex_);
        return busy_;
    }

    std::optional<Completion> pollCompletion() {
        std::optional<Completion> out;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (!completion_) return std::nullopt;
            out = std::move(completion_);
            completion_.reset();
        }
        joinWorker();
        return out;
    }

    void joinWorker() noexcept {
        if (worker_.joinable()) worker_.join();
    }

    // Joins and clears busy/completion state; pipeline teardown stays with
    // the caller, matching the adapters' original reset() split.
    void resetState() {
        joinWorker();
        std::lock_guard<std::mutex> lock(mutex_);
        busy_ = false;
        completion_.reset();
    }

    void emit(const std::string& line) const {
        if (diagnostic_) diagnostic_(line);
    }

   private:
    Diagnostic diagnostic_;
    mutable std::mutex mutex_;
    std::thread worker_;
    bool busy_ = false;
    std::optional<Completion> completion_;
};

}  // namespace rawrcam::develop::demosaic
