#!/usr/bin/env python3
"""Independent transcription of the pinned RawTherapee manual dual-demosaic mask path.
Pinned sources: rtengine/dual_demosaic_RT.cc + rtengine/rt_algo.cc.
This file intentionally does not import tools/dual_reference.py.
"""
import numpy as np
_KAPPA=np.float32(24389.0/27.0)
_EPS=np.float32(216.0/24389.0)
_YR,_YG,_YB=(np.float32(0.212671),np.float32(0.715160),np.float32(0.072169))

def rgb2l(rgb):
    a=np.asarray(rgb,dtype=np.float32)
    y=(a[...,0]*_YR+a[...,1]*_YG+a[...,2]*_YB).astype(np.float32)
    l=np.empty_like(y)
    q=y<=_EPS
    l[q]=np.float32(327.68)*_KAPPA*y[q]
    l[~q]=np.float32(327.68)*(np.float32(116.0)*np.cbrt(y[~q]).astype(np.float32)-np.float32(16.0))
    return l

def _factor(v,t):
    x=np.float32(-16.0)+(np.float32(16.0)/np.float32(t))*np.float32(v)
    return np.float32(0.5)*(np.float32(1.0)+x/np.sqrt(np.float32(1.0)+x*x,dtype=np.float32))

def unblurred_mask(lum,threshold):
    lum=np.asarray(lum,dtype=np.float32); h,w=lum.shape
    if threshold==0: return np.ones_like(lum)
    out=np.empty_like(lum)
    scale=np.float32(0.0625/327.68)
    for j in range(2,h-2):
        for i in range(2,w-2):
            c=np.sqrt(np.float32((lum[j,i+1]-lum[j,i-1])**2+(lum[j+1,i]-lum[j-1,i])**2+(lum[j,i+2]-lum[j,i-2])**2+(lum[j+2,i]-lum[j-2,i])**2),dtype=np.float32)*scale
            out[j,i]=_factor(c,threshold)
    out[0:2,2:w-2]=out[2,2:w-2]
    out[h-2:h,2:w-2]=out[h-3,2:w-2]
    out[:,0]=out[:,1]=out[:,2]
    out[:,w-2]=out[:,w-1]=out[:,w-3]
    return out

def _coeffs(sigma=np.float32(2.0)):
    s=float(sigma)
    q=3.97156-4.14554*np.sqrt(1.0-0.26891*s) if s<2.5 else 0.98711*s-0.96330
    b0=1.57825+2.44413*q+1.4281*q*q+0.422205*q*q*q
    b1=(2.44413*q+2.85619*q*q+1.26661*q*q*q)/b0
    b2=(-1.4281*q*q-1.26661*q*q*q)/b0
    b3=(0.422205*q*q*q)/b0
    B=1.0-(b1+b2+b3)
    M=np.empty((3,3),dtype=np.float64)
    M[0]=[-b3*b1+1-b3*b3-b2,(b3+b1)*(b2+b3*b1),b3*(b1+b3*b2)]
    M[1]=[b1+b3*b2,-(b2-1)*(b2+b3*b1),-(b3*b1+b3*b3+b2-1)*b3]
    M[2]=[b3*b1+b2+b1*b1-b2*b2,b1*b2+b3*b2*b2-b1*b3*b3-b3*b3*b3-b3*b2+b3,b3*(b1+b3*b2)]
    M *= (1+b2+(b1-b3)*b3)/((1+b1-b2+b3)*(1-b1-b2-b3))
    return tuple(np.float32(z) for z in (b1,b2,b3,B)),M.astype(np.float32)

def _one(src,co,M):
    b1,b2,b3,B=co; src=np.asarray(src,dtype=np.float32); n=len(src); t=np.empty(n,dtype=np.float32)
    t[0]=src[0]*(B+b1+b2+b3)
    t[1]=B*src[1]+b1*t[0]+src[0]*(b2+b3)
    t[2]=B*src[2]+b1*t[1]+b2*t[0]+b3*src[0]
    for i in range(3,n): t[i]=B*src[i]+b1*t[i-1]+b2*t[i-2]+b3*t[i-3]
    e=src[-1]; d=np.array([t[-1]-e,t[-2]-e,t[-3]-e],np.float32); q=e+M@d
    t[-1]=q[0]; t[-2]=B*t[-2]+b1*t[-1]+b2*q[1]+b3*q[2]; t[-3]=B*t[-3]+b1*t[-2]+b2*t[-1]+b3*q[1]
    for i in range(n-4,-1,-1): t[i]=B*t[i]+b1*t[i+1]+b2*t[i+2]+b3*t[i+3]
    return t

def blur2(mask):
    co,M=_coeffs(); a=np.asarray(mask,dtype=np.float32); h,w=a.shape; tmp=np.empty_like(a); out=np.empty_like(a)
    for y in range(h): tmp[y]=_one(a[y],co,M)
    for x in range(w): out[:,x]=_one(tmp[:,x],co,M)
    return out

def render(rcd,vng,contrast_percent):
    r=np.asarray(rcd,dtype=np.float32); v=np.asarray(vng,dtype=np.float32)
    if contrast_percent==0: return r.copy(),np.ones(r.shape[:2],np.float32)
    threshold=np.float32(contrast_percent/100.0)
    mask=blur2(unblurred_mask(rgb2l(r),threshold))
    return (mask[...,None]*(r-v)+v).astype(np.float32),mask
