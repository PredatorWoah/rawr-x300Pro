#include "presentation/PresentationSurface.h"

#include <android/native_window_jni.h>
#include <vulkan/vulkan_android.h>

#include <stdexcept>
namespace rawrcam::presentation {
PresentationSurface::~PresentationSurface() { reset(); }
void PresentationSurface::reset() noexcept {
    if (surface_ && instance_) vkDestroySurfaceKHR(instance_, surface_, nullptr);
    surface_ = VK_NULL_HANDLE;
    if (window_) ANativeWindow_release(window_);
    window_ = nullptr;
    instance_ = VK_NULL_HANDLE;
}
void PresentationSurface::replace(VkInstance instance, JNIEnv* env, jobject surface) {
    reset();
    instance_ = instance;
    window_ = ANativeWindow_fromSurface(env, surface);
    if (!window_) throw std::runtime_error("ANativeWindow_fromSurface failed");
    VkAndroidSurfaceCreateInfoKHR info{};
    info.sType = VK_STRUCTURE_TYPE_ANDROID_SURFACE_CREATE_INFO_KHR;
    info.window = window_;
    const auto result = vkCreateAndroidSurfaceKHR(instance_, &info, nullptr, &surface_);
    if (result != VK_SUCCESS) {
        reset();
        throw std::runtime_error("vkCreateAndroidSurfaceKHR failed");
    }
}
}  // namespace rawrcam::presentation
