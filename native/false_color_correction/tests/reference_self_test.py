#!/usr/bin/env python3
import pathlib,sys,numpy as np
sys.path.insert(0,str(pathlib.Path(__file__).resolve().parents[1]/"tools"))
from fcc_reference import fcc
rng=np.random.default_rng(12345)
x=rng.uniform(-.2,1.8,size=(13,17,4)).astype(np.float32);x[...,3]=1
for n in (1,2):
    y=fcc(x,n)
    assert y.shape==x.shape and np.isfinite(y).all()
    # Top/bottom rows are untouched by pinned serial RT code.
    assert np.array_equal(y[0],x[0])
    if n==1: assert np.array_equal(y[-1],x[-1])
print("FCC_REFERENCE_SELF_TEST_PASS")
