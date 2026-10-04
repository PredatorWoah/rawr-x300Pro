import os, sys, math, json
from pathlib import Path
import numpy as np
from PIL import Image, ImageDraw, ImageFont

ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'python'))
from tonemap_ref import Params, ParamsBaseline, render, compile_curve, eval_curve, gamut_compress, M_AP1_TO_SRGB, AP1_LUMA, SRGB_LUMA, MIDDLE_GRAY

OUT=ROOT/'quality_out'; OUT.mkdir(exist_ok=True)

# ---------- offline perceptual metrics (sRGB D65 -> OKLab) ----------
def lin_srgb_to_oklab(rgb):
    rgb=np.asarray(rgb,dtype=np.float64)
    M1=np.array([[0.4122214708,0.5363325363,0.0514459929],
                 [0.2119034982,0.6806995451,0.1073969566],
                 [0.0883024619,0.2817188376,0.6299787005]])
    M2=np.array([[ 0.2104542553, 0.7936177850,-0.0040720468],
                 [ 1.9779984951,-2.4285922050, 0.4505937099],
                 [ 0.0259040371, 0.7827717662,-0.8086757660]])
    lms=rgb@M1.T
    lms_=np.cbrt(lms)
    return lms_@M2.T

def hue_deg(lab):
    return (np.degrees(np.arctan2(lab[...,2],lab[...,1]))+360)%360

def angle_diff(a,b):
    return (a-b+180)%360-180

# ---------- image helpers ----------
def save01(name, arr):
    a=np.clip(arr,0,1)
    Image.fromarray(np.rint(a*255).astype(np.uint8),'RGB').save(OUT/name)

def label_sheet(images, labels, cols, cell_w=None, cell_h=None, name='sheet.png'):
    imgs=[Image.fromarray(np.rint(np.clip(im,0,1)*255).astype(np.uint8),'RGB') for im in images]
    if cell_w is None: cell_w=max(i.width for i in imgs)
    if cell_h is None: cell_h=max(i.height for i in imgs)
    rows=(len(imgs)+cols-1)//cols
    top=22
    canvas=Image.new('RGB',(cols*cell_w,rows*(cell_h+top)),(245,245,245))
    d=ImageDraw.Draw(canvas)
    for n,(im,lab) in enumerate(zip(imgs,labels)):
        x=(n%cols)*cell_w; y=(n//cols)*(cell_h+top)
        canvas.paste(im.resize((cell_w,cell_h)),(x,y+top))
        d.text((x+4,y+4),lab,fill=(0,0,0))
    canvas.save(OUT/name)

# ---------- scene constructors ----------
def exposure_ramp(base, w=768, h=96, lo=-8, hi=8):
    ev=np.linspace(lo,hi,w)
    return np.tile(base[None,None,:]*(2.0**ev)[None,:,None],(h,1,1))

def modulated_ramp(base=np.array([1.,1.,1.]), w=1024,h=160,lo=-10,hi=4,mod=0.08,cycles=96):
    ev=np.linspace(lo,hi,w)
    carrier=1+mod*np.sin(np.linspace(0,cycles*2*np.pi,w))
    v=(MIDDLE_GRAY*2**ev)*carrier
    return np.tile(v[None,:,None]*base[None,None,:],(h,1,1))

# ---------- Tone control characterization ----------
def tone_samples(p, xs=np.linspace(-12,9,4097)):
    k,ys,m,c=compile_curve(p); return xs,eval_curve(xs,k,c)

def crossing_x(xs, ys, ytarget):
    idx=np.searchsorted(ys,ytarget)
    if idx<=0:return float(xs[0])
    if idx>=len(xs):return float(xs[-1])
    x0,x1=xs[idx-1],xs[idx]; y0,y1=ys[idx-1],ys[idx]
    if y1==y0:return float(x0)
    return float(x0+(ytarget-y0)*(x1-x0)/(y1-y0))

def derivative(xs,ys): return np.gradient(ys,xs)

# ---------- Gamut/hue characterization ----------
def render_linear_out(rgb,p):
    _,s=render(rgb,p,return_stages=True)
    return s['outlin'],s['gamut']

def hue_trajectory_metrics():
    colors={
        'red':np.array([1.0,0.03,0.02]),
        'orange':np.array([1.0,0.22,0.03]),
        'yellow':np.array([1.0,0.75,0.03]),
        'green':np.array([0.03,1.0,0.05]),
        'cyan':np.array([0.02,0.9,1.0]),
        'blue':np.array([0.03,0.12,1.0]),
        'magenta':np.array([1.0,0.03,0.75]),
        'skin':np.array([0.72,0.38,0.25]),
    }
    evs=np.linspace(-3,8,89)
    p=ParamsBaseline()
    report={}
    for name,c in colors.items():
        samples=c[None,:]*(2**evs)[:,None]
        outlin,gamut=render_linear_out(samples,p)
        lab0=lin_srgb_to_oklab(outlin)
        labg=lin_srgb_to_oklab(gamut)
        h0=hue_deg(lab0); hg=hue_deg(labg)
        chroma=np.sqrt(labg[:,1]**2+labg[:,2]**2)
        # compare only where perceptual chroma is non-negligible
        valid=chroma>0.015
        delta=angle_diff(hg,h0)
        report[name]={
            'max_hue_change_due_to_gamut_deg':float(np.max(np.abs(delta[valid]))) if valid.any() else 0,
            'p95_hue_change_due_to_gamut_deg':float(np.percentile(np.abs(delta[valid]),95)) if valid.any() else 0,
            'max_chroma':float(np.max(chroma)),
            'final_chroma':float(chroma[-1]),
        }
    return report

# ---------- generate visuals ----------
def generate_control_sheets():
    scene=np.concatenate([
        exposure_ramp(np.array([1.,1.,1.]),384,72,-10,6),
        exposure_ramp(np.array([1.,0.25,0.08]),384,72,-7,7),
        exposure_ramp(np.array([0.06,0.8,1.0]),384,72,-7,7),
    ],axis=0)
    specs=[
      ('Exposure',[('−2 EV',dict(exposureEV=-2)),('−1 EV',dict(exposureEV=-1)),('0 EV',dict()),('+1 EV',dict(exposureEV=1)),('+2 EV',dict(exposureEV=2))]),
      ('Shadows',[('−2',dict(shadowLiftEV=-2)),('−1',dict(shadowLiftEV=-1)),('0',dict()),('+0.5',dict(shadowLiftEV=.5)),('+1',dict(shadowLiftEV=1))]),
      ('Black',[('−12',dict(blackPointEV=-12)),('−11',dict(blackPointEV=-11)),('−10',dict()),('−9',dict(blackPointEV=-9)),('−8',dict(blackPointEV=-8))]),
      ('Contrast',[('0.65',dict(contrast=.65)),('0.8',dict(contrast=.8)),('1.0',dict()),('1.2',dict(contrast=1.2)),('1.5',dict(contrast=1.5))]),
      ('Shoulder',[('+1',dict(shoulderStartEV=1)),('+1.5',dict(shoulderStartEV=1.5)),('+2',dict()),('+2.5',dict(shoulderStartEV=2.5)),('+3',dict(shoulderStartEV=3)),('+3.5',dict(shoulderStartEV=3.5))]),
      ('White',[('+4',dict(whitePointEV=4)),('+5',dict(whitePointEV=5)),('+6',dict()),('+7',dict(whitePointEV=7)),('+8',dict(whitePointEV=8))]),
      ('Highlights',[('−1',dict(highlightBiasEV=-1)),('−0.75',dict(highlightBiasEV=-.75)),('−0.5',dict(highlightBiasEV=-.5)),('0',dict()),('+0.25',dict(highlightBiasEV=.25))]),
      ('Saturation',[('0',dict(saturation=0)),('0.5',dict(saturation=.5)),('1',dict()),('1.25',dict(saturation=1.25)),('1.5',dict(saturation=1.5)),('2',dict(saturation=2))]),
      ('Vibrance',[('-1',dict(vibrance=-1)),('-0.5',dict(vibrance=-.5)),('0',dict()),('+0.5',dict(vibrance=.5)),('+1',dict(vibrance=1))]),
    ]
    for title,vals in specs:
        ims=[]; labs=[]
        for lab,kw in vals:
            try: ims.append(render(scene,ParamsBaseline(**kw))); labs.append(lab)
            except ValueError as e: pass
        label_sheet(ims,labs,cols=len(ims),cell_w=256,cell_h=144,name=f'control_{title.lower()}.png')


def generate_texture_sheets():
    base=modulated_ramp()
    variants=[('default',ParamsBaseline()),('shadow +3',ParamsBaseline(shadowLiftEV=3)),('contrast .65',ParamsBaseline(contrast=.65)),('contrast 1.5',ParamsBaseline(contrast=1.5)),('black -8',ParamsBaseline(blackPointEV=-8)),('black -12',ParamsBaseline(blackPointEV=-12))]
    ims=[render(base,p) for _,p in variants]
    label_sheet(ims,[a for a,_ in variants],cols=3,cell_w=512,cell_h=100,name='microcontrast_shadow_tone.png')


def generate_gamut_stress():
    # hue wheel-ish strips at escalating scene scale, encoded as AP1-like inputs
    hues=np.linspace(0,2*np.pi,720,endpoint=False)
    # simple positive RGB hues using phase shifted cos, normalized
    rgb=np.stack([np.maximum(0,np.cos(hues)),np.maximum(0,np.cos(hues-2*np.pi/3)),np.maximum(0,np.cos(hues+2*np.pi/3))],axis=-1)
    rgb/=np.maximum(rgb.max(axis=1,keepdims=True),1e-8)
    strips=[]; labels=[]
    for ev in [-2,0,2,4,6,8]:
        scene=np.tile((rgb*(2**ev))[None,:,:],(64,1,1))
        strips.append(render(scene,ParamsBaseline())); labels.append(f'{ev:+} EV scale')
    label_sheet(strips,labels,cols=1,cell_w=720,cell_h=64,name='gamut_hue_sweep.png')


def generate_tone_curve_plot():
    # no matplotlib dependency: rasterize curves into RGB canvas
    W,H=1200,700; margin=70
    canvas=np.ones((H,W,3),dtype=np.float64)*0.97
    # grid
    for ev in range(-12,9,2):
        x=int(margin+(ev+12)/20*(W-2*margin)); canvas[margin:H-margin,max(0,x-1):x+1]=.86
    for yy in np.linspace(0,1,6):
        y=int(H-margin-yy*(H-2*margin)); canvas[max(0,y-1):y+1,margin:W-margin]=.86
    sets=[('default',ParamsBaseline()),('shadows +3',ParamsBaseline(shadowLiftEV=3)),('contrast .65',ParamsBaseline(contrast=.65)),('contrast 1.5',ParamsBaseline(contrast=1.5)),('highlights -1.5',ParamsBaseline(highlightBiasEV=-1.5)),('white +8',ParamsBaseline(whitePointEV=8))]
    cols=np.array([[0.05,.05,.05],[.75,.15,.15],[.1,.5,.15],[.15,.25,.75],[.7,.15,.65],[.85,.5,.05]])
    xs=np.linspace(-12,8,3000)
    for (_,p),col in zip(sets,cols):
        k,y,m,c=compile_curve(p); ys=eval_curve(xs,k,c)
        px=(margin+(xs+12)/20*(W-2*margin)).astype(int)
        py=(H-margin-ys*(H-2*margin)).astype(int)
        good=(px>=0)&(px<W)&(py>=0)&(py<H)
        canvas[py[good],px[good]]=col
    im=Image.fromarray(np.rint(canvas*255).astype(np.uint8),'RGB')
    d=ImageDraw.Draw(im)
    for i,(name,_) in enumerate(sets): d.text((85,20+i*18),name,fill=tuple((cols[i]*255).astype(int)))
    d.text((W//2-80,H-28),'scene stops relative to 18% gray',fill=(0,0,0))
    d.text((8,8),'display-linear Y',fill=(0,0,0))
    im.save(OUT/'tone_curve_family.png')


def control_metrics():
    xs=np.linspace(-12,9,12001)
    base=ParamsBaseline(); bx,by=tone_samples(base,xs)
    cases={
      'shadow_-2':ParamsBaseline(shadowLiftEV=-2),'shadow_+1':ParamsBaseline(shadowLiftEV=1),'shadow_+2':ParamsBaseline(shadowLiftEV=2),'shadow_+3':ParamsBaseline(shadowLiftEV=3),
      'contrast_0.65':ParamsBaseline(contrast=.65),'contrast_1.5':ParamsBaseline(contrast=1.5),
      'black_-12':ParamsBaseline(blackPointEV=-12),'black_-8':ParamsBaseline(blackPointEV=-8),
      'shoulder_1':ParamsBaseline(shoulderStartEV=1),'shoulder_3':ParamsBaseline(shoulderStartEV=3),
      'white_4':ParamsBaseline(whitePointEV=4),'white_8':ParamsBaseline(whitePointEV=8),
      'highlight_-1.5':ParamsBaseline(highlightBiasEV=-1.5),'highlight_+0.5':ParamsBaseline(highlightBiasEV=.5),
    }
    report={}
    for name,p in cases.items():
        _,y=tone_samples(p,xs)
        diff=y-by
        # influence energy by regions
        masks={'deep':xs<-6,'shadow':(xs>=-6)&(xs<-2),'midtone':(xs>=-2)&(xs<1),'highlight':xs>=1}
        report[name]={
          'max_abs_delta':float(np.max(np.abs(diff))),
          'region_mean_abs_delta':{k:float(np.mean(np.abs(diff[m]))) for k,m in masks.items()},
          'scene_ev_at_Y0.18':crossing_x(xs,y,.18),
          'scene_ev_at_Y0.50':crossing_x(xs,y,.50),
          'scene_ev_at_Y0.90':crossing_x(xs,y,.90),
        }
    return report


def vibrance_metrics():
    colors={
      'weak_warm':np.array([.50,.45,.42]),
      'skin_like':np.array([.60,.36,.25]),
      'orange_sat':np.array([.80,.30,.05]),
      'cyan_sat':np.array([.05,.65,.75]),
      'blue_sat':np.array([.08,.18,.85]),
      'red_sat':np.array([.85,.06,.04]),
      'magenta_sat':np.array([.55,.20,.55]),
      'deep_shadow_weak':np.array([.012,.010,.009]),
    }
    report={}
    for name,c in colors.items():
        base,bs=render(c[None,:],ParamsBaseline(vibrance=0),return_stages=True)
        lab0=lin_srgb_to_oklab(bs['outlin'])[0]
        base_ap1=bs['color'][0]
        base_y=float(base_ap1@AP1_LUMA)
        basecap1=float(np.linalg.norm(base_ap1-base_y))
        rows={}
        for v in [-1,-.5,0,.5,1]:
            out,st=render(c[None,:],ParamsBaseline(vibrance=v),return_stages=True)
            lab=lin_srgb_to_oklab(st['outlin'])[0]
            c0=float(np.hypot(lab0[1],lab0[2])); c1=float(np.hypot(lab[1],lab[2]))
            hd=float(abs(angle_diff(hue_deg(lab),hue_deg(lab0)))) if c0>.01 and c1>.01 else 0.0
            ap1=st['color'][0]; ay=float(ap1@AP1_LUMA); cap1=float(np.linalg.norm(ap1-ay))
            rows[str(v)]={'ap1_chroma_gain':cap1/max(basecap1,1e-12),
                          'oklab_chroma_ratio':c1/max(c0,1e-12),
                          'oklab_hue_delta_deg':hd}
        report[name]=rows
    return report


def generate_vibrance_palette():
    colors=np.array([[.50,.45,.42],[.60,.36,.25],[.80,.30,.05],[.85,.06,.04],
                     [.03,.75,.08],[.05,.65,.75],[.08,.18,.85],[.55,.20,.55]],float)
    # tiles across exposure to expose highlight/gamut interactions
    ev=np.array([-2,-1,0,1,2],float)
    scene=(colors[:,None,:]*(2.0**ev)[None,:,None]).reshape(-1,3)
    scene=np.tile(scene[None,:,:],(80,1,1))
    ims=[]; labels=[]
    for v in [-1,-.5,0,.5,1]:
        rendered=render(scene,ParamsBaseline(vibrance=v))
        rendered=np.repeat(rendered,24,axis=1)
        ims.append(rendered); labels.append(f'vibrance {v:+g}')
    label_sheet(ims,labels,cols=1,cell_w=960,cell_h=80,name='vibrance_palette_exposure.png')

def main():
    generate_control_sheets(); generate_texture_sheets(); generate_gamut_stress(); generate_tone_curve_plot(); generate_vibrance_palette()
    report={'control_metrics':control_metrics(),'hue_trajectory_metrics':hue_trajectory_metrics(),'vibrance_metrics':vibrance_metrics()}
    (OUT/'quality_metrics.json').write_text(json.dumps(report,indent=2))
    print(json.dumps(report,indent=2))
    print('V1_3_QUALITY_LAB_PASS')

if __name__=='__main__': main()
