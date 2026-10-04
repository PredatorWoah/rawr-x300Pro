#!/usr/bin/env python3
"""Pure RCD CPU oracle core. No DNG or TIFF dependencies.

This module intentionally contains only NumPy/Numba and the verified Rawr-domain
RCD + librtprocess border semantics so synthetic geometry gates do not inherit
photographic file-format dependencies.
"""
import numpy as np
from numba import njit

EPS=np.float32(1e-5); EPS2=np.float32(1e-10); BORDER=9

@njit(cache=True)
def color_rg(y,x):
    if (y & 1)==0:
        return 0 if (x & 1)==0 else 1
    return 1 if (x & 1)==0 else 2

@njit(cache=True)
def hp(a,y,x,dy,dx):
    z=(a[y-3*dy,x-3*dx]-a[y-dy,x-dx]-a[y+dy,x+dx]+a[y+3*dy,x+3*dx]) - np.float32(3.0)*(a[y-2*dy,x-2*dx]+a[y+2*dy,x+2*dx]) + np.float32(6.0)*a[y,x]
    return z*z

@njit(cache=True)
def refined(arr,y,x):
    c=arr[y,x]
    n=np.float32(0.25)*(arr[y-1,x-1]+arr[y-1,x+1]+arr[y+1,x-1]+arr[y+1,x+1])
    return n if abs(np.float32(0.5)-c)<abs(np.float32(0.5)-n) else c

@njit(cache=True)
def border_pixel(a,rgb,y,x):
    h,w=a.shape; sums=np.zeros(3,np.float32); counts=np.zeros(3,np.int32)
    for yy in range(y-1,y+2):
        for xx in range(x-1,x+2):
            if yy<0 or yy>=h or xx<0 or xx>=w: continue
            c=color_rg(yy,xx); sums[c]+=a[yy,xx]; counts[c]+=1
    own=color_rg(y,x)
    for c in range(3): rgb[y,x,c]=sums[c]/counts[c]
    rgb[y,x,own]=a[y,x]

@njit(cache=True)
def demosaic_rg(a):
    h,w=a.shape
    rgb=np.zeros((h,w,3),np.float32); vh=np.zeros((h,w),np.float32); pq=np.zeros((h,w),np.float32); lpf=np.zeros((h,w),np.float32)
    for y in range(h):
        for x in range(w): rgb[y,x,color_rg(y,x)]=a[y,x]
    for y in range(4,h-4):
        for x in range(4,w-4):
            vs=max(EPS2,hp(a,y-1,x,1,0)+hp(a,y,x,1,0)+hp(a,y+1,x,1,0)); hs=max(EPS2,hp(a,y,x-1,0,1)+hp(a,y,x,0,1)+hp(a,y,x+1,0,1)); vh[y,x]=vs/(vs+hs)
            ps=max(EPS2,hp(a,y-1,x-1,1,1)+hp(a,y,x,1,1)+hp(a,y+1,x+1,1,1)); qs=max(EPS2,hp(a,y-1,x+1,1,-1)+hp(a,y,x,1,-1)+hp(a,y+1,x-1,1,-1)); pq[y,x]=ps/(ps+qs)
    for y in range(2,h-2):
        for x in range(2,w-2):
            if color_rg(y,x)==1: continue
            lpf[y,x]=a[y,x]+np.float32(0.5)*(a[y-1,x]+a[y+1,x]+a[y,x-1]+a[y,x+1])+np.float32(0.25)*(a[y-1,x-1]+a[y-1,x+1]+a[y+1,x-1]+a[y+1,x+1])
    for y in range(4,h-4):
        for x in range(4,w-4):
            if color_rg(y,x)==1: continue
            cf=a[y,x]
            ng=EPS+abs(a[y-1,x]-a[y+1,x])+abs(cf-a[y-2,x])+abs(a[y-1,x]-a[y-3,x])+abs(a[y-2,x]-a[y-4,x])
            sg=EPS+abs(a[y-1,x]-a[y+1,x])+abs(cf-a[y+2,x])+abs(a[y+1,x]-a[y+3,x])+abs(a[y+2,x]-a[y+4,x])
            wg=EPS+abs(a[y,x-1]-a[y,x+1])+abs(cf-a[y,x-2])+abs(a[y,x-1]-a[y,x-3])+abs(a[y,x-2]-a[y,x-4])
            eg=EPS+abs(a[y,x-1]-a[y,x+1])+abs(cf-a[y,x+2])+abs(a[y,x+1]-a[y,x+3])+abs(a[y,x+2]-a[y,x+4])
            lp=lpf[y,x]; ne=a[y-1,x]*(np.float32(2)*lp)/(EPS+lp+lpf[y-2,x]); se=a[y+1,x]*(np.float32(2)*lp)/(EPS+lp+lpf[y+2,x]); we=a[y,x-1]*(np.float32(2)*lp)/(EPS+lp+lpf[y,x-2]); ee=a[y,x+1]*(np.float32(2)*lp)/(EPS+lp+lpf[y,x+2])
            ve=(sg*ne+ng*se)/(ng+sg); he=(wg*ee+eg*we)/(eg+wg); d=refined(vh,y,x); rgb[y,x,1]=d*(he-ve)+ve
    for y in range(4,h-4):
        for x in range(4,w-4):
            own=color_rg(y,x)
            if own==1: continue
            c=2-own; G=rgb[y,x,1]
            NW=EPS+abs(rgb[y-1,x-1,c]-rgb[y+1,x+1,c])+abs(rgb[y-1,x-1,c]-rgb[y-3,x-3,c])+abs(G-rgb[y-2,x-2,1])
            NE=EPS+abs(rgb[y-1,x+1,c]-rgb[y+1,x-1,c])+abs(rgb[y-1,x+1,c]-rgb[y-3,x+3,c])+abs(G-rgb[y-2,x+2,1])
            SW=EPS+abs(rgb[y-1,x+1,c]-rgb[y+1,x-1,c])+abs(rgb[y+1,x-1,c]-rgb[y+3,x-3,c])+abs(G-rgb[y+2,x-2,1])
            SE=EPS+abs(rgb[y-1,x-1,c]-rgb[y+1,x+1,c])+abs(rgb[y+1,x+1,c]-rgb[y+3,x+3,c])+abs(G-rgb[y+2,x+2,1])
            nwe=rgb[y-1,x-1,c]-rgb[y-1,x-1,1]; nee=rgb[y-1,x+1,c]-rgb[y-1,x+1,1]; swe=rgb[y+1,x-1,c]-rgb[y+1,x-1,1]; see=rgb[y+1,x+1,c]-rgb[y+1,x+1,1]
            pe=(NW*see+SE*nwe)/(NW+SE); qe=(NE*swe+SW*nee)/(NE+SW); d=refined(pq,y,x); rgb[y,x,c]=G+d*(qe-pe)+pe
    for y in range(4,h-4):
        for x in range(4,w-4):
            if color_rg(y,x)!=1: continue
            G=rgb[y,x,1]; d=refined(vh,y,x); N1=EPS+abs(G-rgb[y-2,x,1]); S1=EPS+abs(G-rgb[y+2,x,1]); W1=EPS+abs(G-rgb[y,x-2,1]); E1=EPS+abs(G-rgb[y,x+2,1])
            for c in (0,2):
                sn=abs(rgb[y-1,x,c]-rgb[y+1,x,c]); ew=abs(rgb[y,x-1,c]-rgb[y,x+1,c]); ng=N1+sn+abs(rgb[y-1,x,c]-rgb[y-3,x,c]); sg=S1+sn+abs(rgb[y+1,x,c]-rgb[y+3,x,c]); wg=W1+ew+abs(rgb[y,x-1,c]-rgb[y,x-3,c]); eg=E1+ew+abs(rgb[y,x+1,c]-rgb[y,x+3,c]); ne=rgb[y-1,x,c]-rgb[y-1,x,1]; se=rgb[y+1,x,c]-rgb[y+1,x,1]; we=rgb[y,x-1,c]-rgb[y,x-1,1]; ee=rgb[y,x+1,c]-rgb[y,x+1,1]; ve=(ng*se+sg*ne)/(ng+sg); he=(eg*we+wg*ee)/(eg+wg); rgb[y,x,c]=G+d*(he-ve)+ve
    for y in range(h):
        for x in range(BORDER): border_pixel(a,rgb,y,x)
        for x in range(w-BORDER,w): border_pixel(a,rgb,y,x)
    for y in range(BORDER):
        for x in range(BORDER,w-BORDER): border_pixel(a,rgb,y,x)
    for y in range(h-BORDER,h):
        for x in range(BORDER,w-BORDER): border_pixel(a,rgb,y,x)
    return rgb

