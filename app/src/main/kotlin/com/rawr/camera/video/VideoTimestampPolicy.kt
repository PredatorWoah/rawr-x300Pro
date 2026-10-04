package com.rawr.camera.video

/** Recording timeline choices. Query [NativeVideoRecorder.supportedTimestampPolicies] before offering one. */
enum class VideoTimestampPolicy(val nativeId: Int, val label: String) {
    /** Keep the encoder Surface's elapsed-time PTS, including gaps when frames arrive late. */
    REALTIME(0, "Real time"),

    /** Future mode: repeat the last frame into missing slots without changing audio timing. */
    FIXED_CADENCE_REPEAT(1, "Fixed cadence (repeat frames)");
}
