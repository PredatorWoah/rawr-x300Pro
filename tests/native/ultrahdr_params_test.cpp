// UltraHDR intent -> encode params mapping (host, no Vulkan link needed).
// Guards the split-brain fix: UltraHdrParams::toGainmapParams() must map
// every field, and its defaults must match GainmapParams defaults so render
// and mux can never drift apart field-by-field.
#include <cassert>
#include <cstdio>

#include "encoding/jpeg/JpegCaptureContext.h"

int main() {
    using namespace rawrcam::encoding::jpeg;
    const UltraHdrParams u;
    assert(!u.enabled);
    assert(u.gainmapQuality == 95);
    const gainmap::GainmapParams g = u.toGainmapParams();
    const gainmap::GainmapParams dflt;
    assert(g.minLog2 == dflt.minLog2);
    assert(g.maxLog2 == dflt.maxLog2);
    assert(g.gamma == dflt.gamma);
    assert(g.offsetSdr == dflt.offsetSdr);
    assert(g.offsetHdr == dflt.offsetHdr);
    assert(g.hdrCapacityMin == dflt.hdrCapacityMin);
    assert(g.hdrCapacityMax == dflt.hdrCapacityMax);
    assert(g.hdrExposure == dflt.hdrExposure);
    assert(g.clipBoost == dflt.clipBoost);
    assert(g.satProtect == dflt.satProtect);
    assert(g.multiChannelMap == dflt.multiChannelMap);
    assert(g.mapBlurSigma == dflt.mapBlurSigma);
    assert(g.glowStrength == dflt.glowStrength);
    assert(g.glowMax == dflt.glowMax);

    UltraHdrParams custom;
    custom.gainMapMinLog2 = 0.5f;
    custom.gainMapMaxLog2 = 3.0f;
    custom.gamma = 0.9f;
    custom.offsetSdr = 0.02f;
    custom.offsetHdr = 0.03f;
    custom.hdrCapacityMinLog2 = 1.0f;
    custom.hdrCapacityMaxLog2 = 3.0f;
    custom.satProtect = 0.85f;
    custom.multiChannelMap = true;
    custom.mapBlurSigma = 0.0f;
    custom.glowStrength = 0.7f;
    custom.glowMax = 3.0f;
    const gainmap::GainmapParams c = custom.toGainmapParams();
    assert(c.minLog2 == 0.5f);
    assert(c.maxLog2 == 3.0f);
    assert(c.gamma == 0.9f);
    assert(c.offsetSdr == 0.02f);
    assert(c.offsetHdr == 0.03f);
    assert(c.hdrCapacityMin == 1.0f);
    assert(c.hdrCapacityMax == 3.0f);
    assert(c.satProtect == 0.85f);
    assert(c.multiChannelMap == true);
    assert(c.mapBlurSigma == 0.0f);
    assert(c.glowStrength == 0.7f);
    assert(c.glowMax == 3.0f);

    std::printf("ultrahdr_params_test ok\n");
    return 0;
}
