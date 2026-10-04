#pragma once

#include <android/log.h>

// Logging macros shared by lifecycle translation units. constexpr gives the
// tag internal linkage, so including this header is ODR-safe.
constexpr const char* kSessionLogTag = "RawrCamNative";
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, kSessionLogTag, __VA_ARGS__)
#define LOGW(...) __android_log_print(ANDROID_LOG_WARN, kSessionLogTag, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, kSessionLogTag, __VA_ARGS__)
