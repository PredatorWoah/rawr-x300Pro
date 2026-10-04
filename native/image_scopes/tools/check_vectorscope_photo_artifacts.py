#!/usr/bin/env python3
from pathlib import Path
import sys

if len(sys.argv)!=2:
    raise SystemExit("usage: check_vectorscope_photo_artifacts.py ARTIFACT_DIR")
root=Path(sys.argv[1])

def token(f):
    while True:
        s=f.readline()
        if not s: raise ValueError("unexpected EOF")
        s=s.strip()
        if s and not s.startswith(b"#"): return s

def ppm(path):
    with path.open("rb") as f:
        if token(f)!=b"P6": raise ValueError(f"{path}: not P6")
        wh=token(f).split()
        while len(wh)<2: wh+=token(f).split()
        w,h=map(int,wh[:2])
        if int(token(f))!=255: raise ValueError(f"{path}: maxval")
        data=f.read(w*h*3)
        if len(data)!=w*h*3: raise ValueError(f"{path}: short read")
        return w,h,data

def check(name):
    p=root/f"{name}_vectorscope_data.ppm"
    w,h,d=ppm(p)
    active=0; chromatic=0
    minx=w;miny=h;maxx=-1;maxy=-1
    for i in range(w*h):
        r,g,b=d[i*3:i*3+3]
        mx=max(r,g,b); mn=min(r,g,b)
        if mx>=6:
            active+=1
            x=i%w;y=i//w
            minx=min(minx,x);maxx=max(maxx,x);miny=min(miny,y);maxy=max(maxy,y)
            if mx-mn>=5: chromatic+=1
    frac=active/(w*h)
    if active<200:
        raise ValueError(f"{name}: presentation nearly empty ({active} active pixels)")
    if frac>0.60:
        raise ValueError(f"{name}: presentation exploded ({frac:.1%} active pixels)")
    if maxx-minx<12 or maxy-miny<12:
        raise ValueError(f"{name}: presentation collapsed bbox {maxx-minx+1}x{maxy-miny+1}")
    if chromatic<100:
        raise ValueError(f"{name}: insufficient chromatic signal ({chromatic} pixels)")
    print(f"{name}: active={active} ({frac:.2%}) chromatic={chromatic} bbox={maxx-minx+1}x{maxy-miny+1}")

try:
    check("portrait_a")
    check("portrait_b")
except Exception as e:
    print("VECTORSCOPE_PHOTO_ARTIFACT_FAIL:",e)
    raise SystemExit(1)

print("VECTORSCOPE_PHOTO_ARTIFACT_PASS")
