#pragma once
#include <tonemap/lut/LutChain.h>

#include <optional>
#include <string>

#include "ColorRenderProfile.h"

namespace rawrcam::tonemap_integration {

/** Loads Rawr-owned persisted LUT-profile metadata and its ordered .cube chain. */
[[nodiscard]] std::optional<tonemap::lut::LutChain> loadRenderTransformLut(ColorRenderProfile profile,
                                                                           const std::string& filesDir,
                                                                           const std::string& importedProfileId = {});

}  // namespace rawrcam::tonemap_integration
