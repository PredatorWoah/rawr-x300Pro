#!/usr/bin/env python3
"""CPU contract test for RawTherapee-style automatic Dual contrast selection.

This intentionally tests the algorithmic contract independently of Vulkan dispatch:
80x80 pass -> optional 40x40/10px pass -> +-10 refinement -> calcContrastThreshold.
"""
import math
import numpy as np

MIN_L=2000.0
MAX_L=20000.0
MIN_VAR=0.5
INF=float('inf')

def tile_variance(L,x0,y0,size):
    a=L[y0:y0+size,x0:x0+size].astype(np.float64,copy=False)
    avg=float(a.mean())
    if avg<MIN_L or avg>MAX_L:
        return INF
    var=float(np.square(a-avg).sum()/(a.size*avg))
    return var if var>=MIN_VAR else INF

def blend_factor(val,threshold):
    x=-16.0+(16.0/threshold)*val
    return 0.5*(1.0+x/math.sqrt(1.0+x*x))

def contrast_at(L,x,y):
    v=(float(L[y,x+1])-float(L[y,x-1]))**2
    v+=(float(L[y+1,x])-float(L[y-1,x]))**2
    v+=(float(L[y,x+2])-float(L[y,x-2]))**2
    v+=(float(L[y+2,x])-float(L[y-2,x]))**2
    return math.sqrt(v)*(0.0625/327.68)

def calc_threshold(L,x0,y0,size):
    limit=((size-4)*(size-4))/100.0
    for c in range(1,100):
        t=c/100.0
        total=0.0
        for y in range(y0+2,y0+size-2):
            for x in range(x0+2,x0+size-2):
                total += blend_factor(contrast_at(L,x,y),t)
        if total<=limit:
            return (c+1)/100.0
    return 1.01

def resolve(L):
    H,W=L.shape
    assert W>=80 and H>=80
    best=(INF,0,0)
    for y in range(0,(H//80)*80,80):
        for x in range(0,(W//80)*80,80):
            v=tile_variance(L,x,y,80)
            if v<best[0]: best=(v,x,y)
    if best[0]<=1.0:
        return calc_threshold(L,best[1],best[2],80),0,best

    nW=W//10-3; nH=H//10-3
    best40=(INF,0,0)
    for iy in range(nH):
        y=iy*10
        for ix in range(nW):
            x=ix*10
            v=tile_variance(L,x,y,40)
            if v<best40[0]: best40=(v,x,y)

    bx,by=best40[1],best40[2]
    refined=(INF,bx,by)
    for y in range(max(by-10,0),min(by+10,H-40)+1):
        for x in range(max(bx-10,0),min(bx+10,W-40)+1):
            v=tile_variance(L,x,y,40)
            if v<refined[0]: refined=(v,x,y)
    if refined[0]<=8.0:
        return calc_threshold(L,refined[1],refined[2],40),1,refined
    return 0.0,1,refined

def noise(shape,seed,sigma,mean=10000.0):
    rng=np.random.default_rng(seed)
    return (mean+rng.normal(0.0,sigma,shape)).astype(np.float32)

# Pass-0 fixture: normalized variance ~0.8, safely within RT's <=1 first-pass acceptance.
L0=noise((160,160),1,math.sqrt(0.8*10000.0))
t0,p0,b0=resolve(L0)
assert p0==0 and 0.0<t0<=1.01 and 0.5<=b0[0]<=1.0,(t0,p0,b0)

# Pass-1 fixture: globally rough image (>1 in 80px tiles), with one calmer 40px patch.
L1=noise((160,160),2,math.sqrt(3.0*10000.0))
L1[60:100,70:110]=noise((40,40),3,math.sqrt(0.8*10000.0))
t1,p1,b1=resolve(L1)
assert p1==1 and 0.0<t1<=1.01 and b1[0]<=8.0,(t1,p1,b1)

# No suitable luminance region must resolve to zero, which is exact RCD-only mask semantics.
Lbad=np.full((160,160),100.0,np.float32)
tb,pb,bb=resolve(Lbad)
assert tb==0.0 and pb==1,(tb,pb,bb)

# Dynamic scratch packing used by the shaders must fit the already-allocated full-res maskB.
for H in range(80,513):
    for W in range(80,513):
        n80=(W//80)*(H//80)
        n40=(W//10-3)*(H//10-3)
        assert n80+n40+441 <= W*H,(W,H,n80,n40)

print(f'DUAL_AUTO_CONTRAST_REFERENCE_PASS pass0={t0*100:.1f} pass1={t1*100:.1f} fallback={tb:.1f}')
