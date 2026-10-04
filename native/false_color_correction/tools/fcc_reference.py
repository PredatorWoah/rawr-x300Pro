#!/usr/bin/env python3
import numpy as np

def _middle4of6(a):
    """Pinned RawTherapee median.h::middle4of6 algebra, float32."""
    a=np.asarray(a,dtype=np.float32)
    r0=np.minimum(a[...,1],a[...,2])
    r1=np.maximum(a[...,1],a[...,2])
    left=np.minimum(a[...,0],r1)
    r1=np.maximum(a[...,0],r1)
    tmp=np.minimum(left,r0)
    r0=np.maximum(left,r0)
    left=tmp
    r3=np.minimum(a[...,4],a[...,5])
    right=np.maximum(a[...,4],a[...,5])
    r2=np.minimum(a[...,3],right)
    right=np.maximum(a[...,3],right)
    tmp=np.minimum(r2,r3)
    r3=np.maximum(r2,r3)
    r2=np.maximum(left,tmp)
    r1=np.minimum(r1,right)
    return np.stack([r0,r1,r2,r3],axis=-1).astype(np.float32)

def _median7(a):
    return np.partition(a,3,axis=-1)[...,3]

def _to_yiq(rgb):
    rgb=np.asarray(rgb,dtype=np.float32)
    y=np.float32(.299)*rgb[...,0]+np.float32(.587)*rgb[...,1]+np.float32(.114)*rgb[...,2]
    i=np.float32(.596)*rgb[...,0]-np.float32(.275)*rgb[...,1]-np.float32(.321)*rgb[...,2]
    q=np.float32(.212)*rgb[...,0]-np.float32(.523)*rgb[...,1]+np.float32(.311)*rgb[...,2]
    return y.astype(np.float32),i.astype(np.float32),q.astype(np.float32)

def _strong_median(ch):
    ch=np.asarray(ch,dtype=np.float32)
    h,w=ch.shape
    out=ch.copy()
    # Exact serial non-SSE row/column semantics of pinned RT: top/bottom untouched,
    # x=0 and final two columns untouched.
    for y in range(1,h-1):
        for x in range(1,w-2):
            if x & 1:
                six=np.asarray([ch[y-1,x],ch[y,x],ch[y+1,x],
                                ch[y-1,x+1],ch[y,x+1],ch[y+1,x+1]],dtype=np.float32)
                mid=_middle4of6(six)
                vals=np.asarray([ch[y-1,x-1],ch[y,x-1],ch[y+1,x-1],*mid],dtype=np.float32)
            else:
                six=np.asarray([ch[y-1,x-1],ch[y,x-1],ch[y+1,x-1],
                                ch[y-1,x],ch[y,x],ch[y+1,x]],dtype=np.float32)
                mid=_middle4of6(six)
                vals=np.asarray([ch[y-1,x+1],ch[y,x+1],ch[y+1,x+1],*mid],dtype=np.float32)
            out[y,x]=np.partition(vals,3)[3]
    return out

def fcc_once(rgb, edge_sigma=0.0, chroma_bound=0.0):
    x=np.asarray(rgb,dtype=np.float32)
    h,w,_=x.shape
    y,i,q=_to_yiq(x)
    im=_strong_median(i); qm=_strong_median(q)
    out=x.copy()
    # RT leaves rows 0,H-1 untouched.
    for yy in range(1,h-1):
        # First/last columns: unblurred chroma reconstruction.
        for xx in (0,w-1):
            I,Q=im[yy,xx],qm[yy,xx]
            out[yy,xx,0]=y[yy,xx]+np.float32(.956)*I+np.float32(.621)*Q
            out[yy,xx,1]=y[yy,xx]-np.float32(.272)*I-np.float32(.647)*Q
            out[yy,xx,2]=y[yy,xx]-np.float32(1.105)*I+np.float32(1.702)*Q
        for xx in range(1,w-1):
            if edge_sigma and edge_sigma > 0:
                yc=float(y[yy,xx]); inv=1.0/max(float(edge_sigma)**2,1e-8)
                acc_i=np.float32(0); acc_q=np.float32(0); ws=np.float32(0)
                for dy in (-1,0,1):
                    for dx in (-1,0,1):
                        yn=float(y[yy+dy,xx+dx]); d=yn-yc
                        wt=np.float32(np.exp(-d*d*inv))
                        acc_i+=wt*im[yy+dy,xx+dx]; acc_q+=wt*qm[yy+dy,xx+dx]; ws+=wt
                I=acc_i/max(ws,np.float32(1e-6)); Q=acc_q/max(ws,np.float32(1e-6))
            else:
                I=np.sum(im[yy-1:yy+2,xx-1:xx+2],dtype=np.float32)*np.float32(1/9)
                Q=np.sum(qm[yy-1:yy+2,xx-1:xx+2],dtype=np.float32)*np.float32(1/9)
            if chroma_bound and chroma_bound > 0:
                s=_bound_scale(float(y[yy,xx]),float(I),float(Q))
                I*=s; Q*=s
            out[yy,xx,0]=y[yy,xx]+np.float32(.956)*I+np.float32(.621)*Q
            out[yy,xx,1]=y[yy,xx]-np.float32(.272)*I-np.float32(.647)*Q
            out[yy,xx,2]=y[yy,xx]-np.float32(1.105)*I+np.float32(1.702)*Q
    return out

def _from_yiq(Y,I,Q):
    return (Y+np.float32(.956)*I+np.float32(.621)*Q,
            Y-np.float32(.272)*I-np.float32(.647)*Q,
            Y-np.float32(1.105)*I+np.float32(1.702)*Q)

def _bound_scale(Y,I,Q):
    r,g,b=_from_yiq(np.float32(Y),np.float32(I),np.float32(Q))
    if 0.0 <= r <= 1.0 and 0.0 <= g <= 1.0 and 0.0 <= b <= 1.0:
        return np.float32(1.0)
    lo,hi=np.float32(0.0),np.float32(1.0)
    for _ in range(4):
        mid=np.float32(0.5*(lo+hi))
        r,g,b=_from_yiq(np.float32(Y),np.float32(mid*I),np.float32(mid*Q))
        if 0.0 <= r <= 1.0 and 0.0 <= g <= 1.0 and 0.0 <= b <= 1.0:
            lo=mid
        else:
            hi=mid
    return lo

def fcc(rgb,steps=1,edge_sigma=0.0,chroma_bound=0.0):
    out=np.asarray(rgb,dtype=np.float32)
    for _ in range(int(steps)):
        out=fcc_once(out,edge_sigma=edge_sigma,chroma_bound=chroma_bound)
    return out
