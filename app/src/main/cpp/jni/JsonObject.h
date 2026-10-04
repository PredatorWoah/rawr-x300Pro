#pragma once
#include <jni.h>

#include <string>
#include <type_traits>
namespace rawrcam::jni {
struct Json {
    JNIEnv* e;
    jobject value;
    jclass cls;
    Json(JNIEnv* env, jstring text) : e(env), cls(env->FindClass("org/json/JSONObject")) {
        value = e->NewObject(cls, e->GetMethodID(cls, "<init>", "(Ljava/lang/String;)V"), text);
    }
    Json(JNIEnv* env, jobject object) : e(env), value(object), cls(env->FindClass("org/json/JSONObject")) {}
    Json(const Json&) = delete;
    Json& operator=(const Json&) = delete;
    ~Json() {
        e->DeleteLocalRef(value);
        e->DeleteLocalRef(cls);
    }
    double number(const char* key, double fallback) {
        jstring k = e->NewStringUTF(key);
        auto v = e->CallDoubleMethod(value, e->GetMethodID(cls, "optDouble", "(Ljava/lang/String;D)D"), k, fallback);
        e->DeleteLocalRef(k);
        return v;
    }
    bool flag(const char* key, bool fallback) {
        jstring k = e->NewStringUTF(key);
        auto v = e->CallBooleanMethod(value, e->GetMethodID(cls, "optBoolean", "(Ljava/lang/String;Z)Z"), k, fallback);
        e->DeleteLocalRef(k);
        return v;
    }
    std::string text(const char* key) {
        jstring k = e->NewStringUTF(key);
        auto v = (jstring)e->CallObjectMethod(
            value, e->GetMethodID(cls, "optString", "(Ljava/lang/String;)Ljava/lang/String;"), k);
        const char* bytes = v ? e->GetStringUTFChars(v, nullptr) : nullptr;
        std::string result = bytes ? bytes : "";
        if (bytes) e->ReleaseStringUTFChars(v, bytes);
        e->DeleteLocalRef(k);
        e->DeleteLocalRef(v);
        return result;
    }
    jobject child(const char* key) {
        jstring k = e->NewStringUTF(key);
        auto v = e->CallObjectMethod(
            value, e->GetMethodID(cls, "optJSONObject", "(Ljava/lang/String;)Lorg/json/JSONObject;"), k);
        e->DeleteLocalRef(k);
        if (!v) v = e->NewObject(cls, e->GetMethodID(cls, "<init>", "()V"));
        return v;
    }
    template <class T>
    T scalar(const char* key, T fallback) {
        if constexpr (std::is_same_v<T, bool>)
            return flag(key, fallback);
        else
            return T(number(key, fallback));
    }
};
}  // namespace rawrcam::jni
