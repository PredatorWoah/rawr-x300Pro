#pragma once
namespace rawrcam::capture {
struct SingleMatchedFrameResult {
    int gpuAcquireFenceFd = -1;
    bool cpuUnlockFailed = false;
};
}  // namespace rawrcam::capture
