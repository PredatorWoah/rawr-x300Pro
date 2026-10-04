import os, sys
import numpy as np
from PIL import Image, ImageDraw
sys.path.insert(0,os.path.dirname(__file__))
from tonemap_ref import *

OUT=os.path.join(os.path.dirname(__file__),'..','validation_out'); os.makedirs(OUT,exist_ok=True)
P=ParamsBaseline()
W,H=768,128

def save(name,arr):
    arr=np.nan_to_num(arr,nan=0,posinf=1,neginf=0)
    im=Image.fromarray(np.uint8(np.clip(arr,0,1)*255+.5),'RGB')
    im.save(os.path.join(OUT,name))

def ramp_color(c, max_scale=16.0):
    x=np.linspace(-4,np.log2(max_scale),W)
    s=2**x
    rgb=s[:,None]*np.asarray(c)[None,:]
    img=np.repeat(rgb[None,:,:],H,axis=0)
    return img

scenes={
'neutral_hdr.png':ramp_color([1,1,1]),
'warm_highlight.png':ramp_color([1.0,.45,.12]),
'red_orange.png':ramp_color([1.0,.16,.02]),
'cyan.png':ramp_color([.04,.85,1.0]),
'blue.png':ramp_color([.03,.08,1.0]),
'magenta.png':ramp_color([1.0,.03,.75]),
'skin_like.png':ramp_color([.75,.40,.26]),
}
for name,scene in scenes.items(): save(name,render(scene,P))

# Shadow gradient
x=np.linspace(-14,1,W); s=.18*(2**x); rgb=np.stack([s,s*.92,s*.83],axis=-1)
scene=np.repeat(rgb[None,:,:],H,axis=0); save('shadow_gradient.png',render(scene,ParamsBaseline(shadowLiftEV=2.0)))

# Hard saturated boundaries under high exposure
colors=np.array([[1,.02,.02],[1,.4,.02],[.02,1,.02],[.02,1,1],[.02,.08,1],[1,.02,1]],float)*5
band=np.zeros((H,W,3)); bw=W//len(colors)
for i,c in enumerate(colors): band[:,i*bw:(i+1)*bw]=c
save('hard_saturated_boundaries.png',render(band,P))

# Extreme exposure triptych
base=ramp_color([.75,.40,.26],4)
out=np.concatenate([render(base,ParamsBaseline(exposureEV=-5)),render(base,P),render(base,ParamsBaseline(exposureEV=5))],axis=0)
save('extreme_exposure.png',out)

# Highlight trajectory grid with labeled tiles
cols=[('warm',[1,.45,.12]),('red',[1,.08,.02]),('cyan',[.03,.8,1]),('blue',[.02,.05,1]),('magenta',[1,.02,.7]),('skin',[.75,.4,.26])]
scales=[.25,.5,1,2,4,8,16]
tile=72; im=Image.new('RGB',(tile*len(scales),tile*len(cols)),(0,0,0)); draw=ImageDraw.Draw(im)
for r,(n,c) in enumerate(cols):
 for j,s in enumerate(scales):
  v=render(np.array(c,float)[None,:]*s,P)[0]
  q=tuple(np.uint8(np.clip(v,0,1)*255+.5))
  draw.rectangle([j*tile,r*tile,(j+1)*tile-1,(r+1)*tile-1],fill=q)
  draw.text((j*tile+3,r*tile+3),f'{s:g}x',fill=(255,255,255) if np.mean(q)<120 else (0,0,0))
 draw.text((3,r*tile+tile-14),n,fill=(255,255,255))
im.save(os.path.join(OUT,'highlight_trajectories.png'))
print('VISUAL_EXPORTS_PASS',len(os.listdir(OUT)),'files')
