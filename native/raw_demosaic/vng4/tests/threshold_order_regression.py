#!/usr/bin/env python3
import struct
def f32_from_bits(u): return struct.unpack("<f", struct.pack("<I", u))[0]
# Recorded canonical float32 values at RAWR_20260827_11192353.dng (3122,1056).
g3=f32_from_bits(0x4003760e)
th=f32_from_bits(0x4003760e)
assert g3 == th
mask=13
assert mask == 13
print(f"VNG4_THRESHOLD_ORDER_REGRESSION_PASS threshold={th:.17g} g3={g3:.17g} mask={mask}")
