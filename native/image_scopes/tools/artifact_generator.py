#!/usr/bin/env python3
# PRODUCTION PRESENTATION NOTE:
# This dependency-free generator is a numerical/geometry review utility.
# The frozen photographic presentations are the Vulkan V22 vectorscope and
# Waveform E renderers. Their fixed real-photo Vulkan artifacts are the visual
# acceptance oracles. Do not use CPU scope previews here as pixel/appearance
# release oracles.
#
# Deterministic PNG review generator. Validation tooling only; production has no filesystem/PNG dependency.
import math, pathlib, struct, zlib
ROOT=pathlib.Path(__file__).resolve().parents[3]/'tmp'/'image_scopes'
SYN=ROOT/'scope_review'/'synthetic'; PHOTO=ROOT/'scope_review'/'photographic'; CMP=ROOT/'scope_review'/'comparisons'
for d in (SYN,PHOTO,CMP): d.mkdir(parents=True,exist_ok=True)

def png(path,w,h,rgb):
    raw=b''.join(b'\x00'+bytes(rgb[y*w*3:(y+1)*w*3]) for y in range(h))
    def ch(t,d):return struct.pack('>I',len(d))+t+d+struct.pack('>I',zlib.crc32(t+d)&0xffffffff)
    path.write_bytes(b'\x89PNG\r\n\x1a\n'+ch(b'IHDR',struct.pack('>IIBBBBB',w,h,8,2,0,0,0))+ch(b'IDAT',zlib.compress(raw,9))+ch(b'IEND',b''))
def clamp(v): return max(0,min(255,int(v)))
def luma(r,g,b):return (13933*r+46871*g+4732*b+32768)>>16
def cbcr(r,g,b):
    def rq(q):return (q+32768)//65536 if q>=0 else -((-q+32768)//65536)
    return max(0,min(255,128+rq(-7509*r-25259*g+32768*b))),max(0,min(255,128+rq(32768*r-29763*g-3005*b)))

def synthetic(w=640,h=480):
    a=[]
    for y in range(h):
      for x in range(w):
        band=(x*6)//w;r=g=b=int(255*x/(w-1))
        if band==1:r,g,b=255,32,32
        elif band==2:r,g,b=32,255,32
        elif band==3:r,g,b=32,32,255
        elif band==4:r,g,b=255,255,32
        elif band==5:r,g,b=255,32,255
        a.append((r,g,b))
    return a

def photographic(w=960,h=540):
    # Deterministic daylight/HDR-like scene: cyan-blue sky, bright sun, warm horizon,
    # dark mountains, green foreground, saturated objects and a warm skin-like patch.
    a=[]
    sunx,suny=int(w*.77),int(h*.18); sr=int(h*.065)
    for y in range(h):
      fy=y/(h-1); horizon=.53
      for x in range(w):
        fx=x/(w-1)
        if fy<horizon:
          t=fy/horizon
          r=45+105*t; g=115+95*t; b=205+30*t
          # warm horizon band
          warm=max(0.0,1.0-abs(fy-horizon)/.12)
          r+=55*warm; g+=20*warm; b-=35*warm
        else:
          t=(fy-horizon)/(1-horizon)
          r=40-22*t;g=82-42*t;b=38-25*t
        # layered dark mountain silhouettes
        ridge1=.43+.08*math.sin(fx*9.0)+.025*math.sin(fx*31.0)
        ridge2=.50+.045*math.sin(fx*13.0+1.2)
        if fy>ridge1: r,g,b=34,43,50
        if fy>ridge2: r,g,b=22+10*(1-fy),48+26*(1-fy),28
        # sun / bloom (intentional encoded clipping at core)
        dx=x-sunx;dy=y-suny;dist=math.sqrt(dx*dx+dy*dy)
        if dist<sr*2.4:
          k=max(0.0,1.0-dist/(sr*2.4));r+=145*k;g+=120*k;b+=70*k
        if dist<sr: r,g,b=255,245,205
        # red/orange object at lower-left
        if .13<fx<.25 and .67<fy<.88:
          edge=min((fx-.13)/.02,(.25-fx)/.02,(fy-.67)/.03,(.88-fy)/.03,1.0)
          if edge>0:r,g,b=clamp(225+20*edge),clamp(55+20*edge),30
        # blue/cyan object
        if .66<fx<.76 and .65<fy<.84:r,g,b=25,150,220
        # skin-like elliptical patch
        ex=(fx-.48)/.065;ey=(fy-.70)/.12
        if ex*ex+ey*ey<1.0:
          shade=max(.55,1-.28*(ex+.4*ey));r,g,b=clamp(205*shade),clamp(142*shade),clamp(105*shade)
        # small specular saturated patch
        if .52<fx<.535 and .645<fy<.665:r,g,b=255,255,248
        # deterministic fine texture in foreground
        if fy>.55:
          n=((x*37+y*17+(x*y)%29)%19)-9;r+=n*.7;g+=n;b+=n*.5
        a.append((clamp(r),clamp(g),clamp(b)))
    return a

def source_png(src,w,h):
    out=[]
    for r,g,b in src:out.extend((r,g,b))
    return w,h,out

def _density_map(c,m,mode='linear',gain=1.0):
    if not c or not m:return 0.0
    x=c/m
    if mode=='log':x=math.log1p(16*c)/math.log1p(16*m)
    elif mode=='sqrt':x=math.sqrt(max(0.0,min(1.0,x)))
    return max(0.0,min(1.0,gain*x))

def waveform_rgb(src,sw,sh):
    W,H=512,256;P=W*H;d=[[0]*P for _ in range(3)]
    for y in range(sh):
      for x in range(sw):
        p=src[y*sw+x];xb=min(W-1,x*W//sw)
        for c in range(3):d[c][p[c]*W+xb]+=1
    # Analysis audit: full-reference waveform has exactly one sample/channel/source pixel.
    assert all(sum(q)==sw*sh for q in d)
    mx=[max(q) or 1 for q in d];ow,oh=640,360;out=[4]*(ow*oh*3);radius=1
    for y in range(oh):
      sy=min(255,int((1-(y+.5)/oh)*256))
      for x in range(ow):
        sx=min(511,int((x+.5)*512/ow));i=(y*ow+x)*3
        if x%(ow//4)==0 or y%(oh//4)==0:out[i:i+3]=[20,20,20]
        for c in range(3):
          sm=ws=0.0
          for oy in range(-radius,radius+1):
            yy=sy+oy
            if not 0<=yy<256:continue
            for ox in range(-radius,radius+1):
              xx=sx+ox
              if not 0<=xx<512:continue
              kw=math.exp(-.70*(ox*ox+oy*oy));sm+=kw*_density_map(d[c][yy*W+xx],mx[c],'linear',1.0);ws+=kw
          q=int(255*(sm/ws if ws else 0))
          out[i+c]=max(out[i+c],q)
    return ow,oh,out

def waveform_luma(src,sw,sh):
    W,H=512,256;P=W*H;d=[0]*P
    for y in range(sh):
      for x in range(sw):
        p=src[y*sw+x];xb=min(W-1,x*W//sw);d[luma(*p)*W+xb]+=1
    assert sum(d)==sw*sh
    m=max(d) or 1;ow,oh=640,360;out=[4]*(ow*oh*3);radius=1
    for y in range(oh):
      sy=min(255,int((1-(y+.5)/oh)*256))
      for x in range(ow):
        sx=min(511,int((x+.5)*512/ow));i=(y*ow+x)*3
        if x%(ow//4)==0 or y%(oh//4)==0:out[i:i+3]=[20,20,20]
        sm=ws=0.0
        for oy in range(-radius,radius+1):
          yy=sy+oy
          if not 0<=yy<256:continue
          for ox in range(-radius,radius+1):
            xx=sx+ox
            if not 0<=xx<512:continue
            kw=math.exp(-.70*(ox*ox+oy*oy));sm+=kw*_density_map(d[yy*W+xx],m,'linear',1.0);ws+=kw
        q=int(255*(sm/ws if ws else 0));out[i:i+3]=[max(out[i],q),max(out[i+1],q),max(out[i+2],q)]
    return ow,oh,out

def vectorscope(src,bins=256):
    d=[0]*(bins*bins)
    for p in src:
      cb,cr=cbcr(*p);x=min(bins-1,cb*bins//256);y=min(bins-1,cr*bins//256);d[y*bins+x]+=1
    assert sum(d)==len(src)
    m=max(d) or 1;ow=oh=512;out=[4]*(ow*oh*3);center=bins//2;radius=2
    targets=[(99,255),(30,12),(255,116),(157,0),(226,244),(0,140)]
    targets=[(min(bins-1,x*bins//256),min(bins-1,y*bins//256)) for x,y in targets]
    for y in range(oh):
      sy=min(bins-1,int((1-(y+.5)/oh)*bins))
      for x in range(ow):
        sx=min(bins-1,int((x+.5)*bins/ow));i=(y*ow+x)*3
        if sx==center or sy==center:out[i:i+3]=[24,24,24]
        if any(max(abs(sx-tx),abs(sy-ty))<=1 for tx,ty in targets):out[i:i+3]=[46,46,46]
        counts=ws=0.0
        for oy in range(-radius,radius+1):
          for ox in range(-radius,radius+1):
            xx,yy=sx+ox,sy+oy
            if 0<=xx<bins and 0<=yy<bins:
              kw=math.exp(-.55*(ox*ox+oy*oy));counts+=kw*d[yy*bins+xx];ws+=kw
        filtered=counts/ws if ws else 0.0
        val=(math.log1p(32*filtered)/math.log1p(32*m)) if filtered else 0.0
        q=int(255*min(1,val))
        if q:out[i:i+3]=[max(out[i],q),max(out[i+1],q),max(out[i+2],q)]
    return ow,oh,out

def color_target(w=720,h=480):
    # Six primary/secondary patches plus neutral scale. No gradients inside patches,
    # so vectorscope positions must form unmistakable discrete clusters.
    cols=[(191,0,0),(0,191,0),(0,0,191),(0,191,191),(191,0,191),(191,191,0)]
    a=[]
    for y in range(h):
      for x in range(w):
        if y < int(h*.68):
          k=min(5,x*6//w); p=cols[k]
        else:
          k=min(7,x*8//w); v=[0,32,64,96,128,160,208,255][k]; p=(v,v,v)
        a.append(p)
    return a

def photographic2(w=960,h=540):
    # Second deterministic photographic-style fixture: low-contrast portrait/object scene
    # with broad tonal distributions, mixed warm/cool light, texture and no large flat bars.
    a=[]
    for y in range(h):
      fy=y/(h-1)
      for x in range(w):
        fx=x/(w-1)
        # blurred indoor background approximation
        r=62+48*(1-fy)+18*math.sin(fx*4.5)
        g=68+38*(1-fy)+12*math.sin(fx*5.7+1.1)
        b=78+52*(1-fy)+24*(1-fx)
        # warm window light from left
        glow=max(0.0,1.0-math.sqrt(((fx-.12)/.28)**2+((fy-.25)/.42)**2))
        r+=105*glow;g+=78*glow;b+=35*glow
        # face-like ellipse, deliberately varied illumination rather than a flat patch
        ex=(fx-.52)/.14; ey=(fy-.45)/.27
        if ex*ex+ey*ey<1:
          shade=.68+.27*(1-fx)+.08*math.cos(ey*2.4)
          r,g,b=218*shade,157*shade,122*shade
          # cheek warmth and shadow side
          r+=16*max(0,1-((ex+.35)/.55)**2-(ey/.5)**2)
          if ex>.25: r*=.86;g*=.84;b*=.90
        # dark hair arc/top
        if ((fx-.52)/.17)**2+((fy-.31)/.20)**2<1 and fy<.34: r,g,b=28,24,23
        # blue clothing lower center
        if fy>.70 and abs(fx-.52)<(.26-.12*(fy-.70)):
          shade=.72+.18*math.sin(x*.07+y*.03);r,g,b=33*shade,72*shade,112*shade
        # red/orange small object at right
        if ((fx-.82)/.065)**2+((fy-.67)/.10)**2<1: r,g,b=205,71,38
        # fine deterministic texture / sensor-like encoded variation
        n=((x*19+y*23+(x*y)%41)%17)-8
        r+=n*.65;g+=n*.55;b+=n*.7
        a.append((clamp(r),clamp(g),clamp(b)))
    return a

# Synthetic oracle-style examples
s=synthetic(); sw,sh=640,480
for name,fn in [('reference_display_rgb_waveform.png',lambda q:waveform_rgb(q,sw,sh)),('reference_display_luma_waveform.png',lambda q:waveform_luma(q,sw,sh)),('reference_vectorscope_256.png',lambda q:vectorscope(q,256)),('reference_vectorscope_128.png',lambda q:vectorscope(q,128))]:
    w,h,p=fn(s);png(SYN/name,w,h,p)
# Photographic review set
pw,ph=960,540;p=photographic(pw,ph)
for name,fn in [('daylight_hdr_source.png',lambda q:source_png(q,pw,ph)),('daylight_hdr_rgb_waveform.png',lambda q:waveform_rgb(q,pw,ph)),('daylight_hdr_luma_waveform.png',lambda q:waveform_luma(q,pw,ph)),('daylight_hdr_vectorscope_256.png',lambda q:vectorscope(q,256)),('daylight_hdr_vectorscope_128.png',lambda q:vectorscope(q,128))]:
    w,h,pix=fn(p);png(PHOTO/name,w,h,pix)
# Six primary/secondary + neutral target for vectorscope semantic review
cw,ch=720,480;c=color_target(cw,ch)
for name,fn in [('color_target_source.png',lambda q:source_png(q,cw,ch)),('color_target_rgb_waveform.png',lambda q:waveform_rgb(q,cw,ch)),('color_target_luma_waveform.png',lambda q:waveform_luma(q,cw,ch)),('color_target_vectorscope_256.png',lambda q:vectorscope(q,256)),('color_target_vectorscope_128.png',lambda q:vectorscope(q,128))]:
    w,h,pix=fn(c);png(SYN/name,w,h,pix)
# A second, smoother photographic-style fixture for vectorscope readability
pw2,ph2=960,540;p2=photographic2(pw2,ph2)
for name,fn in [('portrait_scene_source.png',lambda q:source_png(q,pw2,ph2)),('portrait_scene_rgb_waveform.png',lambda q:waveform_rgb(q,pw2,ph2)),('portrait_scene_luma_waveform.png',lambda q:waveform_luma(q,pw2,ph2)),('portrait_scene_vectorscope_256.png',lambda q:vectorscope(q,256)),('portrait_scene_vectorscope_128.png',lambda q:vectorscope(q,128))]:
    w,h,pix=fn(p2);png(PHOTO/name,w,h,pix)
print(ROOT/'scope_review')
