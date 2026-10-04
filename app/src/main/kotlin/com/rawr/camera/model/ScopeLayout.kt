package com.rawr.camera.model

data class ScopePosition(val xDp: Float, val yDp: Float)

private const val COMPACT_SCOPE_OUTER_MARGIN_DP = 10f
private const val COMPACT_SCOPE_GAP_DP = 16f
private const val COMPACT_SCOPE_SIDE_INSET_DP = 16f

/**
 * Compact scope placement in the portrait-locked Activity coordinate system.
 *
 * Portrait uses one physical outer margin on both left/right edges. In landscape that same
 * physical margin is applied to the x axis, which becomes the visible top/bottom axis after the
 * instrument quarter-turn. The third scope is separated from the scope above it by a fixed gap.
 */
fun compactScopePosition(
    orientation: Orientation,
    count: Int,
    index: Int,
    containerWidthDp: Float,
    containerHeightDp: Float,
    physicalWidthDp: Float,
    physicalHeightDp: Float
): ScopePosition {
    require(count in 1..3)
    require(index in 0 until count)

    return when (orientation) {
        Orientation.Portrait -> {
            val right = containerWidthDp - COMPACT_SCOPE_OUTER_MARGIN_DP - physicalWidthDp
            val x =
                when {
                    count == 1 -> right
                    index == 0 -> COMPACT_SCOPE_OUTER_MARGIN_DP
                    else -> right
                }
            val y =
                COMPACT_SCOPE_SIDE_INSET_DP +
                    if (index == 2) physicalHeightDp + COMPACT_SCOPE_GAP_DP else 0f
            ScopePosition(x, y)
        }

        Orientation.Landscape -> {
            val top = containerWidthDp - COMPACT_SCOPE_OUTER_MARGIN_DP - physicalWidthDp
            val bottom = COMPACT_SCOPE_OUTER_MARGIN_DP
            val x = if (index == 1) bottom else top
            val sideInset = COMPACT_SCOPE_SIDE_INSET_DP
            val y = sideInset + if (index == 2) physicalHeightDp + COMPACT_SCOPE_GAP_DP else 0f
            ScopePosition(x, y)
        }
    }
}

// Legacy normalized slot contract retained for model/tooling compatibility. Production compact
// placement uses physical dp margins in ScopeLayer.
data class ScopeSlot(val xFraction: Float, val yFraction: Float)

fun scopeSlots(orientation: Orientation, count: Int): List<ScopeSlot> {
    if (count <= 0) return emptyList()
    return when (orientation) {
        Orientation.Portrait -> {
            when (count) {
                1 -> listOf(ScopeSlot(.6611f, .025f))
                2 -> listOf(ScopeSlot(.0278f, .025f), ScopeSlot(.6611f, .025f))
                else -> listOf(ScopeSlot(.0278f, .025f), ScopeSlot(.6611f, .025f), ScopeSlot(.6611f, .1917f))
            }
        }

        Orientation.Landscape -> {
            when (count) {
                1 -> listOf(ScopeSlot(.689f, .067f))
                2 -> listOf(ScopeSlot(.689f, .067f), ScopeSlot(.000f, .067f))
                else -> listOf(ScopeSlot(.689f, .067f), ScopeSlot(.000f, .067f), ScopeSlot(.689f, .321f))
            }
        }
    }
}
