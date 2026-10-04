#!/usr/bin/env python3
import numpy as np
KAPPA=np.float32(24389.0/27.0); EPS=np.float32(216.0/24389.0)
YCO=np.array([0.212671,0.715160,0.072169],dtype=np.float32)
def rgb_to_l(rgb):
    rgb=np.asarray(rgb,dtype=np.float32); y=np.sum(rgb[...,:3]*YCO,axis=-1,dtype=np.float32)
    out=np.empty_like(y)
    lo=y<=EPS
    out[lo]=np.float32(327.68)*KAPPA*y[lo]
    out[~lo]=np.float32(327.68)*(np.float32(116.0)*np.cbrt(y[~lo]).astype(np.float32)-np.float32(16.0))
    return out

def blend_factor(val,threshold):
    val=np.asarray(val,dtype=np.float32); t=np.float32(threshold)
    x=np.float32(-16.0)+(np.float32(16.0)/t)*val
    return np.float32(0.5)*(np.float32(1.0)+x/np.sqrt(np.float32(1.0)+x*x,dtype=np.float32))

def raw_mask(lum,threshold):
    lum=np.asarray(lum,dtype=np.float32); h,w=lum.shape
    if threshold==0: return np.ones_like(lum)
    yy=np.clip(np.arange(h),2,h-3); xx=np.clip(np.arange(w),2,w-3)
    y,x=np.meshgrid(yy,xx,indexing='ij')
    c=np.sqrt((lum[y,x+1]-lum[y,x-1])**2+(lum[y+1,x]-lum[y-1,x])**2+(lum[y,x+2]-lum[y,x-2])**2+(lum[y+2,x]-lum[y-2,x])**2,dtype=np.float32)*np.float32(0.0625/327.68)
    return blend_factor(c,np.float32(threshold))

def yvv_coeffs(sigma=2.0):
    s=float(sigma); q=3.97156-4.14554*np.sqrt(1.0-0.26891*s) if s<2.5 else 0.98711*s-0.96330
    b0=1.57825+2.44413*q+1.4281*q*q+0.422205*q*q*q
    b1=(2.44413*q+2.85619*q*q+1.26661*q*q*q)/b0
    b2=(-1.4281*q*q-1.26661*q*q*q)/b0
    b3=(0.422205*q*q*q)/b0; B=1.0-(b1+b2+b3)
    M=np.empty((3,3),dtype=np.float64)
    M[0]=[-b3*b1+1-b3*b3-b2,(b3+b1)*(b2+b3*b1),b3*(b1+b3*b2)]
    M[1]=[b1+b3*b2,-(b2-1)*(b2+b3*b1),-(b3*b1+b3*b3+b2-1)*b3]
    M[2]=[b3*b1+b2+b1*b1-b2*b2,b1*b2+b3*b2*b2-b1*b3*b3-b3*b3*b3-b3*b2+b3,b3*(b1+b3*b2)]
    # RawTherapee SSE/RT_SIMDE path used by gaussianBlur(GAUSS_STANDARD).
    M *= (1+b2+(b1-b3)*b3)/((1+b1-b2+b3)*(1-b1-b2-b3))
    return tuple(np.float32(v) for v in (b1,b2,b3,B)),M.astype(np.float32)

def _recursive_1d(src, coeffs, M):
    b1,b2,b3,B=coeffs; src=np.asarray(src,dtype=np.float32); n=src.size; t=np.empty(n,dtype=np.float32)
    t[0]=src[0]*(B+b1+b2+b3); t[1]=B*src[1]+b1*t[0]+src[0]*(b2+b3); t[2]=B*src[2]+b1*t[1]+b2*t[0]+b3*src[0]
    for i in range(3,n): t[i]=B*src[i]+b1*t[i-1]+b2*t[i-2]+b3*t[i-3]
    edge=src[-1]; d=np.array([t[-1]-edge,t[-2]-edge,t[-3]-edge],dtype=np.float32); q=edge+M@d
    t[-1]=q[0]; t[-2]=B*t[-2]+b1*t[-1]+b2*q[1]+b3*q[2]; t[-3]=B*t[-3]+b1*t[-2]+b2*t[-1]+b3*q[1]
    for i in range(n-4,-1,-1): t[i]=B*t[i]+b1*t[i+1]+b2*t[i+2]+b3*t[i+3]
    return t

def gaussian_rt(mask):
    coeffs,M=yvv_coeffs(2.0); m=np.asarray(mask,dtype=np.float32); h,w=m.shape; tmp=np.empty_like(m); out=np.empty_like(m)
    for y in range(h): tmp[y]=_recursive_1d(m[y],coeffs,M)
    for x in range(w): out[:,x]=_recursive_1d(tmp[:,x],coeffs,M)
    return out

def dual_blend(rcd,vng,contrast_percent):
    rcd=np.asarray(rcd,dtype=np.float32); vng=np.asarray(vng,dtype=np.float32)
    if contrast_percent==0: return rcd.copy(),np.ones(rcd.shape[:2],dtype=np.float32)
    mask=gaussian_rt(raw_mask(rgb_to_l(rcd),np.float32(contrast_percent/100.0)))
    out=mask[...,None]*(rcd-vng)+vng
    return out.astype(np.float32),mask
