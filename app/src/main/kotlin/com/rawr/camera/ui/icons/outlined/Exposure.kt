/*
 * Copyright 2025 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

package com.rawr.camera.ui.icons.outlined

import com.rawr.camera.ui.icons.Icons
import com.rawr.camera.ui.icons.materialIcon
import com.rawr.camera.ui.icons.materialPath
import androidx.compose.ui.graphics.vector.ImageVector

public val Icons.Outlined.Exposure: ImageVector
    get() {
        if (_exposure != null) {
            return _exposure!!
        }
        _exposure = materialIcon(name = "Outlined.Exposure") {
            materialPath {
                moveTo(19.0f, 3.0f)
                lineTo(5.0f, 3.0f)
                curveToRelative(-1.1f, 0.0f, -2.0f, 0.9f, -2.0f, 2.0f)
                verticalLineToRelative(14.0f)
                curveToRelative(0.0f, 1.1f, 0.9f, 2.0f, 2.0f, 2.0f)
                horizontalLineToRelative(14.0f)
                curveToRelative(1.1f, 0.0f, 2.0f, -0.9f, 2.0f, -2.0f)
                lineTo(21.0f, 5.0f)
                curveToRelative(0.0f, -1.1f, -0.9f, -2.0f, -2.0f, -2.0f)
                close()
                moveTo(17.59f, 5.0f)
                lineTo(5.0f, 17.59f)
                lineTo(5.0f, 5.0f)
                horizontalLineToRelative(12.59f)
                close()
                moveTo(6.41f, 19.0f)
                lineTo(19.0f, 6.41f)
                lineTo(19.0f, 19.0f)
                lineTo(6.41f, 19.0f)
                close()
                moveTo(6.0f, 7.0f)
                horizontalLineToRelative(5.0f)
                verticalLineToRelative(1.5f)
                lineTo(6.0f, 8.5f)
                close()
                moveTo(16.0f, 12.5f)
                horizontalLineToRelative(-1.5f)
                verticalLineToRelative(2.0f)
                horizontalLineToRelative(-2.0f)
                lineTo(12.5f, 16.0f)
                horizontalLineToRelative(2.0f)
                verticalLineToRelative(2.0f)
                lineTo(16.0f, 18.0f)
                verticalLineToRelative(-2.0f)
                horizontalLineToRelative(2.0f)
                verticalLineToRelative(-1.5f)
                horizontalLineToRelative(-2.0f)
                close()
            }
        }
        return _exposure!!
    }

private var _exposure: ImageVector? = null
