package com.rawr.camera.settings.model

/** Stable native IDs; paired gamut and transfer are inseparable recording profiles. */
enum class VideoLogProfile(val label: String, val nativeId: Int, val gamut: LutGamut, val transfer: LutTransfer) {
    LogC3("ARRI LogC3 EI800", 4, LutGamut.ArriWideGamut3, LutTransfer.LogC3),
    SLog3("Sony S-Log3", 5, LutGamut.SonySGamut3Cine, LutTransfer.SLog3),
    VLog("Panasonic V-Log", 6, LutGamut.PanasonicVGamut, LutTransfer.VLog),
    FLog2C("FUJIFILM F-Log2 C", 7, LutGamut.FujifilmFGamutC, LutTransfer.FLog2C),
    DaVinciIntermediate("DaVinci Intermediate", 8, LutGamut.DaVinciWideGamut, LutTransfer.DaVinciIntermediate)
}
