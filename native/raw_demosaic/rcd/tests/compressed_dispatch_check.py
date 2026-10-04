#!/usr/bin/env python3
# Validate half-width Bayer-site mapping used by Vulkan late passes.
patterns = {
    0: ((0,1),(1,2)), # RGGB
    1: ((1,0),(2,1)), # GRBG
    2: ((1,2),(0,1)), # GBRG
    3: ((2,1),(1,0)), # BGGR
}
def color(pattern,x,y): return patterns[pattern][y&1][x&1]
def map_x(pattern,w,y,hx,want_green):
    x0=2*hx
    x=x0 + (1 if ((color(pattern,x0,y)==1) != want_green) else 0)
    return x if x<w else None
# Exhaustive ordering/uniqueness on representative odd/even sizes.
for pattern in range(4):
    for w,h in [(19,19),(20,19),(19,20),(20,20),(63,65),(64,64)]:
        for want_green in (False,True):
            got=[]
            for y in range(h):
                for hx in range((w+1)//2):
                    x=map_x(pattern,w,y,hx,want_green)
                    if x is not None: got.append((x,y))
            expected=[(x,y) for y in range(h) for x in range(w) if (color(pattern,x,y)==1)==want_green]
            assert got==expected, (pattern,w,h,want_green)
# Production geometries: even widths guarantee exactly width/2 sites per row.
for pattern in range(4):
    for w,h in [(4080,3064),(4080,3072),(4096,3072)]:
        half=w//2
        for want_green in (False,True):
            for y in (0,1,h//2,h-2,h-1):
                first=map_x(pattern,w,y,0,want_green)
                last=map_x(pattern,w,y,half-1,want_green)
                assert first is not None and last is not None
                assert (color(pattern,first,y)==1)==want_green
                assert (color(pattern,last,y)==1)==want_green
            assert half*h==(w*h)//2
print('RCD_COMPRESSED_DISPATCH_PASS patterns=4 odd_even_and_production_geometries=true')
