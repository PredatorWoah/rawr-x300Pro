
#include <cstddef>
#include <iostream>
#include <type_traits>

#include "monitoring_overlays/config_types.h"
#include "monitoring_overlays/types.h"
using namespace monitoring_overlays;

int main() {
    static_assert(std::is_standard_layout_v<ColorRgba>);
    static_assert(std::is_standard_layout_v<FalseColorRange>);
    static_assert(sizeof(ColorRgba) == 16);
    static_assert(sizeof(FalseColorRange) == 24);
    static_assert(kMaxFalseColorRanges == 16);

    MonitoringOverlaysCreateInfo ci{};
    if (ci.rawWorkgroupSizeX != 16 || ci.rawWorkgroupSizeY != 16 || ci.focusWorkgroupSizeX != 8 ||
        ci.focusWorkgroupSizeY != 8 || ci.falseColorWorkgroupSizeX != 16 || ci.falseColorWorkgroupSizeY != 16 ||
        ci.tonemapShadowWorkgroupSizeX != 16 || ci.tonemapShadowWorkgroupSizeY != 16 ||
        ci.combinedWorkgroupSizeX != 8 || ci.combinedWorkgroupSizeY != 8) {
        std::cerr << "ABI_VALIDATION_FAIL workgroup ABI\n";
        return 1;
    }
    if (offsetof(RawStateRecordInfo, commandBuffer) != 0 || offsetof(FocusPeakingRecordInfo, commandBuffer) != 0 ||
        offsetof(FalseColorRecordInfo, commandBuffer) != 0 || offsetof(CombinedRecordInfo, commandBuffer) != 0) {
        std::cerr << "ABI_VALIDATION_FAIL record-info layout\n";
        return 1;
    }
    std::cout << "ABI_VALIDATION_PASS"
              << " ColorRgba=" << sizeof(ColorRgba) << " FalseColorRange=" << sizeof(FalseColorRange)
              << " FalseColorParams=" << sizeof(FalseColorParams)
              << " CombinedRecordInfo=" << sizeof(CombinedRecordInfo) << "\n";
    return 0;
}
