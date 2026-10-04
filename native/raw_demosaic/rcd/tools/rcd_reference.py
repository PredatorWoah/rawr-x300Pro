#!/usr/bin/env python3
"""Small deterministic CPU RCD 2.3 reference for validation crops.

Independent scalar transcription of CarVac/librtprocess rcd.cc equations.
It supports two domains:
  * librtprocess: clamp CFA to [0,1], final RGB >= 0
  * rawr: no CFA/final clamp, preserving negative and >1 values
This is a validation oracle, not the production implementation.
"""
from __future__ import annotations
import argparse
from pathlib import Path
import numpy as np

EPS=1e-5
EPS2=1e-10
BORDER=9
PATTERNS={
    'RGGB': ((0,1),(1,2)),
    'GRBG': ((1,0),(2,1)),
    'GBRG': ((1,2),(0,1)),
    'BGGR': ((2,1),(1,0)),
}

def color(pattern,y,x): return PATTERNS[pattern][y&1][x&1]
def intp(a,b,c): return a*(b-c)+c

def bayer_border(cfa, pattern, rgb, border=BORDER):
    """Semantic port of librtprocess::bayerborder_demosaic().

    This intentionally follows border.cc's four regions: first columns, last
    columns, first rows (excluding the column bands), and last rows. Missing
    channels are the unweighted mean of same-color samples in the clipped 3x3
    neighborhood; the native CFA channel is copied exactly.
    """
    h,w=cfa.shape

    def write_pixel(y,x):
        sums=[0.0,0.0,0.0]
        counts=[0,0,0]
        for yy in range(y-1,y+2):
            for xx in range(x-1,x+2):
                if 0 <= yy < h and 0 <= xx < w:
                    c=color(pattern,yy,xx)
                    sums[c]+=float(cfa[yy,xx])
                    counts[c]+=1
        own=color(pattern,y,x)
        if own==1:
            rgb[y,x,0]=sums[0]/counts[0]
            rgb[y,x,1]=float(cfa[y,x])
            rgb[y,x,2]=sums[2]/counts[2]
        else:
            rgb[y,x,1]=sums[1]/counts[1]
            if own==0:
                rgb[y,x,0]=float(cfa[y,x])
                rgb[y,x,2]=sums[2]/counts[2]
            else:
                rgb[y,x,0]=sums[0]/counts[0]
                rgb[y,x,2]=float(cfa[y,x])

    for y in range(h):
        for x in range(border):
            write_pixel(y,x)
        for x in range(w-border,w):
            write_pixel(y,x)
    for y in range(border):
        for x in range(border,w-border):
            write_pixel(y,x)
    for y in range(h-border,h):
        for x in range(border,w-border):
            write_pixel(y,x)

def demosaic(cfa_in: np.ndarray, pattern='RGGB', mode='rawr') -> np.ndarray:
    cfa=np.asarray(cfa_in,dtype=np.float32).copy()
    if mode=='librtprocess': np.clip(cfa,0.0,1.0,out=cfa)
    h,w=cfa.shape
    if h<19 or w<19: raise ValueError('RCD requires at least 19x19')
    rgb=np.zeros((h,w,3),np.float32)
    for y in range(h):
        for x in range(w): rgb[y,x,color(pattern,y,x)]=cfa[y,x]
    vh=np.zeros((h,w),np.float32)
    pq=np.zeros((h,w),np.float32)

    def at(y,x): return float(cfa[y,x])
    def hp(y,x,dy,dx):
        z=(at(y-3*dy,x-3*dx)-at(y-dy,x-dx)-at(y+dy,x+dx)+at(y+3*dy,x+3*dx)) \
          -3.0*(at(y-2*dy,x-2*dx)+at(y+2*dy,x+2*dx))+6.0*at(y,x)
        return z*z
    for y in range(4,h-4):
        for x in range(4,w-4):
            vs=max(EPS2,hp(y-1,x,1,0)+hp(y,x,1,0)+hp(y+1,x,1,0))
            hs=max(EPS2,hp(y,x-1,0,1)+hp(y,x,0,1)+hp(y,x+1,0,1))
            vh[y,x]=vs/(vs+hs)
            ps=max(EPS2,hp(y-1,x-1,1,1)+hp(y,x,1,1)+hp(y+1,x+1,1,1))
            qs=max(EPS2,hp(y-1,x+1,1,-1)+hp(y,x,1,-1)+hp(y+1,x-1,1,-1))
            pq[y,x]=ps/(ps+qs)

    lpf=np.zeros((h,w),np.float32)
    for y in range(2,h-2):
        for x in range(2,w-2):
            if color(pattern,y,x)==1: continue
            lpf[y,x]=at(y,x)+0.5*(at(y-1,x)+at(y+1,x)+at(y,x-1)+at(y,x+1))+0.25*(at(y-1,x-1)+at(y-1,x+1)+at(y+1,x-1)+at(y+1,x+1))

    def refined(arr,y,x):
        center=float(arr[y,x]); neigh=0.25*(float(arr[y-1,x-1])+float(arr[y-1,x+1])+float(arr[y+1,x-1])+float(arr[y+1,x+1]))
        return neigh if abs(0.5-center)<abs(0.5-neigh) else center

    # G at R/B.
    for y in range(4,h-4):
        for x in range(4,w-4):
            if color(pattern,y,x)==1: continue
            cf=at(y,x)
            ng=EPS+abs(at(y-1,x)-at(y+1,x))+abs(cf-at(y-2,x))+abs(at(y-1,x)-at(y-3,x))+abs(at(y-2,x)-at(y-4,x))
            sg=EPS+abs(at(y-1,x)-at(y+1,x))+abs(cf-at(y+2,x))+abs(at(y+1,x)-at(y+3,x))+abs(at(y+2,x)-at(y+4,x))
            wg=EPS+abs(at(y,x-1)-at(y,x+1))+abs(cf-at(y,x-2))+abs(at(y,x-1)-at(y,x-3))+abs(at(y,x-2)-at(y,x-4))
            eg=EPS+abs(at(y,x-1)-at(y,x+1))+abs(cf-at(y,x+2))+abs(at(y,x+1)-at(y,x+3))+abs(at(y,x+2)-at(y,x+4))
            lp=float(lpf[y,x])
            ne=at(y-1,x)*(2*lp)/(EPS+lp+float(lpf[y-2,x]))
            se=at(y+1,x)*(2*lp)/(EPS+lp+float(lpf[y+2,x]))
            we=at(y,x-1)*(2*lp)/(EPS+lp+float(lpf[y,x-2]))
            ee=at(y,x+1)*(2*lp)/(EPS+lp+float(lpf[y,x+2]))
            ve=(sg*ne+ng*se)/(ng+sg); he=(wg*ee+eg*we)/(eg+wg)
            rgb[y,x,1]=intp(refined(vh,y,x),he,ve)

    # Opposite chroma at R/B.
    for y in range(4,h-4):
        for x in range(4,w-4):
            own=color(pattern,y,x)
            if own==1: continue
            c=2-own; G=float(rgb[y,x,1])
            nw=rgb[y-1,x-1]; ne=rgb[y-1,x+1]; sw=rgb[y+1,x-1]; se=rgb[y+1,x+1]
            NW=EPS+abs(float(nw[c]-se[c]))+abs(float(nw[c]-rgb[y-3,x-3,c]))+abs(G-float(rgb[y-2,x-2,1]))
            NE=EPS+abs(float(ne[c]-sw[c]))+abs(float(ne[c]-rgb[y-3,x+3,c]))+abs(G-float(rgb[y-2,x+2,1]))
            SW=EPS+abs(float(ne[c]-sw[c]))+abs(float(sw[c]-rgb[y+3,x-3,c]))+abs(G-float(rgb[y+2,x-2,1]))
            SE=EPS+abs(float(nw[c]-se[c]))+abs(float(se[c]-rgb[y+3,x+3,c]))+abs(G-float(rgb[y+2,x+2,1]))
            nwe=float(nw[c]-nw[1]); nee=float(ne[c]-ne[1]); swe=float(sw[c]-sw[1]); see=float(se[c]-se[1])
            pe=(NW*see+SE*nwe)/(NW+SE); qe=(NE*swe+SW*nee)/(NE+SW)
            rgb[y,x,c]=G+intp(refined(pq,y,x),qe,pe)

    # R/B at green.
    for y in range(4,h-4):
        for x in range(4,w-4):
            if color(pattern,y,x)!=1: continue
            G=float(rgb[y,x,1]); disc=refined(vh,y,x)
            N1=EPS+abs(G-float(rgb[y-2,x,1])); S1=EPS+abs(G-float(rgb[y+2,x,1])); W1=EPS+abs(G-float(rgb[y,x-2,1])); E1=EPS+abs(G-float(rgb[y,x+2,1]))
            for c in (0,2):
                n=rgb[y-1,x]; s=rgb[y+1,x]; ww=rgb[y,x-1]; e=rgb[y,x+1]
                sn=abs(float(n[c]-s[c])); ew=abs(float(ww[c]-e[c]))
                ng=N1+sn+abs(float(n[c]-rgb[y-3,x,c])); sg=S1+sn+abs(float(s[c]-rgb[y+3,x,c]))
                wg=W1+ew+abs(float(ww[c]-rgb[y,x-3,c])); eg=E1+ew+abs(float(e[c]-rgb[y,x+3,c]))
                ne=float(n[c]-n[1]); se=float(s[c]-s[1]); we=float(ww[c]-ww[1]); ee=float(e[c]-e[1])
                ve=(ng*se+sg*ne)/(ng+sg); he=(eg*we+wg*ee)/(eg+wg)
                rgb[y,x,c]=G+intp(disc,he,ve)

    bayer_border(cfa,pattern,rgb,BORDER)
    if mode=='librtprocess': np.maximum(rgb,0.0,out=rgb)
    return rgb

def main():
    ap=argparse.ArgumentParser(); ap.add_argument('input_npy'); ap.add_argument('output_npy'); ap.add_argument('--pattern',default='RGGB',choices=PATTERNS); ap.add_argument('--mode',default='rawr',choices=['rawr','librtprocess'])
    a=ap.parse_args(); cfa=np.load(a.input_npy); out=demosaic(cfa,a.pattern,a.mode); np.save(a.output_npy,out); print(f'RCD_REFERENCE_PASS shape={out.shape} min={out.min():.7g} max={out.max():.7g}')
if __name__=='__main__': main()
