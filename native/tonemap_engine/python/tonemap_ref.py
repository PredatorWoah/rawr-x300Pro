import math
import numpy as np

AP1_LUMA = np.array([0.2722287168, 0.6740817658, 0.0536895174], dtype=np.float64)
SRGB_LUMA = np.array([0.2126, 0.7152, 0.0722], dtype=np.float64)
M_AP1_TO_SRGB = np.array([
    [ 1.70505154, -0.62179068, -0.08325840],
    [-0.13025714,  1.14080269, -0.01054853],
    [-0.02400328, -0.12896877,  1.15297184],
], dtype=np.float64)

class Config:
    def __init__(self, middleGray, shadowAnchorEV, shoulderOutputCap,
                 gamutCompressionStart, vibranceStrength,
                 vibranceShadowStart, vibranceShadowEnd):
        self.middleGray=float(middleGray)
        self.shadowAnchorEV=float(shadowAnchorEV)
        self.shoulderOutputCap=float(shoulderOutputCap)
        self.gamutCompressionStart=float(gamutCompressionStart)
        self.vibranceStrength=float(vibranceStrength)
        self.vibranceShadowStart=float(vibranceShadowStart)
        self.vibranceShadowEnd=float(vibranceShadowEnd)

class Params:
    def __init__(self, exposureEV, blackPointEV, shadowLiftEV, midtoneLiftEV, contrast,
                 shoulderStartEV, whitePointEV, highlightBiasEV,
                 saturation, vibrance, aePostGain=1.0):
        self.exposureEV=float(exposureEV)
        self.blackPointEV=float(blackPointEV)
        self.shadowLiftEV=float(shadowLiftEV)
        self.midtoneLiftEV=float(midtoneLiftEV)
        self.contrast=float(contrast)
        self.shoulderStartEV=float(shoulderStartEV)
        self.whitePointEV=float(whitePointEV)
        self.highlightBiasEV=float(highlightBiasEV)
        self.saturation=float(saturation)
        self.vibrance=float(vibrance)
        self.aePostGain=float(aePostGain)

class Preset:
    def __init__(self, params, config):
        self.params=params
        self.config=config

def neutral_baseline():
    # Keep synthetic/hardware-validated defaults in one named data object.
    # This is intentionally not called Natural until real-camera calibration.
    return Preset(
        Params(0.0, -10.0, 0.0, 0.0, 1.0, 2.0, 6.0, 0.0, 1.0, 0.0, 1.0),
        Config(0.18, -3.0, 0.90, 0.85, 0.75, 0.02, 0.10))

_NEUTRAL = neutral_baseline()
MIDDLE_GRAY = _NEUTRAL.config.middleGray
GAMUT_START = _NEUTRAL.config.gamutCompressionStart
EPS = 2.0**-20
MAX_AE_POST_GAIN = 16.0

def ParamsBaseline(**overrides):
    b=_NEUTRAL.params
    d=dict(exposureEV=b.exposureEV, blackPointEV=b.blackPointEV, shadowLiftEV=b.shadowLiftEV, midtoneLiftEV=b.midtoneLiftEV,
           contrast=b.contrast, shoulderStartEV=b.shoulderStartEV, whitePointEV=b.whitePointEV,
           highlightBiasEV=b.highlightBiasEV, saturation=b.saturation, vibrance=b.vibrance, aePostGain=b.aePostGain)
    d.update(overrides)
    return Params(**d)


def _monotone_tangents(x, y):
    x=np.asarray(x,dtype=np.float64); y=np.asarray(y,dtype=np.float64)
    h=np.diff(x); d=np.diff(y)/h
    m=np.zeros(5,dtype=np.float64)
    for i in range(1,4):
        dp, dn = d[i-1], d[i]
        if dp <= 0.0 or dn <= 0.0:
            m[i]=0.0
        else:
            hp, hn = h[i-1], h[i]
            w1=2.0*hn+hp; w2=hn+2.0*hp
            m[i]=(w1+w2)/(w1/dp+w2/dn)
    m[0]=0.0; m[4]=0.0
    # Hyman-style radial limiter, sufficient for monotone cubic Hermite.
    for i in range(4):
        if d[i] <= 0:
            m[i]=m[i+1]=0.0
            continue
        a=m[i]/d[i]; b=m[i+1]/d[i]
        s=a*a+b*b
        if s > 9.0:
            tau=3.0/math.sqrt(s)
            m[i]=tau*a*d[i]
            m[i+1]=tau*b*d[i]
    return m


def compile_curve(p: Params, cfg=None):
    if cfg is None:
        cfg=_NEUTRAL.config
    # V1.1 quality refinement:
    # - Black point moves the black/toe endpoint only.
    # - The shadow anchor is fixed at -3 EV, so Black no longer drags the
    #   entire shadow control region with it.
    # - Contrast is the local slope at middle gray, rather than changing
    #   both shadow/highlight anchor values.
    # - Highlight controls cannot change shadow-side tangents.
    x0=float(p.blackPointEV)
    x1=cfg.shadowAnchorEV
    x2=0.0
    x3=float(p.shoulderStartEV)
    x4=float(p.whitePointEV)
    if not (x0 < x1 < x2 < x3 < x4):
        raise ValueError(f'illegal tone anchors: {x0,x1,x2,x3,x4}')

    y0=0.0; y2=cfg.middleGray; y4=1.0
    # V2 base curve stays neutral; user tone controls are monotonic transforms
    # evaluated after this curve so they do not fight the Hermite limiter.
    y1=y2*(2.0**x1)
    y3=y2*(2.0**x3)
    tiny=1e-6
    y1=min(max(y1,tiny), y2-tiny)
    y3=min(max(y3,y2+tiny), cfg.shoulderOutputCap)

    x=np.array([x0,x1,x2,x3,x4],dtype=np.float64)
    y=np.array([y0,y1,y2,y3,y4],dtype=np.float64)
    h=np.diff(x); d=np.diff(y)/h
    m=np.zeros(5,dtype=np.float64)

    def harmonic(i):
        dp,dn=d[i-1],d[i]
        if dp <= 0.0 or dn <= 0.0:
            return 0.0
        hp,hn=h[i-1],h[i]
        w1=2.0*hn+hp; w2=hn+2.0*hp
        return (w1+w2)/(w1/dp+w2/dn)

    m[0]=0.0; m[4]=0.0
    m[1]=harmonic(1)  # lower side only
    m[3]=harmonic(3)  # upper side only
    # Contrast now has a precise meaning: desired dY/d(scene EV) at 18% gray.
    m[2]=cfg.middleGray*math.log(2.0)

    # Monotone radial limiter. Iterate because limiting one shared endpoint
    # can alter the normalized slope of its neighboring segment.
    for _ in range(3):
        for i in range(4):
            if d[i] <= 0.0:
                m[i]=m[i+1]=0.0
                continue
            a=m[i]/d[i]; b=m[i+1]/d[i]
            ss=a*a+b*b
            if ss > 9.0:
                tau=3.0/math.sqrt(ss)
                m[i]=tau*a*d[i]
                m[i+1]=tau*b*d[i]

    coeff=np.zeros((4,4),dtype=np.float64)
    for i in range(4):
        hi=h[i]
        yi,yj=y[i],y[i+1]; mi,mj=m[i],m[i+1]
        coeff[i]=[2*yi-2*yj+hi*(mi+mj),
                  -3*yi+3*yj-hi*(2*mi+mj),
                  hi*mi, yi]
    return x,y,m,coeff


def eval_curve(xv, knots, coeff):
    v=np.asarray(xv,dtype=np.float64)
    out=np.empty_like(v)
    out[v <= knots[0]]=0.0
    out[v >= knots[4]]=1.0
    mid=(v>knots[0]) & (v<knots[4])
    vm=v[mid]
    idx=np.searchsorted(knots[1:4], vm, side='right')
    lo=knots[idx]; hi=knots[idx+1]
    t=(vm-lo)/(hi-lo)
    c=coeff[idx]
    out[mid]=((c[:,0]*t+c[:,1])*t+c[:,2])*t+c[:,3]
    return out


def apply_tone_shape_v2(y, p, cfg):
    y=np.clip(np.asarray(y,dtype=np.float64),0.0,1.0)
    pivot=cfg.middleGray
    lower=(y>0.0)&(y<pivot)
    if abs(p.shadowLiftEV)>1e-12:
        u=np.clip(y/pivot,0.0,1.0)
        gamma_s=2.0**(-0.5*p.shadowLiftEV)
        y=np.where(lower,pivot*np.power(u,gamma_s),y)
    if abs(p.midtoneLiftEV)>1e-12:
        g=2.0**p.midtoneLiftEV
        y=np.where((y>0.0)&(y<1.0),(g*y)/np.maximum(1.0-y+g*y,EPS),y)
    c=max(float(p.contrast),0.05)
    lo=y<pivot; hi=y>pivot
    ulo=np.clip(y/pivot,0.0,1.0)
    uhi=np.clip((1.0-y)/(1.0-pivot),0.0,1.0)
    y=np.where(lo,pivot*np.power(ulo,c),y)
    y=np.where(hi,1.0-(1.0-pivot)*np.power(uhi,c),y)
    if abs(p.highlightBiasEV)>1e-12:
        upper=(y>pivot)&(y<1.0)
        u=np.clip((y-pivot)/(1.0-pivot),0.0,1.0)
        gamma_h=2.0**(-p.highlightBiasEV)
        y=np.where(upper,pivot+(1.0-pivot)*np.power(u,gamma_h),y)
    return np.clip(y,0.0,1.0)

def gamut_compress(rgb, start=GAMUT_START):
    rgb=np.asarray(rgb,dtype=np.float64)
    Y=np.sum(rgb*SRGB_LUMA,axis=-1,keepdims=True)
    N=np.repeat(Y,3,axis=-1)
    C=rgb-N
    H=np.where(C>=0.0, np.maximum(1.0-Y,EPS), np.maximum(Y,EPS))
    ratios=np.where(C>=0.0,C/H,-C/H)
    r=np.max(ratios,axis=-1,keepdims=True)
    d=np.maximum(r-start,0.0)
    q=max(1.0-start,1e-6)
    rc=np.where(r<=start,r,start+d/(1.0+d/q))
    k=np.where(r>EPS,rc/np.maximum(r,EPS),1.0)
    return N+k*C


def apply_color_controls(rgb, saturation=1.0, vibrance=0.0, cfg=None):
    if cfg is None:
        cfg=_NEUTRAL.config
    """V1.3A cheap AP1 saturation + selective vibrance.

    Saturation is uniform luminance-preserving chroma scale. Vibrance is an
    additional chroma scale whose strength falls quadratically as an HSV-like
    saturation proxy approaches 1, and fades to zero in deep shadows. This
    avoids hue-specific masks and preserves the AP1 chroma direction exactly.
    """
    rgb=np.asarray(rgb,dtype=np.float64)
    Y=np.sum(rgb*AP1_LUMA,axis=-1,keepdims=True)
    C=rgb-Y
    sat_rgb=Y+float(saturation)*C
    span=np.max(sat_rgb,axis=-1,keepdims=True)-np.min(sat_rgb,axis=-1,keepdims=True)
    peak=np.max(np.abs(sat_rgb),axis=-1,keepdims=True)
    chroma_proxy=np.clip(span/np.maximum(peak,EPS),0.0,1.0)
    selective=(1.0-chroma_proxy)**2
    # Smoothly suppress vibrance in the deepest shadows where chroma boosts are
    # more likely to reveal noise. 0 below ~2% linear, fully active by 10%.
    t=np.clip((Y-cfg.vibranceShadowStart)/(cfg.vibranceShadowEnd-cfg.vibranceShadowStart),0.0,1.0)
    shadow_guard=t*t*(3.0-2.0*t)
    gain=1.0 + np.clip(float(vibrance),-1.0,1.0)*cfg.vibranceStrength*selective*shadow_guard
    return Y + gain*(sat_rgb-Y)


def srgb_oetf(x):
    x=np.asarray(x,dtype=np.float64)
    return np.where(x<=0.0031308,12.92*x,1.055*np.power(np.maximum(x,0.0),1.0/2.4)-0.055)


def render(rgb_camera, p: Params, camera_to_ap1=None, return_stages=False, cfg=None):
    if not np.isfinite(p.aePostGain) or not (0.0 < p.aePostGain <= MAX_AE_POST_GAIN):
        raise ValueError('aePostGain must be finite, > 0, and <= 16.0')
    if cfg is None:
        cfg=_NEUTRAL.config
    rgb=np.asarray(rgb_camera,dtype=np.float64)
    if camera_to_ap1 is None:
        camera_to_ap1=np.eye(3,dtype=np.float64)
    work=rgb @ np.asarray(camera_to_ap1,dtype=np.float64).T
    Yin=np.sum(work*AP1_LUMA,axis=-1)
    safe=np.maximum(Yin,EPS)
    x=np.log2(safe/cfg.middleGray)+np.log2(p.aePostGain)+p.exposureEV
    knots,ys,m,coeff=compile_curve(p,cfg)
    Yout=apply_tone_shape_v2(eval_curve(x,knots,coeff),p,cfg)
    scale=np.where(Yin>EPS,Yout/safe,0.0)
    toned=work*scale[...,None]
    color=apply_color_controls(toned,p.saturation,p.vibrance,cfg)
    outlin=color @ M_AP1_TO_SRGB.T
    gamut=gamut_compress_v12(outlin,start=cfg.gamutCompressionStart)
    final=np.clip(srgb_oetf(np.clip(gamut,0.0,1.0)),0.0,1.0)
    if return_stages:
        return final, dict(work=work,Yin=Yin,x=x,Yout=Yout,toned=toned,color=color,outlin=outlin,gamut=gamut,
                           knots=knots,ys=ys,tangents=m,coeff=coeff)
    return final

# V1.2 offline/production-quality gamut helpers.
# OKLab is used only for pixels that actually require destination-gamut compression.
_OK_M1 = np.array([[0.4122214708,0.5363325363,0.0514459929],
                   [0.2119034982,0.6806995451,0.1073969566],
                   [0.0883024619,0.2817188376,0.6299787005]], dtype=np.float64)
_OK_M2 = np.array([[0.2104542553,0.7936177850,-0.0040720468],
                   [1.9779984951,-2.4285922050,0.4505937099],
                   [0.0259040371,0.7827717662,-0.8086757660]], dtype=np.float64)
_OK_M1I = np.linalg.inv(_OK_M1)
_OK_M2I = np.linalg.inv(_OK_M2)

def linear_srgb_to_oklab(rgb):
    rgb=np.asarray(rgb,dtype=np.float64)
    return np.cbrt(rgb @ _OK_M1.T) @ _OK_M2.T

def oklab_to_linear_srgb(lab):
    lab=np.asarray(lab,dtype=np.float64)
    lms_=lab @ _OK_M2I.T
    return (lms_**3) @ _OK_M1I.T

def _compute_max_saturation(a, b):
    """Bjorn Ottosson sRGB/OKLab max-saturation approximation + one Halley step.

    a,b are normalized OKLab hue directions. Formula adapted from the MIT-licensed
    reference implementation at https://bottosson.github.io/posts/gamutclipping/.
    """
    a=np.asarray(a,dtype=np.float64); b=np.asarray(b,dtype=np.float64)
    red=(-1.88170328*a - 0.80936493*b) > 1.0
    green=(~red) & ((1.81444104*a - 1.19445276*b) > 1.0)
    blue=~(red|green)
    k0=np.empty_like(a); k1=np.empty_like(a); k2=np.empty_like(a); k3=np.empty_like(a); k4=np.empty_like(a)
    wl=np.empty_like(a); wm=np.empty_like(a); ws=np.empty_like(a)
    vals=[(red,(1.19086277,1.76576728,0.59662641,0.75515197,0.56771245,4.0767416621,-3.3077115913,0.2309699292)),
          (green,(0.73956515,-0.45954404,0.08285427,0.12541070,0.14503204,-1.2684380046,2.6097574011,-0.3413193965)),
          (blue,(1.35733652,-0.00915799,-1.15130210,-0.50559606,0.00692167,-0.0041960863,-0.7034186147,1.7076147010))]
    for mask,v in vals:
        k0[mask],k1[mask],k2[mask],k3[mask],k4[mask],wl[mask],wm[mask],ws[mask]=v
    S=k0+k1*a+k2*b+k3*a*a+k4*a*b
    kl=0.3963377774*a+0.2158037573*b
    km=-0.1055613458*a-0.0638541728*b
    ks=-0.0894841775*a-1.2914855480*b
    lp=1+S*kl; mp=1+S*km; sp=1+S*ks
    l=lp**3; m=mp**3; ss=sp**3
    ld=3*kl*lp*lp; md=3*km*mp*mp; sd=3*ks*sp*sp
    ld2=6*kl*kl*lp; md2=6*km*km*mp; sd2=6*ks*ks*sp
    f=wl*l+wm*m+ws*ss; f1=wl*ld+wm*md+ws*sd; f2=wl*ld2+wm*md2+ws*sd2
    den=f1*f1-0.5*f*f2
    S=S-np.divide(f*f1,den,out=np.zeros_like(S),where=np.abs(den)>1e-20)
    return S

def _find_cusp(a,b):
    S=_compute_max_saturation(a,b)
    rgb=oklab_to_linear_srgb(np.stack([np.ones_like(S),S*a,S*b],axis=-1))
    mx=np.maximum(np.max(rgb,axis=-1),1e-20)
    L=np.cbrt(1.0/mx)
    return L,L*S

def _find_gamut_intersection(a,b,L1,C1,L0):
    """Analytic sRGB gamut intersection: triangle estimate + one Halley refinement."""
    a=np.asarray(a,float); b=np.asarray(b,float); L1=np.asarray(L1,float); C1=np.asarray(C1,float); L0=np.asarray(L0,float)
    Lc,Cc=_find_cusp(a,b)
    lower=((L1-L0)*Cc-(Lc-L0)*C1)<=0.0
    den_lo=C1*Lc+Cc*(L0-L1)
    den_hi=C1*(Lc-1.0)+Cc*(L0-L1)
    tlo=np.divide(Cc*L0,den_lo,out=np.zeros_like(L1),where=np.abs(den_lo)>1e-20)
    thi=np.divide(Cc*(L0-1.0),den_hi,out=np.zeros_like(L1),where=np.abs(den_hi)>1e-20)
    t=np.where(lower,tlo,thi)
    upper=~lower
    if np.any(upper):
        au=a[upper]; bu=b[upper]; l1=L1[upper]; c1=C1[upper]; l0=L0[upper]; tt=t[upper]
        dL=l1-l0; dC=c1
        kl=0.3963377774*au+0.2158037573*bu
        km=-0.1055613458*au-0.0638541728*bu
        ks=-0.0894841775*au-1.2914855480*bu
        ldt=dL+dC*kl; mdt=dL+dC*km; sdt=dL+dC*ks
        L=l0*(1-tt)+tt*l1; C=tt*c1
        lp=L+C*kl; mp=L+C*km; sp=L+C*ks
        lv=lp**3; mv=mp**3; sv=sp**3
        ld=3*ldt*lp*lp; md=3*mdt*mp*mp; sd=3*sdt*sp*sp
        ld2=6*ldt*ldt*lp; md2=6*mdt*mdt*mp; sd2=6*sdt*sdt*sp
        coeff=((4.0767416621,-3.3077115913,0.2309699292),(-1.2684380046,2.6097574011,-0.3413193965),(-0.0041960863,-0.7034186147,1.7076147010))
        corr=[]
        for cr,cg,cb in coeff:
            f=cr*lv+cg*mv+cb*sv-1.0
            f1=cr*ld+cg*md+cb*sd
            f2=cr*ld2+cg*md2+cb*sd2
            den=f1*f1-0.5*f*f2
            u=np.divide(f1,den,out=np.full_like(f1,-1.0),where=np.abs(den)>1e-20)
            dt=-f*u
            corr.append(np.where(u>=0.0,dt,np.inf))
        tt=tt+np.minimum(corr[0],np.minimum(corr[1],corr[2]))
        t[upper]=tt
    return np.clip(t,0.0,1.0)

def gamut_compress_v121(rgb, start=GAMUT_START):
    """V1.2.1: V1.2 smooth RGB precompression + analytic fixed-L OKLab hue restoration.

    This preserves V1.2's intended appearance but replaces the 12-step chroma
    bisection with the sRGB/OKLab analytic gamut intersection from Ottosson's
    MIT-licensed reference method (cusp fit + Halley refinement).
    """
    rgb=np.asarray(rgb,dtype=np.float64)
    Y=np.sum(rgb*SRGB_LUMA,axis=-1,keepdims=True)
    Ya=np.clip(Y,0.0,1.0); N=np.repeat(Ya,3,axis=-1); C=rgb-N
    H=np.where(C>=0.0,np.maximum(1.0-Ya,EPS),np.maximum(Ya,EPS))
    ratios=np.where(C>=0.0,C/H,-C/H); r=np.max(ratios,axis=-1,keepdims=True)
    d=np.maximum(r-start,0.0); q=max(1.0-start,1e-6)
    rc=np.where(r<=start,r,start+d/(1.0+d/q)); k=np.where(r>EPS,rc/np.maximum(r,EPS),1.0)
    prelim=N+k*C
    need=(r[...,0]>start)|(Y[...,0]<0.0)|(Y[...,0]>1.0)
    if not np.any(need): return prelim
    flat_rgb=rgb.reshape(-1,3); flat_pre=prelim.reshape(-1,3); flat_need=need.reshape(-1); out=flat_pre.copy()
    src=flat_rgb[flat_need]; pre=np.clip(flat_pre[flat_need],0.0,1.0)
    lab0=linear_srgb_to_oklab(src); labp=linear_srgb_to_oklab(pre)
    C0=np.hypot(lab0[:,1],lab0[:,2]); Cp=np.hypot(labp[:,1],labp[:,2])
    ua=np.divide(lab0[:,1],C0,out=np.ones_like(C0),where=C0>1e-12); ub=np.divide(lab0[:,2],C0,out=np.zeros_like(C0),where=C0>1e-12)
    candidate=oklab_to_linear_srgb(np.stack([labp[:,0],ua*Cp,ub*Cp],axis=-1))
    ok=np.all((candidate>=0.0)&(candidate<=1.0),axis=-1)
    corrected=candidate.copy()
    if np.any(~ok):
        L=labp[~ok,0]; c=Cp[~ok]; aa=ua[~ok]; bb=ub[~ok]
        t=_find_gamut_intersection(aa,bb,L,c,L)
        corrected[~ok]=oklab_to_linear_srgb(np.stack([L,aa*(t*c),bb*(t*c)],axis=-1))
    out[flat_need]=np.clip(corrected,0.0,1.0)
    return out.reshape(rgb.shape)

# Compatibility name used by the rest of the V1.2 test harness.
gamut_compress_v12 = gamut_compress_v121

def dither_hash01(x, y, seed=0):
    """Legacy V1.2.1 hash retained only for offline A/B diagnostics."""
    x=np.asarray(x,dtype=np.uint32); y=np.asarray(y,dtype=np.uint32)
    n=(x*np.uint32(0x1f123bb5)) ^ (y*np.uint32(0x5f356495)) ^ np.uint32(seed)
    n ^= n >> np.uint32(16); n *= np.uint32(0x7feb352d)
    n ^= n >> np.uint32(15); n *= np.uint32(0x846ca68b)
    n ^= n >> np.uint32(16)
    return (n & np.uint32(0x00ffffff)).astype(np.float64)/16777216.0

def dither_codes_v122(x, y):
    """Low-discrepancy interleaved-gradient dither in output-code units."""
    xf=np.asarray(x,dtype=np.float64); yf=np.asarray(y,dtype=np.float64)
    inner=np.mod(0.06711056*xf + 0.00583715*yf, 1.0)
    return np.mod(52.9829189*inner,1.0)-0.5

def apply_rgba8_dither(encoded, x=None, y=None):
    """V1.2.2 static neutral low-discrepancy dither, +/-0.5 output code."""
    a=np.asarray(encoded,dtype=np.float64)
    h,w=a.shape[-3],a.shape[-2]
    if x is None or y is None:
        yy,xx=np.indices((h,w),dtype=np.uint32)
    else:
        xx=np.asarray(x,dtype=np.uint32); yy=np.asarray(y,dtype=np.uint32)
    n=dither_codes_v122(xx,yy)/255.0
    return np.clip(a+n[...,None],0.0,1.0)
