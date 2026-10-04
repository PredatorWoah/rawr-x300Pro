package com.rawr.camera.fixtures

import com.rawr.camera.model.CameraCapabilities
import com.rawr.camera.model.DiscreteExposureCapability
import com.rawr.camera.model.ExposureCandidate
import com.rawr.camera.model.LensCapability
import com.rawr.camera.model.ManualFocusCapability
import com.rawr.camera.model.WhiteBalanceMode

/** Representative camera capability data for previews and tests. */
object CaptureFixtures {
    fun baseline() = CameraCapabilities(
        lenses =
            listOf(
                LensCapability("wide", "14"),
                LensCapability("main", "35"),
                LensCapability("tele", "85")
            ),
        shutter =
            exposureCapability(
                prefix = "ss",
                labels =
                    listOf(
                        "1/4000",
                        "1/3000",
                        "1/2000",
                        "1/1500",
                        "1/1000",
                        "1/750",
                        "1/500",
                        "1/350",
                        "1/250",
                        "1/180",
                        "1/125",
                        "1/90",
                        "1/60",
                        "1/45",
                        "1/30",
                        "1/20",
                        "1/15",
                        "1/10",
                        "1/8",
                        "1/6",
                        "1/4",
                        "1/3",
                        "1/2",
                        "0.7s",
                        "1s",
                        "1.4s",
                        "2s",
                        "3s",
                        "4s",
                        "6s",
                        "8s",
                        "11s",
                        "15s",
                        "22s",
                        "30s"
                    ),
                anchorIndices = (0..34 step 2).toSet(),
                initialIndex = 10
            ),
        iso =
            exposureCapability(
                prefix = "iso",
                labels =
                    listOf(
                        "25",
                        "35",
                        "50",
                        "70",
                        "100",
                        "140",
                        "200",
                        "280",
                        "400",
                        "560",
                        "800",
                        "1100",
                        "1600",
                        "2200",
                        "3200",
                        "4500",
                        "6400"
                    ),
                anchorIndices = (0..16 step 2).toSet(),
                initialIndex = 6
            ),
        ev =
            evCompensationCapability(
                minimumTenths = -30,
                maximumTenths = 30,
                initialTenths = 0
            ),
        manualFocus = ManualFocusCapability(supported = true, distanceReadoutTrustworthy = false),
        tapAfSupported = true,
        fpsChoices = listOf(24, 30, 60),
        stabilizationOptions = listOf("Off", "Optical", "Electronic"),
        supportedWhiteBalanceModes = WhiteBalanceMode.entries.filter { it != WhiteBalanceMode.ManualTempTint }.toSet(),
        manualWhiteBalanceSupported = true
    )

    private fun evCompensationCapability(
        minimumTenths: Int,
        maximumTenths: Int,
        initialTenths: Int
    ): DiscreteExposureCapability {
        require(minimumTenths <= initialTenths && initialTenths <= maximumTenths)

        // Integer tenths avoid floating-point identity/formatting drift in fixture IDs.
        // Full-stop anchors remain whole EV values while the control can traverse every 0.1 EV.
        val values = (minimumTenths..maximumTenths).toList()
        val candidates =
            values.map { tenths ->
                val label = formatEvTenths(tenths)
                ExposureCandidate(
                    id = "ev_${label.idToken()}",
                    displayLabel = label
                )
            }
        val byTenths = values.zip(candidates).toMap()

        return DiscreteExposureCapability(
            candidates = candidates,
            fullStopAnchorIds =
                values
                    .filter { it % 10 == 0 }
                    .mapTo(mutableSetOf()) { requireNotNull(byTenths[it]).id },
            initialCandidateId = requireNotNull(byTenths[initialTenths]).id
        )
    }

    private fun formatEvTenths(tenths: Int): String {
        val sign = if (tenths >= 0) "+" else "-"
        val magnitude = kotlin.math.abs(tenths)
        return "$sign${magnitude / 10}.${magnitude % 10}"
    }

    private fun exposureCapability(
        prefix: String,
        labels: List<String>,
        anchorIndices: Set<Int>,
        initialIndex: Int
    ): DiscreteExposureCapability {
        // Fixture IDs are explicit, stable semantic identifiers from the screen's perspective.
        // Real capability providers must supply their own opaque IDs; Compose never interprets them.
        val candidates =
            labels.map { label ->
                ExposureCandidate(
                    id = "${prefix}_${label.idToken()}",
                    displayLabel = label
                )
            }
        return DiscreteExposureCapability(
            candidates = candidates,
            fullStopAnchorIds = anchorIndices.mapTo(mutableSetOf()) { candidates[it].id },
            initialCandidateId = candidates[initialIndex].id
        )
    }

    private fun String.idToken(): String = buildString {
        for (char in this@idToken) {
            when {
                char.isLetterOrDigit() -> append(char.lowercaseChar())
                char == '+' -> append("plus")
                char == '-' -> append("minus")
                char == '.' -> append('_')
                char == '/' -> append('_')
            }
        }
    }
}
