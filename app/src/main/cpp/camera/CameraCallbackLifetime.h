#pragma once
#include <condition_variable>
#include <cstdint>
#include <memory>
#include <mutex>
namespace rawrcam::camera {
template <class Owner>
struct CameraCallbackLifetime {
    std::mutex mutex;
    std::condition_variable idle;
    Owner* owner = nullptr;
    uint32_t inFlight = 0;
};
template <class Owner>
struct CameraCallbackGuard {
    explicit CameraCallbackGuard(const std::shared_ptr<CameraCallbackLifetime<Owner>>& lifetime) : lifetime(lifetime) {
        std::lock_guard<std::mutex> lock(lifetime->mutex);
        owner = lifetime->owner;
        if (owner) ++lifetime->inFlight;
    }
    ~CameraCallbackGuard() {
        if (!owner) return;
        std::lock_guard<std::mutex> lock(lifetime->mutex);
        if (--lifetime->inFlight == 0) lifetime->idle.notify_all();
    }
    CameraCallbackGuard(const CameraCallbackGuard&) = delete;
    CameraCallbackGuard& operator=(const CameraCallbackGuard&) = delete;
    std::shared_ptr<CameraCallbackLifetime<Owner>> lifetime;
    Owner* owner = nullptr;
};
}  // namespace rawrcam::camera
