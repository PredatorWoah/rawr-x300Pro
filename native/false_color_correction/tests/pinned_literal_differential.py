#!/usr/bin/env python3
import pathlib,sys,numpy as np
sys.path.insert(0,str(pathlib.Path(__file__).resolve().parents[1]/"tools"))
from fcc_reference import fcc

def middle6(v):
    a=list(map(np.float32,v)); a.sort(); return a[1:5]
def med7(v):
    a=list(map(np.float32,v)); a.sort(); return a[3]
def literal_once(x):
    x=np.asarray(x,np.float32); h,w,_=x.shape
    Y=np.float32(.299)*x[...,0]+np.float32(.587)*x[...,1]+np.float32(.114)*x[...,2]
    I=np.float32(.596)*x[...,0]-np.float32(.275)*x[...,1]-np.float32(.321)*x[...,2]
    Q=np.float32(.212)*x[...,0]-np.float32(.523)*x[...,1]+np.float32(.311)*x[...,2]
    def sm(ch):
        o=ch.copy()
        for y in range(1,h-1):
            pre1=[ch[y-1,0],ch[y,0],ch[y+1,0]]
            pre2=[ch[y-1,1],ch[y,1],ch[y+1,1]]
            for j in range(1,w-2,2):
                post1=[ch[y-1,j+1],ch[y,j+1],ch[y+1,j+1]]
                mid=middle6(pre2+post1)
                o[y,j]=med7(pre1+mid)
                post2=[ch[y-1,j+2],ch[y,j+2],ch[y+1,j+2]]
                o[y,j+1]=med7(post2+mid)
                pre1,post1=post1,pre1
                pre2,post2=post2,pre2
            o[y,w-1]=ch[y,w-1]
            o[y,w-2]=ch[y,w-2]
        return o
    ii,qq=sm(I),sm(Q); out=x.copy(); k=np.float32(1/9)
    for y in range(1,h-1):
        for xx in range(w):
            if xx==0 or xx==w-1: i,q=ii[y,xx],qq[y,xx]
            else:
                i=np.sum(ii[y-1:y+2,xx-1:xx+2],dtype=np.float32)*k
                q=np.sum(qq[y-1:y+2,xx-1:xx+2],dtype=np.float32)*k
            out[y,xx,0]=Y[y,xx]+np.float32(.956)*i+np.float32(.621)*q
            out[y,xx,1]=Y[y,xx]-np.float32(.272)*i-np.float32(.647)*q
            out[y,xx,2]=Y[y,xx]-np.float32(1.105)*i+np.float32(1.702)*q
    return out
rng=np.random.default_rng(0xFCC)
for h,w in [(4,3),(7,8),(13,17),(16,18)]:
    x=rng.uniform(-.4,2.0,(h,w,4)).astype(np.float32);x[...,3]=1
    for steps in (1,2,3,4,6,8):
        a=x.copy()
        for _ in range(steps):
            a=literal_once(a)
        b=fcc(x,steps)
        err=np.max(np.abs(a-b))
        if err>3e-6: raise SystemExit(f"FCC_PINNED_LITERAL_DIFFERENTIAL_FAIL {w}x{h} s={steps} max={err}")
print('FCC_PINNED_LITERAL_DIFFERENTIAL_PASS')
