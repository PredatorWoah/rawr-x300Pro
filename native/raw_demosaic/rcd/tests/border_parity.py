#!/usr/bin/env python3
"""Exhaustive semantic parity gate for librtprocess bayerborder_demosaic().

The oracle below is a direct, compact restatement of border.cc behavior for a
valid 3-color Bayer CFA.  The implementation under test is tools.rcd_reference.
"""
from pathlib import Path
import sys
import numpy as np
ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'tools'))
from rcd_reference import PATTERNS, color, bayer_border, BORDER


def oracle(cfa, pattern, border=BORDER):
    h,w=cfa.shape
    rgb=np.full((h,w,3),np.nan,np.float32)

    def one(y,x):
        sums=np.zeros(6,np.float64)
        for yy in range(y-1,y+2):
            for xx in range(x-1,x+2):
                if yy < 0 or yy >= h or xx < 0 or xx >= w:
                    continue
                c=color(pattern,yy,xx)
                sums[c]+=float(cfa[yy,xx])
                sums[c+3]+=1.0
        c=color(pattern,y,x)
        if c==1:
            rgb[y,x,0]=sums[0]/sums[3]
            rgb[y,x,1]=cfa[y,x]
            rgb[y,x,2]=sums[2]/sums[5]
        else:
            rgb[y,x,1]=sums[1]/sums[4]
            if c==0:
                rgb[y,x,0]=cfa[y,x]
                rgb[y,x,2]=sums[2]/sums[5]
            else:
                rgb[y,x,0]=sums[0]/sums[3]
                rgb[y,x,2]=cfa[y,x]

    # Same four regions/order as librtprocess border.cc.
    for y in range(h):
        for x in range(border): one(y,x)
        for x in range(w-border,w): one(y,x)
    for y in range(border):
        for x in range(border,w-border): one(y,x)
    for y in range(h-border,h):
        for x in range(border,w-border): one(y,x)
    return rgb


def shader_semantics(cfa, pattern, y, x):
    h,w=cfa.shape
    sums=np.zeros(3,np.float64); counts=np.zeros(3,np.int32)
    for dy in (-1,0,1):
        for dx in (-1,0,1):
            yy=y+dy; xx=x+dx
            if xx<0 or yy<0 or xx>=w or yy>=h: continue
            c=color(pattern,yy,xx)
            sums[c]+=float(cfa[yy,xx]); counts[c]+=1
    own=color(pattern,y,x)
    out=sums/counts
    out[own]=float(cfa[y,x])
    return out.astype(np.float32)

rng=np.random.default_rng(0x524344)
for h,w in [(19,19),(20,22),(31,29),(64,66)]:
    cfa=rng.normal(0.35,0.8,size=(h,w)).astype(np.float32)  # include <0 and >1
    for pattern in PATTERNS:
        got=np.zeros((h,w,3),np.float32)
        bayer_border(cfa,pattern,got,BORDER)
        ref=oracle(cfa,pattern,BORDER)
        mask=np.zeros((h,w),bool)
        mask[:BORDER,:]=True; mask[-BORDER:,:]=True; mask[:,:BORDER]=True; mask[:,-BORDER:]=True
        err=np.max(np.abs(got[mask]-ref[mask]))
        if err != 0.0:
            raise SystemExit(f'RCD_BORDER_PARITY_FAIL cpu pattern={pattern} {w}x{h} max={err}')
        # Check every border pixel against the exact per-invocation GPU semantics.
        ys,xs=np.nonzero(mask)
        for y,x in zip(ys,xs):
            s=shader_semantics(cfa,pattern,int(y),int(x))
            if not np.array_equal(s,ref[y,x]):
                e=np.max(np.abs(s-ref[y,x]))
                raise SystemExit(f'RCD_BORDER_PARITY_FAIL shader pattern={pattern} p={x},{y} max={e}')
print('RCD_LIBRTPROCESS_BORDER_PARITY_PASS patterns=4 geometries=4 border=9')
