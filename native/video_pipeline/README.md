# Video RAW stage

`VideoDemosaic` owns the recording resolution RAW demosaic, crop or 2× Bayer
reduction, lens-shading sampling, and sensor clip-state output. It does not
read the lower resolution viewfinder image. Its output feeds the shared
post-demosaic processor in `native/post_demosaic`.

Layout: `include/video_pipeline/`, `src/`, `shaders/` (`video_demosaic.comp`
and `video_raw_core.glsl`, which the fused RAW-input tonemap variant also
includes). Android-only.

`VideoProcessingConfig` is the native settings contract. Highlight method,
threshold, compression, lens shading, and profiled denoise can update while
recording. FCC capacity and defringe pipeline parameters are latched when a
recording starts; status reports when a settings change needs a new recording.
The Video false color correction switch maps FCC steps to zero for video only;
still capture keeps its configured 1–8 steps. Changing this switch takes effect
on the next recording because the FCC pipeline is allocated at start.

The recorder's timestamp policy is selected at recording start through
`NativeVideoRecorder.Settings.timestampPolicy`. Kotlin can enumerate
`VideoTimestampPolicy.entries` and query
`NativeVideoRecorder.supportedTimestampPolicies()` before showing a choice.
`REALTIME` is supported: the Vulkan encoder Surface supplies video presentation
times, retaining elapsed time through late or missing frames so audio keeps its
normal timeline. This is **not** the RAW sensor timestamp; that timestamp is
currently used for frame/metadata pairing and diagnostics. `FIXED_CADENCE_REPEAT`
is reserved but unavailable until the native path can present a repeated frame
for each missing cadence slot. Rewriting muxer timestamps without those frames
would change playback speed relative to audio.
