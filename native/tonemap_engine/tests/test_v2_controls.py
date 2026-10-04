#!/usr/bin/env python3
import pathlib, sys, numpy as np
ROOT=pathlib.Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'python'))
from tonemap_ref import ParamsBaseline, render

def req(c,m):
    if not c: raise AssertionError(m)

def yout(p):
    ev=np.linspace(-10,6,4097)
    rgb=(0.18*np.exp2(ev))[:,None]*np.ones((1,3))
    _,st=render(rgb,p,return_stages=True)
    return ev,st['Yout']

def main():
    controls=[
        ParamsBaseline(shadowLiftEV=-1),ParamsBaseline(shadowLiftEV=1.5),
        ParamsBaseline(midtoneLiftEV=-1),ParamsBaseline(midtoneLiftEV=1),
        ParamsBaseline(contrast=2**-0.85),ParamsBaseline(contrast=2**0.85),
        ParamsBaseline(highlightBiasEV=-1),ParamsBaseline(highlightBiasEV=.5),
    ]
    for p in controls:
        ev,y=yout(p)
        req(np.all(np.diff(y)>=-1e-12),'v2 control made tone response non-monotonic')
        req(abs(y[-1]-1)<1e-12,'white endpoint moved')
    ev,n=yout(ParamsBaseline())
    _,s=yout(ParamsBaseline(shadowLiftEV=1.5))
    req(np.max(np.abs(s[ev>=0]-n[ev>=0]))<2e-12,'shadows leaked above middle gray')
    _,h=yout(ParamsBaseline(highlightBiasEV=-1))
    req(np.max(np.abs(h[ev<=0]-n[ev<=0]))<2e-12,'highlights leaked below middle gray')
    # +100 contrast should be visibly stronger than V1's limiter-collapsed max.
    _,c=yout(ParamsBaseline(contrast=2**0.85))
    i0=np.argmin(abs(ev)); im=np.argmin(abs(ev+1)); ip=np.argmin(abs(ev-1))
    req((c[ip]-c[im]) > 1.5*(n[ip]-n[im]), 'contrast +100 is still too weak')
    # Highlights -100 must preserve white while being much gentler at +2 EV than old V1 0.36 anchor.
    i2=np.argmin(abs(ev-2))
    req(h[i2] > 0.50 and h[i2] < n[i2], 'highlight -100 shoulder semantics unexpected')
    # PostGain is deliberately the same pre-tone EV coordinate as render exposure.
    rng=np.random.default_rng(7); rgb=rng.uniform(.001,2.0,(4096,3))
    a=render(rgb,ParamsBaseline(aePostGain=2.0))
    b=render(rgb,ParamsBaseline(exposureEV=1.0))
    req(np.max(np.abs(a-b))<3e-14,'postGain 2x != +1 EV render exposure')
    print('TONEMAP_V2_CONTROL_CONTRACT_PASS')
if __name__=='__main__': main()
