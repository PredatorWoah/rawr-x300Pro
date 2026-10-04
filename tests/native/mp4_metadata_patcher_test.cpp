// Patches the MP4 given on the command line in place. Make/model default to
// fixed test values and can be overridden by the next two arguments.
// tests/video_containers.py generates fixtures and checks
// the result with ffprobe.
#include <fcntl.h>
#include <unistd.h>

#include <cstdio>
#include <string>

#include "video/Mp4MetadataPatcher.h"

int main(int argc, char** argv) {
    if (argc != 2 && argc != 3 && argc != 4) {
        std::fprintf(stderr, "usage: %s file.mp4 [--log | make model]\n", argv[0]);
        return 2;
    }
    const int fd = open(argv[1], O_RDWR);
    if (fd < 0) {
        std::perror("open");
        return 2;
    }
    std::string error;
    const std::string make = argc == 4 ? argv[2] : "vivo";
    const std::string model = argc == 4 ? argv[3] : "V2408A";
    const bool ok =
        rawrcam::video::patchMp4DeviceMetadata(fd,
                                               {make, model, "RAWR test", "ARRI LogC3 EI800", "ARRI Wide Gamut 3",
                                                "LogC3", "10", argc == 3 && std::string(argv[2]) == "--log"},
                                               &error);
    close(fd);
    if (!ok) {
        std::fprintf(stderr, "FAIL patch: %s\n", error.c_str());
        return 1;
    }
    return 0;
}
