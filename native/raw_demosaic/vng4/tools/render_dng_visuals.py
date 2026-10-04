#!/usr/bin/env python3
import argparse, json
from pathlib import Path
import numpy as np
import tifffile
from PIL import Image

def ratios(v):
    if isinstance(v,tuple) and len(v)%2==0:
        # tifffile exposes SRATIONAL/RATIONAL tags as flattened numerator,denominator pairs.
        den=[v[i+1] for i in range(0,len(v),2)]
        if all(float(x)!=0.0 for x in den):
            return np.asarray([float(v[i])/float(v[i+1]) for i in range(0,len(v),2)],np.float64)
    a=np.asarray(v).reshape(-1)
    out=[]
    for x in a:
        try: out.append(float(x))
        except (TypeError,ValueError): out.append(float(x[0])/float(x[1]))
    return np.asarray(out,np.float64)
def cct_from_xyz(xyz):
    xyz=np.asarray(xyz,np.float64); xyz=xyz/np.sum(xyz); x,y=xyz[0],xyz[1]; n=(x-.3320)/(.1858-y)
    return float(-449*n**3+3525*n**2-6823.3*n+5520.33)
def illum(code): return {17:2856.0,21:6504.0}.get(int(code))
def dng_transform(dng):
    with tifffile.TiffFile(dng) as tf:
        p=tf.pages[0]; tags=p.tags
        neutral=ratios(tags['AsShotNeutral'].value)
        cm1=ratios(tags['ColorMatrix1'].value).reshape(3,3); cm2=ratios(tags['ColorMatrix2'].value).reshape(3,3)
        fm1=ratios(tags['ForwardMatrix1'].value).reshape(3,3); fm2=ratios(tags['ForwardMatrix2'].value).reshape(3,3)
        t1=illum(tags['CalibrationIlluminant1'].value); t2=illum(tags['CalibrationIlluminant2'].value)
    if t1 is None or t2 is None: raise RuntimeError('unsupported CalibrationIlluminant for visualization')
    T=5500.0
    for _ in range(24):
        w=float(np.clip((1/T-1/t2)/(1/t1-1/t2),0,1)); cm=w*cm1+(1-w)*cm2; xyz=np.linalg.solve(cm,neutral); T=.5*T+.5*float(np.clip(cct_from_xyz(xyz),2000,12000))
    w=float(np.clip((1/T-1/t2)/(1/t1-1/t2),0,1)); fm=w*fm1+(1-w)*fm2
    xyz2srgb=np.array([[3.1338561,-1.6168667,-.4906146],[-.9787684,1.9161415,.0334540],[.0719453,-.2289914,1.4052427]],np.float64)
    return xyz2srgb@(fm@np.diag(1.0/neutral)),T,w,neutral
def gpu_load(path,w,h):
    q=np.fromfile(path,dtype='<f2');
    if q.size!=w*h*4: raise RuntimeError(f'GPU size mismatch got={q.size} expected={w*h*4}')
    return q.reshape(h,w,4)[...,:3].astype(np.float32)
def display(rgb,M,ev):
    x=np.einsum('ij,hwj->hwi',M,rgb.astype(np.float64),optimize=True).astype(np.float32); x=np.maximum(x*ev,0); x=x/(1+x)
    x=np.where(x<=.0031308,12.92*x,1.055*np.power(x,1/2.4)-.055); return np.clip(x,0,1)
def save(path,x,maxw=None):
    im=Image.fromarray(np.round(x*255).astype(np.uint8),'RGB')
    if maxw and im.width>maxw: im=im.resize((maxw,round(im.height*maxw/im.width)),Image.Resampling.LANCZOS)
    if str(path).lower().endswith('.jpg'): im.save(path,quality=95,subsampling=0)
    else: im.save(path)
def main():
    ap=argparse.ArgumentParser(); ap.add_argument('dng');ap.add_argument('cpu_npy');ap.add_argument('gpu_rgba16f');ap.add_argument('--out-dir',required=True);ap.add_argument('--crop',type=int,default=1024);a=ap.parse_args()
    out=Path(a.out_dir);out.mkdir(parents=True,exist_ok=True); cpu=np.load(a.cpu_npy).astype(np.float32);h,w,_=cpu.shape;gpu=gpu_load(a.gpu_rgba16f,w,h)
    M,T,mw,neutral=dng_transform(a.dng); lin=np.einsum('ij,hwj->hwi',M,cpu.astype(np.float64),optimize=True).astype(np.float32); p=float(np.quantile(np.maximum(lin,0).max(axis=2)[::4,::4],.995)); ev=.9/max(p,1e-6)
    dc=display(cpu,M,ev);dg=display(gpu,M,ev);stem=Path(a.dng).stem;c=min(a.crop,w,h);x0=(w-c)//2;y0=(h-c)//2
    save(out/f'{stem}_CPU_VNG4_DNG_COLOR.jpg',dc,2048);save(out/f'{stem}_VULKAN_VNG4_DNG_COLOR.jpg',dg,2048)
    save(out/f'{stem}_CPU_VNG4_center_{c}.png',dc[y0:y0+c,x0:x0+c]);save(out/f'{stem}_VULKAN_VNG4_center_{c}.png',dg[y0:y0+c,x0:x0+c])
    d=np.clip(np.abs(gpu-cpu)*128.0,0,1);save(out/f'{stem}_CPU_VULKAN_DIFF_x128.jpg',d,2048);save(out/f'{stem}_CPU_VULKAN_DIFF_center_{c}_x128.png',d[y0:y0+c,x0:x0+c])
    meta={'dng':str(Path(a.dng).resolve()),'width':w,'height':h,'estimated_cct_K':T,'forward_matrix_weight_1':mw,'as_shot_neutral':neutral.tolist(),'visual_exposure':ev,'diff_amplification':128.0,'crop':[x0,y0,c,c]}
    (out/f'{stem}_visual.json').write_text(json.dumps(meta,indent=2)+'\n');print('VNG4_DNG_VISUAL_PASS '+json.dumps(meta,separators=(',',':')))
if __name__=='__main__':main()
