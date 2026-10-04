package com.rawr.camera.ui

import com.rawr.camera.model.Orientation

/**
 * Classifies sensor angle with a neutral band around transitions. Returning the current value in the
 * neutral band prevents UI labels/scopes from chattering when the phone is held near 45 degrees.
 */
internal fun classifyPhysicalOrientation(angle: Int, current: Orientation): Orientation {
    if (angle !in 0..359) return current
    return when {
        angle in 60..120 || angle in 240..300 -> Orientation.Landscape
        angle <= 30 || angle >= 330 || angle in 150..210 -> Orientation.Portrait
        else -> current
    }
}

/** Converts OrientationEventListener's physical-angle convention into the clockwise
 * display-rotation convention consumed by Camera2-style sensor-to-display transforms.
 * Portrait quadrants are unchanged; the two landscape quadrants exchange 90/270.
 */
internal fun physicalAngleToDisplayRotation(angle: Int): Int {
    if (angle !in 0..359) return 0
    val physicalQuadrant = ((angle + 45) / 90 * 90) % 360
    return (360 - physicalQuadrant) % 360
}
