#include "VivoVendorTags.h"

#include <dlfcn.h>

namespace rawrcam::camera::vivo {
namespace {
using GetTagFromNameFn = camera_status_t (*)(const ACameraMetadata*, const char*, uint32_t*);
}

std::optional<uint32_t> resolveMetadataTagByNameCompat(const ACameraMetadata* metadata, const char* name,
                                                       std::string* error) {
    void* handle = dlopen("libcamera2ndk.so", RTLD_NOW | RTLD_LOCAL);
    if (!handle) {
        if (error) *error = "libcamera2ndk.so unavailable";
        return std::nullopt;
    }
    auto* fn = reinterpret_cast<GetTagFromNameFn>(dlsym(handle, "ACameraMetadata_getTagFromName"));
    if (!fn) {
        if (error) *error = "ACameraMetadata_getTagFromName unavailable at runtime";
        dlclose(handle);
        return std::nullopt;
    }
    uint32_t tag = 0;
    const camera_status_t status = fn(metadata, name, &tag);
    dlclose(handle);
    if (status != ACAMERA_OK) {
        if (error) *error = "vendor tag lookup failed status=" + std::to_string(status);
        return std::nullopt;
    }
    return tag;
}

bool containsI32Tag(const ACameraMetadata* metadata, uint32_t listTag, uint32_t wanted) {
    ACameraMetadata_const_entry e{};
    if (ACameraMetadata_getConstEntry(metadata, listTag, &e) != ACAMERA_OK || !e.data.i32) return false;
    for (uint32_t i = 0; i < e.count; ++i) {
        if (static_cast<uint32_t>(e.data.i32[i]) == wanted) return true;
    }
    return false;
}
}  // namespace rawrcam::camera::vivo
