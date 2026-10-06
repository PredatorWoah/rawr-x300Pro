package com.rawr.camera.integration

import org.junit.Assert.assertEquals
import org.junit.Test

class DeviceNamesTest {
    @Test
    fun knownModelCodesBecomeMarketingNames() {
        assertEquals("vivo X300 Pro", DeviceNames.marketingModel("V2514"))
        assertEquals("vivo X300 Ultra", DeviceNames.marketingModel("V2562"))
    }

    @Test
    fun unknownModelsKeepTheirCode() {
        assertEquals("Pixel 9", DeviceNames.marketingModel("Pixel 9"))
    }
}
