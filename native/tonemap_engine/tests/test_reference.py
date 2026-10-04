import os, sys, math
import numpy as np
sys.path.insert(0, os.path.join(os.path.dirname(__file__),'..','python'))
from tonemap_ref import *

def assert_true(v,msg):
    if not v: raise AssertionError(msg)

def poly_value(c,t):
    return ((c[0]*t+c[1])*t+c[2])*t+c[3]

def poly_dt(c,t):
    return (3*c[0]*t+2*c[1])*t+c[2]

def test_curve_monotonicity_continuity():
    cases=[]
    for black in [-14,-10,-6]:
      for shadow in [-2,0,1.0]:
       for contrast in [0.6,1.0,1.5]:
        for ss in [0.75,2.0,3.5]:
         for white in [5.0,8.0,10.0]:
          if ss >= white: continue
          for hb in [-1.0,0,0.25]:
            cases.append(ParamsBaseline(blackPointEV=black,shadowLiftEV=shadow,contrast=contrast,
                                shoulderStartEV=ss,whitePointEV=white,highlightBiasEV=hb))
    for p in cases:
        k,y,m,c=compile_curve(p)
        xs=np.linspace(k[0]-1,k[4]+1,20000)
        ys=eval_curve(xs,k,c)
        assert_true(np.min(np.diff(ys)) >= -2e-11, 'non-monotone tone')
        for j in range(1,4):
            # Exact polynomial endpoint value and derivative continuity.
            yl=poly_value(c[j-1],1.0); yr=poly_value(c[j],0.0)
            dl=poly_dt(c[j-1],1.0)/(k[j]-k[j-1])
            dr=poly_dt(c[j],0.0)/(k[j+1]-k[j])
            assert_true(abs(yl-yr)<2e-12,'value discontinuity')
            assert_true(abs(dl-dr)<2e-12,'derivative discontinuity')
        assert_true(abs(poly_dt(c[0],0.0))<2e-12,'black slope not zero')
        assert_true(abs(poly_dt(c[3],1.0))<2e-12,'white slope not zero')

def test_neutral_and_exposure():
    p=ParamsBaseline()
    v=np.geomspace(1e-6,16,2000)
    rgb=np.stack([v,v,v],axis=-1)
    out=render(rgb,p)
    assert_true(np.max(np.abs(out[:,0]-out[:,1]))<3e-6,'neutral R/G')
    assert_true(np.max(np.abs(out[:,1]-out[:,2]))<3e-6,'neutral G/B')
    lum=[]
    c=np.array([[0.24,0.10,0.03]])
    for ev in np.linspace(-5,5,81):
        o=render(c,ParamsBaseline(exposureEV=ev))[0]
        lum.append(o @ SRGB_LUMA)
    assert_true(np.min(np.diff(lum))>=-2e-12,'exposure not monotone')

def test_tone_hue_ratios():
    colors=np.array([[1.0,.2,.05],[.05,.8,1.0],[1.0,.05,.8],[.1,.1,1.0],[.8,.45,.25]],float)
    scales=np.array([.25,.5,1,2,4,8,16.0])
    for c in colors:
      base=c/c.max()
      for s in scales:
        _,st=render((c*s)[None,:],ParamsBaseline(),return_stages=True)
        t=st['toned'][0]
        if t.max()>1e-10:
          ratio=t/t.max()
          assert_true(np.max(np.abs(ratio-base))<2e-12,'tone hue ratio changed')

def test_gamut_continuity_and_bounds():
    Y=.7
    dirs=np.array([[1,-.2,-.1],[-.2,1,-.3],[1,-.4,1]],float)
    for d in dirs:
      d=d-(d@SRGB_LUMA)
      vals=[]
      for a in np.linspace(0,5,5000):
        x=np.array([Y,Y,Y])+a*d
        g=gamut_compress(x[None,:])[0]
        vals.append(g)
      vals=np.array(vals)
      assert_true(np.isfinite(vals).all(),'gamut non-finite')
      assert_true(vals.min()>-1e-9 and vals.max()<1+1e-9,'gamut out of bounds')
      lum=vals@SRGB_LUMA
      assert_true(np.max(np.abs(lum-Y))<2e-12,'gamut changed luminance')
      jumps=np.linalg.norm(np.diff(vals,axis=0),axis=1)
      assert_true(np.max(jumps)<.02,'gamut discontinuity')

def test_finite_extremes():
    rng=np.random.default_rng(7)
    rgb=rng.normal(0.3,4.0,(300000,3))
    for ev in [-8,-5,0,5,8]:
      out=render(rgb,ParamsBaseline(exposureEV=ev, saturation=1.5))
      assert_true(np.isfinite(out).all(),'NaN/Inf')

def test_random_legal_parameter_sweep():
    rng=np.random.default_rng(19)
    rgb=rng.normal(.25,3.0,(2048,3))
    for _ in range(300):
        black=rng.uniform(-15,-3.25)
        shoulder=rng.uniform(.5,4.5)
        white=rng.uniform(shoulder+.5,11.0)
        p=ParamsBaseline(exposureEV=rng.uniform(-8,8), blackPointEV=black,
                 shadowLiftEV=rng.uniform(-2,1), contrast=rng.uniform(.5,1.5),
                 shoulderStartEV=shoulder, whitePointEV=white,
                 highlightBiasEV=rng.uniform(-1,.25), saturation=rng.uniform(0,2), vibrance=rng.uniform(-1,1))
        k,y,m,c=compile_curve(p)
        xs=np.linspace(k[0],k[4],4096)
        curve=eval_curve(xs,k,c)
        assert_true(np.isfinite(curve).all(),'curve non-finite')
        assert_true(np.min(np.diff(curve))>=-2e-10,'random curve non-monotone')
        out=render(rgb,p)
        assert_true(np.isfinite(out).all(),'random render non-finite')
        assert_true(out.min()>=-1e-12 and out.max()<=1+1e-12,'random render out of range')

def test_control_locality_semantics():
    xs=np.linspace(-12,9,24001)
    def curve(p):
        k,_,_,c=compile_curve(p)
        return eval_curve(xs,k,c)
    base=curve(ParamsBaseline())

    # Normal-range highlight changes must not alter the shadow half.
    for hb in [-1.0,-0.5,0.25]:
        y=curve(ParamsBaseline(highlightBiasEV=hb))
        assert_true(np.max(np.abs(y[xs<0]-base[xs<0]))<2e-12,
                    'highlight control leaked into shadows')

    # Normal-range shadow changes must not alter the highlight half.
    for sh in [-2.0,-1.0,1.0]:
        y=curve(ParamsBaseline(shadowLiftEV=sh))
        assert_true(np.max(np.abs(y[xs>0]-base[xs>0]))<2e-12,
                    'shadow control leaked into highlights')

    # Black should be strongly localized to the lower region.
    for black in [-12.0,-8.0]:
        y=curve(ParamsBaseline(blackPointEV=black))
        assert_true(np.max(np.abs(y[xs>=0]-base[xs>=0]))<2e-12,
                    'black point leaked into highlights')

    # Middle gray remains an invariant across all photographic controls.
    for p in [ParamsBaseline(shadowLiftEV=1),ParamsBaseline(blackPointEV=-8),ParamsBaseline(contrast=1.5),
              ParamsBaseline(shoulderStartEV=3),ParamsBaseline(whitePointEV=8),ParamsBaseline(highlightBiasEV=-1)]:
        k,_,_,c=compile_curve(p)
        assert_true(abs(float(eval_curve(np.array([0.0]),k,c)[0])-MIDDLE_GRAY)<2e-12,
                    'middle gray anchor drift')



def test_highlight_extreme_saturation_and_slope():
    xs=np.linspace(0.0,3.0,12001)
    def curve(p):
        k,_,_,c=compile_curve(p)
        return eval_curve(xs,k,c)
    # Public/API extremes must saturate to the validated photographic endpoints.
    lo=curve(ParamsBaseline(highlightBiasEV=-1.0))
    hi=curve(ParamsBaseline(highlightBiasEV=0.25))
    assert_true(np.max(np.abs(curve(ParamsBaseline(highlightBiasEV=-8.0))-lo))<2e-12,
                'negative highlight extreme not saturated')
    assert_true(np.max(np.abs(curve(ParamsBaseline(highlightBiasEV=8.0))-hi))<2e-12,
                'positive highlight extreme not saturated')
    # In the active upper-midtone region, legal Highlights settings must retain
    # a useful slope instead of forming a visible plateau. Ignore the final white
    # asymptote where slope is intentionally allowed to approach zero.
    active=(xs>=0.0)&(xs<=2.0)
    for hb in [-1.0,-0.75,-0.5,0.0,0.25]:
        y=curve(ParamsBaseline(highlightBiasEV=hb))
        dy=np.gradient(y,xs)
        assert_true(np.percentile(dy[active],5)>0.045,
                    f'highlight {hb} collapses upper-midtone slope')


def test_v12_gamut_hue_and_continuity():
    from tonemap_ref import linear_srgb_to_oklab, gamut_compress_v12, M_AP1_TO_SRGB, AP1_LUMA, MIDDLE_GRAY, EPS
    colors=[np.array([1.,.03,.02]),np.array([1.,.22,.03]),np.array([1.,.75,.03]),
            np.array([.03,1.,.05]),np.array([.02,.9,1.]),np.array([.03,.12,1.]),np.array([1.,.03,.75])]
    ev=np.linspace(-4,9,20001)
    for c in colors:
        vals=c[None,:]*(2.0**ev)[:,None]
        # render() exercises the complete V1.2 path
        out,st=render(vals,ParamsBaseline(),return_stages=True)
        g=st['gamut']; assert_true(np.min(g)>=-1e-7 and np.max(g)<=1.0000001,'V1.2 gamut must be bounded before OETF')
        l0=linear_srgb_to_oklab(st['outlin']); lg=linear_srgb_to_oklab(g)
        c0=np.hypot(l0[:,1],l0[:,2]); cg=np.hypot(lg[:,1],lg[:,2])
        valid=(c0>.02)&(cg>.02)
        h0=np.arctan2(l0[:,2],l0[:,1]); hg=np.arctan2(lg[:,2],lg[:,1])
        dh=np.abs(np.angle(np.exp(1j*(hg-h0))))
        assert_true(np.max(np.degrees(dh[valid]))<0.05,'V1.2 compressed hue must follow source OKLab hue')
        jumps=np.max(np.abs(np.diff(out,axis=0)),axis=1)
        # Detect isolated discontinuity spikes rather than rejecting legitimate steep smooth slopes.
        local=np.maximum(np.roll(jumps,1),np.roll(jumps,-1)); local=np.maximum(local,1e-9)
        ratio=jumps[2:-2]/local[2:-2]
        spike=jumps[2:-2][ratio>1.20]
        assert_true(spike.size==0 or np.max(spike)<(0.25/255.0),'V1.2 hue correction created a visible isolated trajectory discontinuity')

def test_output_dither_properties():
    from tonemap_ref import apply_rgba8_dither
    h,w=256,2048
    base=np.tile(np.linspace(.02,.08,w)[None,:,None],(h,1,3))
    d=apply_rgba8_dither(base)
    err=(d-base)*255.0
    assert_true(abs(float(np.mean(err)))<0.01,'dither must be effectively zero mean')
    assert_true(float(np.max(np.abs(err)))<=0.50001,'dither amplitude must stay inside half an 8-bit code')
    std=float(np.std(err))
    assert_true(0.285 < std < 0.292,'dither RMS must match the intended uniform half-code distribution')
    assert_true(float(np.max(np.abs(d[...,0]-d[...,1])))<1e-14 and float(np.max(np.abs(d[...,1]-d[...,2])))<1e-14,
                'neutral dither must not introduce chroma noise')
    q0=np.rint(base*255).astype(np.uint8); q1=np.rint(d*255).astype(np.uint8)
    # Undithered shallow gradients form perfectly coherent vertical code bands.
    # Dither must break that 2D coherence: at a transition region, rows must not all quantize identically.
    varied=np.sum(np.ptp(q1[...,0],axis=0)>0)
    assert_true(varied>w//2,'dither must decorrelate 8-bit contour boundaries across scanlines')
    # Downsampling/defocus should reconstruct the underlying smooth ramp rather than
    # reveal coherent vertical contours. Measure row-averaged quantization error.
    target=base[...,0]*255.0
    qerr=q1[...,0].astype(np.float64)-target
    col=np.mean(qerr,axis=0)
    assert_true(float(np.sqrt(np.mean(col*col)))<0.008,'dither leaves excessive coherent vertical contour energy')


def test_vibrance_semantics():
    # Neutral colors must remain exactly neutral for the full public range.
    neutral=np.array([[0.05,0.05,0.05],[0.18,0.18,0.18],[0.7,0.7,0.7]],dtype=np.float64)
    for v in (-1.0,-0.5,0.0,0.5,1.0,8.0,-8.0):
        out=apply_color_controls(neutral,1.0,v)
        assert_true(np.max(np.abs(out-neutral))<1e-14,'vibrance altered neutral axis')

    # v=0 is exactly the existing saturation behavior.
    rng=np.random.default_rng(123)
    rgb=rng.uniform(-0.05,1.25,(10000,3))
    Y=np.sum(rgb*AP1_LUMA,axis=-1,keepdims=True)
    for sat in (0.0,0.5,1.0,1.5,2.0):
        ref=Y+sat*(rgb-Y)
        got=apply_color_controls(rgb,sat,0.0)
        assert_true(np.max(np.abs(got-ref))<2e-14,'vibrance neutral changed V1.2.2 saturation math')

    # Vibrance must preserve AP1 luminance and chroma direction: it is a scalar
    # chroma operation, not a hue rotator.
    rgb=rng.uniform(0.01,0.9,(20000,3))
    Y=np.sum(rgb*AP1_LUMA,axis=-1,keepdims=True)
    C0=rgb-Y
    for v in (-1.0,-0.5,0.5,1.0):
        got=apply_color_controls(rgb,1.0,v)
        Yg=np.sum(got*AP1_LUMA,axis=-1,keepdims=True)
        assert_true(np.max(np.abs(Yg-Y))<5e-13,'vibrance changed AP1 luminance')
        C1=got-Yg
        # Cross product of input/output chroma should vanish.
        cross=np.cross(C0,C1)
        assert_true(np.max(np.linalg.norm(cross,axis=1))<2e-13,'vibrance rotated AP1 chroma direction')

    # Positive vibrance must preferentially boost weak color, restrain a
    # skin-like moderate color, and barely touch an already-saturated color.
    def gain(c):
        c=np.asarray(c,dtype=np.float64); y=float(c@AP1_LUMA)
        out=apply_color_controls(c,1.0,1.0)
        return float(np.linalg.norm(out-y)/max(np.linalg.norm(c-y),1e-12))
    weak=gain([0.50,0.45,0.42])
    skin=gain([0.60,0.36,0.25])
    saturated=gain([0.85,0.06,0.04])
    assert_true(weak>1.45,'positive vibrance does not materially boost weak color')
    assert_true(1.05<skin<1.20,'skin-like moderate color is not sufficiently restrained')
    assert_true(saturated<1.01,'already-saturated color receives excessive vibrance')
    assert_true(weak>skin>saturated,'vibrance selectivity ordering failed')

    # Deep-shadow guard: a weak color at 1% linear should receive effectively no
    # vibrance boost, avoiding unnecessary chroma-noise amplification.
    c=np.array([0.012,0.010,0.009],dtype=np.float64)
    y=float(c@AP1_LUMA)
    out=apply_color_controls(c,1.0,1.0)
    g=float(np.linalg.norm(out-y)/max(np.linalg.norm(c-y),1e-12))
    assert_true(abs(g-1.0)<1e-12,'deep-shadow vibrance guard failed')


def test_vibrance_output_hue_stability():
    # Offline perceptual check after AP1->sRGB, before gamut mapping. The AP1
    # chroma operation should not cause objectionable OKLab hue motion.
    colors=np.array([[.50,.45,.42],[.60,.36,.25],[.8,.3,.05],[.05,.65,.75],
                     [.08,.18,.85],[.85,.06,.04],[.55,.20,.55]],dtype=np.float64)
    base=apply_color_controls(colors,1.0,0.0) @ M_AP1_TO_SRGB.T
    lab0=linear_srgb_to_oklab(base)
    for v in (-1.0,-0.5,0.5,1.0):
        out=apply_color_controls(colors,1.0,v) @ M_AP1_TO_SRGB.T
        lab=linear_srgb_to_oklab(out)
        c0=np.hypot(lab0[:,1],lab0[:,2]); c1=np.hypot(lab[:,1],lab[:,2])
        valid=(c0>.01)&(c1>.01)
        h0=np.arctan2(lab0[:,2],lab0[:,1]); h=np.arctan2(lab[:,2],lab[:,1])
        dh=np.degrees(np.abs(np.angle(np.exp(1j*(h-h0)))))
        assert_true(np.max(dh[valid])<1.5,'cheap vibrance creates excessive perceptual hue drift')

def test_ae_post_gain_semantics():
    rgb=np.array([[0.03,0.02,0.01],[0.18,0.18,0.18],[0.8,0.25,0.05],[1.4,0.7,0.2],[2.5,1.5,0.9]],dtype=np.float64)

    # Identity: the new parameter must leave the established reference unchanged.
    base=render(rgb,ParamsBaseline())
    identity=render(rgb,ParamsBaseline(aePostGain=1.0))
    assert_true(np.array_equal(base,identity),'aePostGain=1 changed reference output')

    # Linear pre-tone semantics: post gain in powers of two is exactly equivalent
    # to the corresponding stop-domain render exposure at the tone input.
    for gain,ev in [(0.5,-1.0),(1.0,0.0),(2.0,1.0),(4.0,2.0)]:
        out_g,st_g=render(rgb,ParamsBaseline(aePostGain=gain),return_stages=True)
        out_e,st_e=render(rgb,ParamsBaseline(exposureEV=ev),return_stages=True)
        assert_true(np.max(np.abs(st_g['x']-st_e['x']))<2e-14,'AE post gain stop position mismatch')
        assert_true(np.max(np.abs(st_g['Yout']-st_e['Yout']))<2e-14,'AE post gain tone response mismatch')
        assert_true(np.max(np.abs(st_g['toned']-st_e['toned']))<2e-14,'AE post gain pre-tone equivalence mismatch')
        assert_true(np.max(np.abs(out_g-out_e))<2e-14,'AE post gain final equivalence mismatch')

    # API semantics remain independent: total pre-tone exposure is additive in stops.
    mixed,_=render(rgb,ParamsBaseline(aePostGain=2.0,exposureEV=-0.5),return_stages=True)
    combined,_=render(rgb,ParamsBaseline(aePostGain=1.0,exposureEV=0.5),return_stages=True)
    assert_true(np.max(np.abs(mixed-combined))<2e-14,'AE post gain/render exposure composition mismatch')

    # Above-white scene values remain finite and are not clamped before the shoulder.
    high=np.array([[1.2,0.6,0.3],[2.0,0.4,0.2],[4.0,2.0,1.0]],dtype=np.float64)
    out,st=render(high,ParamsBaseline(aePostGain=4.0),return_stages=True)
    assert_true(np.isfinite(out).all() and np.isfinite(st['x']).all(),'AE post gain highlight non-finite')
    assert_true(np.max(st['work'])>1.0,'test fixture lost highlight headroom before tone')
    for i in range(len(high)):
        t=st['toned'][i]
        w=st['work'][i]
        if np.max(np.abs(t))>1e-12 and np.max(np.abs(w))>1e-12:
            # Scalar tone rescale must retain working-RGB ratios.
            tn=t/np.max(np.abs(t)); wn=w/np.max(np.abs(w))
            assert_true(np.max(np.abs(tn-wn))<2e-12,'AE post gain changed tone hue ratios')

    # Equal-channel gain must not break the neutral axis.
    n=np.geomspace(1e-5,4.0,512)
    neutral=np.stack([n,n,n],axis=-1)
    out=render(neutral,ParamsBaseline(aePostGain=4.0))
    assert_true(np.max(np.abs(out[:,0]-out[:,1]))<3e-6,'AE post gain neutral R/G')
    assert_true(np.max(np.abs(out[:,1]-out[:,2]))<3e-6,'AE post gain neutral G/B')


    # Exact upper-bound contract: just below and exactly 16x are valid; above is rejected.
    for good in (np.nextafter(16.0, 0.0), 16.0):
        bounded=render(rgb,ParamsBaseline(aePostGain=good))
        assert_true(np.isfinite(bounded).all(),f'valid aePostGain rejected/non-finite: {good}')

    # CPU reference mirrors the public validation contract. No silent clamping.
    for bad in (float('nan'),float('inf'),0.0,-1.0,np.nextafter(16.0, np.inf),16.0001):
        try:
            render(rgb,ParamsBaseline(aePostGain=bad))
        except ValueError:
            pass
        else:
            raise AssertionError(f'invalid aePostGain accepted: {bad}')


def run_all():
    tests=[test_curve_monotonicity_continuity,test_neutral_and_exposure,test_tone_hue_ratios,
           test_gamut_continuity_and_bounds,test_finite_extremes,test_random_legal_parameter_sweep,
           test_control_locality_semantics,test_highlight_extreme_saturation_and_slope,
           test_v12_gamut_hue_and_continuity,test_output_dither_properties,
           test_vibrance_semantics,test_vibrance_output_hue_stability,test_ae_post_gain_semantics]
    for t in tests:
        t(); print('PASS',t.__name__)
    print('ALL_REFERENCE_TESTS_PASS')

if __name__=='__main__': run_all()
