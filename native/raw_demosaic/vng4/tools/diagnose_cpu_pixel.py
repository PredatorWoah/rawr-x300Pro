#!/usr/bin/env python3
import argparse, json
from pathlib import Path
import sys
import numpy as np
import tifffile
sys.path.insert(0, str(Path(__file__).resolve().parent))
from vng4_reference import TERMS, CHOOD, CFAS
from full_dng_oracle import PATTERNS, rational_values, expand_black, cfa_bytes

def main():
    ap=argparse.ArgumentParser()
    ap.add_argument('dng'); ap.add_argument('--x',type=int,required=True); ap.add_argument('--y',type=int,required=True)
    a=ap.parse_args()
    with tifffile.TiffFile(a.dng) as tf:
        page=tf.pages[0]; raw=page.asarray()
        white=np.float32(np.asarray(page.tags['WhiteLevel'].value).reshape(-1)[0])
        blacks=expand_black(rational_values(page.tags['BlackLevel'].value))
        cfa_key=cfa_bytes(page.tags['CFAPattern'].value)
    if cfa_key not in PATTERNS: raise SystemExit(f'VNG4_PIXEL_DIAGNOSTIC_UNSUPPORTED_CFA {list(cfa_key)}')
    pattern,_=PATTERNS[cfa_key]; cfa=CFAS[pattern]; h,w=raw.shape; x,y=a.x,a.y
    if not (2 <= x < w-2 and 2 <= y < h-2): raise SystemExit('target must be at least 2 pixels from image edge')
    yy=np.arange(h,dtype=np.int32)[:,None]&1; xx=np.arange(w,dtype=np.int32)[None,:]&1
    parity=yy*2+xx; b=np.take(np.asarray(blacks,np.float32),parity)
    norm=(raw.astype(np.float32)-b)/(white-b)
    gpu_wire=(norm*np.float32(255.0)).astype(np.float32)
    norm=(gpu_wire*np.float32(1.0/255.0)).astype(np.float32)
    # Canonical first interpolation for the 5x5 neighborhood needed by the green stage.
    image=np.zeros((5,5,4),np.float32)
    for Y in range(y-2,y+3):
        for X in range(x-2,x+3):
            ly,lx=Y-(y-2),X-(x-2); own=int(cfa[Y&1,X&1]); image[ly,lx,own]=norm[Y,X]
            if 0 < Y < h-1 and 0 < X < w-1:
                sums=np.zeros(4,np.float32); dens=np.zeros(4,np.float32)
                for dy in range(-1,2):
                    for dx in range(-1,2):
                        if dx==0 and dy==0: continue
                        wt=np.float32(1 << ((1 if dy==0 else 0)+(1 if dx==0 else 0)))
                        cc=int(cfa[(Y+dy)&1,(X+dx)&1])
                        sums[cc]=np.float32(sums[cc]+np.float32(norm[Y+dy,X+dx]*wt)); dens[cc]=np.float32(dens[cc]+wt)
                for cc in range(4):
                    if cc!=own: image[ly,lx,cc]=np.float32(sums[cc]/dens[cc])
    def w4(Y,X,c): return image[Y-(y-2),X-(x-2),c]
    gv=np.zeros(8,np.float32); term_d=[]; term_g3=[]; term_a=[]; term_b=[]
    for row in TERMS:
        y1,x1,y2,x2,weight,mask=(int(z) for z in row); cc=int(cfa[(y+y1)&1,(x+x1)&1])
        if int(cfa[(y+y2)&1,(x+x2)&1]) != cc: term_d.append(None); term_g3.append(float(gv[3])); term_a.append(None); term_b.append(None); continue
        diag=2 if (int(cfa[y&1,(x+1)&1])==cc and int(cfa[(y+1)&1,x&1])==cc) else 1
        if abs(y1-y2)==diag and abs(x1-x2)==diag: term_d.append(None); term_g3.append(float(gv[3])); term_a.append(None); term_b.append(None); continue
        va=np.float32(w4(y+y1,x+x1,cc)); vb=np.float32(w4(y+y2,x+x2,cc)); diff=np.float32(abs(np.float32(va-vb))*np.float32(1<<weight)); term_a.append(float(va)); term_b.append(float(vb))
        for g in range(8):
            if mask & (1<<g): gv[g]=np.float32(gv[g]+diff)
        term_d.append(float(diff)); term_g3.append(float(gv[3]))
    mn=np.float32(gv.min()); mx=np.float32(gv.max()); th=np.float32(mn + np.float32(mx * np.float32(.5)))
    color=int(cfa[y&1,x&1]); greenval=np.float32(w4(y,x,color)); s0=np.float32(0); s1=np.float32(0); num=0; accepted=0
    for g in range(8):
        if gv[g] <= th:
            accepted |= 1<<g; dy,dx=(int(z) for z in CHOOD[g]); nc=int(cfa[(y+dy)&1,(x+dx)&1])
            if nc!=color and int(cfa[(y+2*dy)&1,(x+2*dx)&1])==color: s0=np.float32(s0+np.float32(greenval+w4(y+2*dy,x+2*dx,color)))
            if color&1: s1=np.float32(s1+w4(y+dy,x+dx,color^2))
            else: s1=np.float32(s1+np.float32(w4(y+dy,x+dx,1)+w4(y+dy,x+dx,3)))
            num += 1
    if color&1: s0=np.float32(s0*np.float32(.5))
    result=np.float32(greenval+np.float32((s1-s0)/(np.float32(2)*np.float32(num))))
    out={'x':x,'y':y,'centerColor':color,'gval':[float(z) for z in gv],'min':float(mn),'max':float(mx),'threshold':float(th),'accepted_mask':accepted,'accepted_bits':format(accepted,'08b'),'num':num,'greenval':float(greenval),'sum0':float(s0),'sum1':float(s1),'green_result':float(result)}
    print('VNG4_CPU_PIXEL_DIAGNOSTIC '+json.dumps(out,separators=(',',':')))
    print('VNG4_CPU_TERM_TRACE '+json.dumps({'d':term_d,'g3':term_g3,'a':term_a,'b':term_b},separators=(',',':')))
    print('VNG4_CPU_WORKING4_5X5 '+json.dumps({'values':[float(z) for z in image.reshape(-1)]},separators=(',',':')))
    cand_direct=[]; cand_recip=[]; cand_sum=[]; cand_den=[]
    for Y in range(y-2,y+3):
        for X in range(x-2,x+3):
            own=int(cfa[Y&1,X&1])
            for cc0 in range(4):
                sm=np.float32(0); dn=np.float32(0)
                for dy in range(-1,2):
                    for dx in range(-1,2):
                        if dx==0 and dy==0: continue
                        wt=np.float32(1 << ((1 if dy==0 else 0)+(1 if dx==0 else 0)))
                        rc=int(cfa[(Y+dy)&1,(X+dx)&1])
                        if rc==cc0:
                            pr=np.float32(norm[Y+dy,X+dx]*wt)
                            sm=np.float32(sm+pr); dn=np.float32(dn+wt)
                if cc0==own:
                    direct=np.float32(norm[Y,X]); recip=direct
                else:
                    direct=np.float32(sm/dn)
                    rr=np.float32(np.float32(1.0)/dn); recip=np.float32(sm*rr)
                cand_direct.append(float(direct));cand_recip.append(float(recip));cand_sum.append(float(sm));cand_den.append(float(dn))
    print('VNG4_CPU_LINEAR_CANDIDATES '+json.dumps({'direct':cand_direct,'recipmul':cand_recip,'sum':cand_sum,'den':cand_den},separators=(',',':')))

if __name__=='__main__': main()
