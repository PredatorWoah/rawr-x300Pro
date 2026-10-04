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

public val Icons.Outlined.Iso: ImageVector
    get() {
        if (_iso != null) {
            return _iso!!
        }
        _iso = materialIcon(name = "Outlined.Iso") {
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
                moveTo(5.5f, 7.5f)
                horizontalLineToRelative(2.0f)
                verticalLineToRelative(-2.0f)
                lineTo(9.0f, 5.5f)
                verticalLineToRelative(2.0f)
                horizontalLineToRelative(2.0f)
                lineTo(11.0f, 9.0f)
                lineTo(9.0f, 9.0f)
                verticalLineToRelative(2.0f)
                lineTo(7.5f, 11.0f)
                lineTo(7.5f, 9.0f)
                horizontalLineToRelative(-2.0f)
                lineTo(5.5f, 7.5f)
                close()
                moveTo(19.0f, 19.0f)
                lineTo(5.0f, 19.0f)
                lineTo(19.0f, 5.0f)
                verticalLineToRelative(14.0f)
                close()
                moveTo(17.0f, 17.0f)
                verticalLineToRelative(-1.5f)
                horizontalLineToRelative(-5.0f)
                lineTo(12.0f, 17.0f)
                horizontalLineToRelative(5.0f)
                close()
            }
        }
        return _iso!!
    }

private var _iso: ImageVector? = null
