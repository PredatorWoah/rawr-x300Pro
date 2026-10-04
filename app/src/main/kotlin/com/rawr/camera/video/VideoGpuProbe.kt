package com.rawr.camera.video

import android.hardware.HardwareBuffer
import android.view.Surface

/** Diagnostic Vulkan format query for a dequeued encoder buffer. */
internal object VideoGpuProbe {
    init { System.loadLibrary("rawrcam_native") }
    external fun inspectHardwareBuffer(buffer: HardwareBuffer): LongArray
    external fun inspectEncoderSurface(surface: Surface): LongArray
    external fun renderHalfFloatFrames(surface: Surface): Int
}
