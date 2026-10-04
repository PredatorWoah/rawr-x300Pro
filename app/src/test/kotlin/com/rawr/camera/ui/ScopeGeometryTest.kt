package com.rawr.camera.ui

import androidx.compose.ui.unit.dp
import kotlin.test.*

class ScopeGeometryTest {
    @Test fun compactPortraitScopesUseSymmetricMarginsWithoutOverlap() {
        val left = calculateScopeGeometry(360.dp, 480.dp, false, 2, 0, false, 0)
        val right = calculateScopeGeometry(360.dp, 480.dp, false, 2, 1, false, 0)
        assertEquals(10.dp, left.x)
        assertEquals(10.dp, 360.dp - right.x - right.physicalWidth)
        assertTrue(left.x + left.physicalWidth < right.x)
        assertEquals(left.y, right.y)
    }

    @Test fun singlePortraitScopeUsesTheRightSlot() {
        val scope = calculateScopeGeometry(360.dp, 480.dp, false, 1, 0, false, 0)
        assertEquals(10.dp, 360.dp - scope.x - scope.physicalWidth)
    }

    @Test fun bothLandscapePosturesRotateInOppositeDirectionsWithIdenticalGeometry() {
        val clockwise = calculateScopeGeometry(360.dp, 480.dp, true, 2, 0, false, 90)
        val counter = calculateScopeGeometry(360.dp, 480.dp, true, 2, 0, false, 270)
        assertEquals(1, clockwise.presentationQuarterTurns)
        assertEquals(3, counter.presentationQuarterTurns)
        assertEquals(clockwise.copy(presentationQuarterTurns = 3), counter)
        val left = calculateScopeGeometry(360.dp, 480.dp, true, 2, 1, false, 90)
        assertTrue(left.x + left.physicalWidth < clockwise.x)
    }

    @Test fun expandedScopeStaysCenteredOnShortDisplays() {
        for (landscape in listOf(false, true)) {
            val scope = calculateScopeGeometry(240.dp, 200.dp, landscape, 2, 0, true, 90)
            assertTrue(scope.physicalWidth <= 240.dp)
            assertTrue(scope.physicalHeight <= 200.dp)
            assertEquals((240.dp - scope.physicalWidth) / 2, scope.x)
            assertEquals((200.dp - scope.physicalHeight) / 2, scope.y)
        }
    }
}
