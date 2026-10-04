#pragma once
#include <android/native_window.h>
#include <jni.h>
#include <vulkan/vulkan.h>
namespace rawrcam::presentation {
class PresentationSurface final {
   public:
    ~PresentationSurface();
    PresentationSurface() = default;
    PresentationSurface(const PresentationSurface&) = delete;
    PresentationSurface& operator=(const PresentationSurface&) = delete;
    void replace(VkInstance instance, JNIEnv* env, jobject surface);
    void reset() noexcept;
    VkSurfaceKHR handle() const noexcept { return surface_; }
    ANativeWindow* window() const noexcept { return window_; }

   private:
    VkInstance instance_ = VK_NULL_HANDLE;
    ANativeWindow* window_ = nullptr;
    VkSurfaceKHR surface_ = VK_NULL_HANDLE;
};
}  // namespace rawrcam::presentation
