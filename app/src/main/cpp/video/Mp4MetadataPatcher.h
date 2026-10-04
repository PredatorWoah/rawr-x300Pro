#pragma once

#include <string>

namespace rawrcam::video {

struct Mp4DeviceMetadata {
    std::string make;
    std::string model;
    std::string software;
    std::string renderProfile;
    std::string gamut;
    std::string transfer;
    std::string bitDepth;
    bool log = false;
};

// Adds camera make/model to a finished MP4 on a readable and writable fd.
// Writes moov/udta ©mak ©mod ©too (QuickTime, read by ffmpeg and most
// editors) and com.android.manufacturer / com.android.model keys in the
// moov/meta mdta box (merged into the one MPEG4Writer writes). mdat is never
// moved, so chunk offsets stay valid. When moov is the last box it is
// rewritten in place; otherwise the new moov is appended and the old one
// becomes a free box. Returns false and leaves the file untouched when the
// layout is not one it can patch safely.
bool patchMp4DeviceMetadata(int fd, const Mp4DeviceMetadata& metadata, std::string* error = nullptr);

}  // namespace rawrcam::video
