#!/usr/bin/env python3
import argparse, json, math
from pathlib import Path
import numpy as np
import tifffile
from PIL import Image

def ratios(v):
    a=np.asarray(v).reshape(-1)
    if len(a)%2==0 and all(float(a[i+1])!=0 for i in range(0,len(a),2)):
        return np.array([float(a[i])/float(a[i+1]) for i in range(0,len(a),2)],np.float64)
    return a.astype(np.float64)

def cct_from_xyz(xyz):
    xyz=np.asarray(xyz,np.float64); xyz=xyz/np.sum(xyz); x,y=xyz[0],xyz[1]
    n=(x-0.3320)/(0.1858-y)
    return float(-449*n**3 + 3525*n**2 - 6823.3*n + 5520.33)

def illuminant_temp(code):
    # DNG/EXIF LightSource codes used by these files.
    return {17:2856.0, 21:6504.0}.get(int(code))

def dng_transform(dng):
    with tifffile.TiffFile(dng) as tf:
        p=tf.pages[0]
        neutral=ratios(p.tags['AsShotNeutral'].value)
        cm1=ratios(p.tags['ColorMatrix1'].value).reshape(3,3)
        cm2=ratios(p.tags['ColorMatrix2'].value).reshape(3,3)
        fm1=ratios(p.tags['ForwardMatrix1'].value).reshape(3,3)
        fm2=ratios(p.tags['ForwardMatrix2'].value).reshape(3,3)
        t1=illuminant_temp(p.tags['CalibrationIlluminant1'].value)
        t2=illuminant_temp(p.tags['CalibrationIlluminant2'].value)
    if t1 is None or t2 is None: raise RuntimeError('unsupported CalibrationIlluminant for visualization')
    # Fixed-point DNG-style reciprocal-temperature interpolation using AsShotNeutral.
    T=5500.0
    for _ in range(24):
        w=(1.0/T-1.0/t2)/(1.0/t1-1.0/t2); w=float(np.clip(w,0.0,1.0))
        cm=w*cm1+(1.0-w)*cm2
        xyz=np.linalg.solve(cm,neutral)
        Tn=float(np.clip(cct_from_xyz(xyz),2000.0,12000.0))
        T=0.5*T+0.5*Tn
    w=(1.0/T-1.0/t2)/(1.0/t1-1.0/t2); w=float(np.clip(w,0.0,1.0))
    fm=w*fm1+(1.0-w)*fm2
    wb=np.diag(1.0/neutral)
    cam_to_xyz_d50=fm@wb
    # Bradford-adapted XYZ D50 -> linear sRGB/D65.
    xyz_d50_to_srgb=np.array([
        [ 3.1338561, -1.6168667, -0.4906146],
        [-0.9787684,  1.9161415,  0.0334540],
        [ 0.0719453, -0.2289914,  1.4052427],
    ],np.float64)
    return xyz_d50_to_srgb@cam_to_xyz_d50, T, w, neutral

def load_gpu(path,w,h):
    a=np.fromfile(path,dtype='<f2')
    if a.size!=w*h*4: raise RuntimeError(f'GPU size mismatch: {a.size}')
    return a.reshape(h,w,4)[...,:3].astype(np.float32)

def to_display(rgb, M, exposure):
    x=np.einsum('ij,hwj->hwi',M,rgb.astype(np.float64),optimize=True).astype(np.float32)
    x=np.maximum(x*exposure,0.0)
    # Neutral visualization shoulder, identical for CPU and GPU.
    x=x/(1.0+x)
    lo=x<=0.0031308
    x=np.where(lo,12.92*x,1.055*np.power(x,1.0/2.4)-0.055)
    return np.clip(x,0,1)

def save_rgb(path,x,max_width=None):
    im=Image.fromarray(np.round(x*255).astype(np.uint8),'RGB')
    if max_width and im.width>max_width:
        nh=round(im.height*max_width/im.width); im=im.resize((max_width,nh),Image.Resampling.LANCZOS)
    if str(path).lower().endswith('.jpg'): im.save(path,quality=95,subsampling=0)
    else: im.save(path)

def main():
    ap=argparse.ArgumentParser()
    ap.add_argument('dng'); ap.add_argument('cpu_npy'); ap.add_argument('gpu_rgba16f'); ap.add_argument('--out-dir',required=True); ap.add_argument('--crop',type=int,default=1024)
    a=ap.parse_args(); out=Path(a.out_dir); out.mkdir(parents=True,exist_ok=True)
    cpu=np.load(a.cpu_npy).astype(np.float32); h,w,_=cpu.shape; gpu=load_gpu(a.gpu_rgba16f,w,h)
    M,T,weight,neutral=dng_transform(a.dng)
    lin_cpu=np.einsum('ij,hwj->hwi',M,cpu.astype(np.float64),optimize=True).astype(np.float32)
    lum=np.maximum(lin_cpu,0).max(axis=2); p995=float(np.quantile(lum[::4,::4],0.995)); exposure=0.9/max(p995,1e-6)
    dcpu=to_display(cpu,M,exposure); dgpu=to_display(gpu,M,exposure)
    stem=Path(a.dng).stem
    save_rgb(out/f'{stem}_CPU_RCD_DNG_COLOR.jpg',dcpu,2048); save_rgb(out/f'{stem}_VULKAN_RCD_DNG_COLOR.jpg',dgpu,2048)
    c=min(a.crop,w,h); x0=(w-c)//2; y0=(h-c)//2
    save_rgb(out/f'{stem}_CPU_RCD_center_{c}.png',dcpu[y0:y0+c,x0:x0+c]); save_rgb(out/f'{stem}_VULKAN_RCD_center_{c}.png',dgpu[y0:y0+c,x0:x0+c])
    # Difference is computed in raw linear RCD RGB, not after tone/color rendering.
    diff=np.abs(gpu-cpu); amp=128.0
    dvis=np.clip(diff*amp,0,1)
    save_rgb(out/f'{stem}_CPU_VULKAN_DIFF_x128.jpg',dvis,2048); save_rgb(out/f'{stem}_CPU_VULKAN_DIFF_center_{c}_x128.png',dvis[y0:y0+c,x0:x0+c])
    meta={'dng':str(Path(a.dng).resolve()),'width':w,'height':h,'estimated_cct_K':T,'forward_matrix_weight_1':weight,'as_shot_neutral':neutral.tolist(),'visual_exposure':exposure,'diff_amplification':amp,'crop':[x0,y0,c,c]}
    (out/f'{stem}_visual.json').write_text(json.dumps(meta,indent=2)+'\n')
    print('RCD_DNG_VISUAL_PASS '+json.dumps(meta,separators=(',',':')))
if __name__=='__main__': main()
