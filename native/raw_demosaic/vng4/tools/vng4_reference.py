import numpy as np
from numba import njit
TERMS=np.array([-2, -2, 0, -1, 0, 1, -2, -2, 0, 0, 1, 1, -2, -1, -1, 0, 0, 1, -2, -1, 0, -1, 0, 2, -2, -1, 0, 0, 0, 3, -2, -1, 0, 1, 1, 1, -2, 0, 0, -1, 0, 6, -2, 0, 0, 0, 1, 2, -2, 0, 0, 1, 0, 3, -2, 1, -1, 0, 0, 4, -2, 1, 0, -1, 1, 4, -2, 1, 0, 0, 0, 6, -2, 1, 0, 1, 0, 2, -2, 2, 0, 0, 1, 4, -2, 2, 0, 1, 0, 4, -1, -2, -1, 0, 0, 128, -1, -2, 0, -1, 0, 1, -1, -2, 1, -1, 0, 1, -1, -2, 1, 0, 1, 1, -1, -1, -1, 1, 0, 136, -1, -1, 1, -2, 0, 64, -1, -1, 1, -1, 0, 34, -1, -1, 1, 0, 0, 51, -1, -1, 1, 1, 1, 17, -1, 0, -1, 2, 0, 8, -1, 0, 0, -1, 0, 68, -1, 0, 0, 1, 0, 17, -1, 0, 1, -2, 1, 64, -1, 0, 1, -1, 0, 102, -1, 0, 1, 0, 1, 34, -1, 0, 1, 1, 0, 51, -1, 0, 1, 2, 1, 16, -1, 1, 1, -1, 1, 68, -1, 1, 1, 0, 0, 102, -1, 1, 1, 1, 0, 34, -1, 1, 1, 2, 0, 16, -1, 2, 0, 1, 0, 4, -1, 2, 1, 0, 1, 4, -1, 2, 1, 1, 0, 4, 0, -2, 0, 0, 1, 128, 0, -1, 0, 1, 1, 136, 0, -1, 1, -2, 0, 64, 0, -1, 1, 0, 0, 17, 0, -1, 2, -2, 0, 64, 0, -1, 2, -1, 0, 32, 0, -1, 2, 0, 0, 48, 0, -1, 2, 1, 1, 16, 0, 0, 0, 2, 1, 8, 0, 0, 2, -2, 1, 64, 0, 0, 2, -1, 0, 96, 0, 0, 2, 0, 1, 32, 0, 0, 2, 1, 0, 48, 0, 0, 2, 2, 1, 16, 0, 1, 1, 0, 0, 68, 0, 1, 1, 2, 0, 16, 0, 1, 2, -1, 1, 64, 0, 1, 2, 0, 0, 96, 0, 1, 2, 1, 0, 32, 0, 1, 2, 2, 0, 16, 1, -2, 1, 0, 0, 128, 1, -1, 1, 1, 0, 136, 1, 0, 1, 2, 0, 8, 1, 0, 2, -1, 0, 64, 1, 0, 2, 1, 0, 16],dtype=np.int32).reshape(64,6)
CHOOD=np.array([-1, -1, -1, 0, -1, 1, 0, 1, 1, 1, 1, 0, 1, -1, 0, -1],dtype=np.int32).reshape(8,2)
# four-color CFA convention used by librtprocess VNG4: R,G1,B,G2
CFAS=np.array([[[0,1],[3,2]],[[1,0],[2,3]],[[3,2],[0,1]],[[2,3],[1,0]]],dtype=np.int32)
@njit(cache=True)
def _border(raw,out,cfa,b=3):
 h,w=raw.shape
 for y in range(h):
  for x in range(w):
   if x>=b and y>=b and x<w-b and y<h-b: continue
   s0=s1=s2=0.0; n0=n1=n2=0
   for dy in range(-1,2):
    yy=y+dy
    if yy<0 or yy>=h: continue
    for dx in range(-1,2):
     xx=x+dx
     if xx<0 or xx>=w: continue
     c=cfa[yy&1,xx&1]; c3=1 if (c&1) else c
     v=raw[yy,xx]
     if c3==0: s0+=v;n0+=1
     elif c3==1: s1+=v;n1+=1
     else:s2+=v;n2+=1
   own=cfa[y&1,x&1]; own3=1 if (own&1) else own
   out[y,x,0]=raw[y,x] if own3==0 else s0/n0
   out[y,x,1]=raw[y,x] if own3==1 else s1/n1
   out[y,x,2]=raw[y,x] if own3==2 else s2/n2
@njit(cache=True)
def demosaic(raw,pattern=0):
 raw=np.asarray(raw,np.float32);h,w=raw.shape;cfa=CFAS[pattern]
 image=np.zeros((h,w,4),np.float32)
 # native values everywhere
 for y in range(h):
  for x in range(w): image[y,x,cfa[y&1,x&1]]=raw[y,x]
 # reference first linear interpolation: only non-edge rows/cols
 for y in range(1,h-1):
  for x in range(1,w-1):
   own=cfa[y&1,x&1]; sums=np.zeros(4,np.float32); dens=np.zeros(4,np.float32)
   for dy in range(-1,2):
    for dx in range(-1,2):
     if dx==0 and dy==0: continue
     shift=(1 if dy==0 else 0)+(1 if dx==0 else 0); wt=np.float32(1<<shift); c=cfa[(y+dy)&1,(x+dx)&1]
     sums[c]+=raw[y+dy,x+dx]*wt; dens[c]+=wt
   for c in range(4):
    if c!=own: image[y,x,c]=sums[c]/dens[c]
 green=np.zeros((h,w),np.float32)
 # VNG green interpolation
 for y in range(2,h-2):
  for x in range(2,w-2):
   gvals=np.zeros(8,np.float32)
   for t in range(64):
    y1,x1,y2,x2,weight,mask=TERMS[t]
    c=cfa[(y+y1)&1,(x+x1)&1]
    if cfa[(y+y2)&1,(x+x2)&1]!=c: continue
    diag=2 if (cfa[y&1,(x+1)&1]==c and cfa[(y+1)&1,x&1]==c) else 1
    if abs(y1-y2)==diag and abs(x1-x2)==diag: continue
    diff=np.float32(abs(image[y+y1,x+x1,c]-image[y+y2,x+x2,c])*np.float32(1<<weight))
    for g in range(8):
     if mask&(1<<g): gvals[g]+=diff
   mn=np.float32(gvals.min()); mx=np.float32(gvals.max())
   # Canonical librtprocess spelling/order: min + max * 0.5f.
   th=np.float32(mn + np.float32(mx * np.float32(0.5))); color=cfa[y&1,x&1]; gv=image[y,x,color]; s0=np.float32(0.0);s1=np.float32(0.0);num=0
   if color&1:
    other=color^2
    for d in range(8):
     if gvals[d]<=th:
      yy,xx=CHOOD[d]; nc=cfa[(y+yy)&1,(x+xx)&1]
      if nc!=color and cfa[(y+2*yy)&1,(x+2*xx)&1]==color: s0+=gv+image[y+2*yy,x+2*xx,color]
      s1+=image[y+yy,x+xx,other];num+=1
    s0*=np.float32(0.5)
   else:
    for d in range(8):
     if gvals[d]<=th:
      yy,xx=CHOOD[d];nc=cfa[(y+yy)&1,(x+xx)&1]
      if nc!=color and cfa[(y+2*yy)&1,(x+2*xx)&1]==color: s0+=gv+image[y+2*yy,x+2*xx,color]
      s1+=image[y+yy,x+xx,1]+image[y+yy,x+xx,3];num+=1
   green[y,x]=np.float32(gv+(s1-s0)/(np.float32(2.0)*np.float32(num)))
 out=np.zeros((h,w,3),np.float32)
 # red/blue interpolation for interior rows/cols, exact librtprocess unclamped semantics
 for y in range(3,h-3):
  # determine which non-green channel is horizontal in this row
  c0=cfa[y&1,0];c1=cfa[y&1,1]; rowc=c0 if not(c0&1) else c1
  for x in range(3,w-3):
   c=cfa[y&1,x&1];g=green[y,x];out[y,x,1]=g
   if not(c&1):
    own=0 if c==0 else 2;opp=2 if own==0 else 0;out[y,x,own]=raw[y,x]
    rb=(raw[y-1,x-1]-green[y-1,x-1]+raw[y+1,x-1]-green[y+1,x-1]+raw[y-1,x+1]-green[y-1,x+1]+raw[y+1,x+1]-green[y+1,x+1])
    out[y,x,opp]=np.float32(g+np.float32(0.25)*rb)
   else:
    hor=np.float32(g+np.float32(0.5)*((raw[y,x-1]-green[y,x-1])+(raw[y,x+1]-green[y,x+1])))
    ver=np.float32(g+np.float32(0.5)*((raw[y-1,x]-green[y-1,x])+(raw[y+1,x]-green[y+1,x])))
    if rowc==0: out[y,x,0]=hor;out[y,x,2]=ver
    else: out[y,x,2]=hor;out[y,x,0]=ver
 _border(raw,out,cfa,3)
 return out
