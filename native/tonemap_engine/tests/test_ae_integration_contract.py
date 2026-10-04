#!/usr/bin/env python3
"""Static AE -> Tonemap range-contract fixture.

This intentionally does not import or vendor raw_auto_exposure. It represents the
application adapter contract only.
"""
import math
import pathlib
import sys
import numpy as np

ROOT = pathlib.Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "python"))
from tonemap_ref import MAX_AE_POST_GAIN, ParamsBaseline, render

AE_MAX_POST_GAIN_LINEAR_V1 = 16.0

def require(condition, message):
    if not condition:
        raise AssertionError(message)

def tonemap_accepts(gain):
    try:
        render(np.array([[0.18, 0.18, 0.18]], dtype=np.float64),
               ParamsBaseline(aePostGain=gain))
        return True
    except ValueError:
        return False

def main():
    require(MAX_AE_POST_GAIN == 16.0, "Tonemap maximum changed")
    require(AE_MAX_POST_GAIN_LINEAR_V1 <= MAX_AE_POST_GAIN,
            "V1 AE configuration is not representable by TonemapEngine")
    require(math.log2(AE_MAX_POST_GAIN_LINEAR_V1) == 4.0,
            "16x must equal +4 EV")

    # Representative decisions spanning the entire valid integration range,
    # including the exact upper boundary. Every valid fixture decision must be
    # representable by TonemapEngine.
    decisions = [1e-6, 0.5, 1.0, 2.0, 4.0, 8.0,
                 np.nextafter(16.0, 0.0), 16.0]
    require(all(0.0 < g <= AE_MAX_POST_GAIN_LINEAR_V1 for g in decisions),
            "fixture emitted a gain outside its configured AE ceiling")
    require(all(tonemap_accepts(float(g)) for g in decisions),
            "TonemapEngine rejected a gain valid under the V1 AE ceiling")

    require(not tonemap_accepts(float(np.nextafter(16.0, np.inf))),
            "TonemapEngine accepted a gain above 16x")
    require(not tonemap_accepts(32.0),
            "TonemapEngine accepted an incompatible AE decision")

    # A hypothetical AE configuration above the Tonemap ceiling is invalid for
    # this integration; the app adapter must reject the configuration itself.
    incompatible_ae_ceiling = 32.0
    require(incompatible_ae_ceiling > MAX_AE_POST_GAIN,
            "incompatible fixture setup is not actually incompatible")

    print("AE_TONEMAP_RANGE_CONTRACT_PASS maxPostGainLinear=16x equals_plus_4EV")

if __name__ == "__main__":
    main()
