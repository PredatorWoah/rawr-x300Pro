"""Summarize device-produced pixels. No CPU rendering oracle is used."""
import argparse, hashlib, json, shutil, subprocess
from pathlib import Path
import numpy as np
from PIL import Image, ImageDraw, ImageOps


def main():
    p=argparse.ArgumentParser();p.add_argument('runs',type=Path);p.add_argument('original',type=Path);p.add_argument('output',type=Path)
    args=p.parse_args();args.output.mkdir(parents=True,exist_ok=True)
    root=args.runs;box=(2140,1240,2550,1310)
    original=Image.open(args.original)
    orig=np.asarray(original).astype('i2')
    panels=[('Original JPEG',ImageOps.exif_transpose(original).crop(box))]
    metrics={}; refs={}
    variants=['baseline_ev1','passive','nodir','no_recovery','pure_rcd','nofcc','ca_fitted','ca_inverse',
              'dir20_fcc1','gamma24_fcc1','normalized_fcc','halation_on','normalized_halation','baseline_repeat']
    for name in variants:
        d=root/name;ppm=d/(name+'.ppm');jpeg=d/(name+'.jpg')
        if not ppm.exists() or not jpeg.exists():continue
        try: im=Image.open(ppm);im.load();j=Image.open(jpeg);j.load()
        except OSError:continue
        a=np.asarray(im).astype('i2');refs[name]=a
        crop=np.rot90(a,-1)[box[1]:box[3],box[0]:box[2]]
        red=(crop[...,0]>128)&(crop[...,1]<90)&((crop[...,0]-crop[...,1])>80)
        error=a-orig
        ja=np.asarray(j).astype('i2');jd=np.abs(a-ja)
        m=dict(lettering_red_pixels=int(red.sum()),trace_pixel_rgb=crop[43,212].tolist(),
               original_mae=float(np.abs(error).mean()),original_rmse=float(np.sqrt(np.square(error.astype('f4')).mean())),
               jpeg_bytes=jpeg.stat().st_size,encoder_mae=float(jd.mean()),encoder_max=int(jd.max()),
               jpeg_quantization_matches_original=j.quantization==original.quantization,
               ppm_sha256=hashlib.sha256(ppm.read_bytes()).hexdigest())
        tap=d/'source_linear_srgb.rgba32f'
        if tap.exists():
            s=np.fromfile(tap,dtype='<f4').reshape(410,70,4)[...,:3]
            m['negative_film_input_components']=int((s<0).sum());m['nonfinite_film_input_components']=int((~np.isfinite(s)).sum())
        metrics[name]=m
        out=args.output/name;out.mkdir(exist_ok=True)
        for source in [jpeg,d/'config.txt',d/'highlight_lab_manifest.txt']:
            if source.exists():shutil.copy2(source,out/source.name)
        # Preserve exact lossless RGB, with physical orientation applied for viewing.
        im.transpose(Image.Transpose.ROTATE_270).save(out/(name+'.png'))
        if name in ['baseline_ev1','nodir','nofcc','normalized_fcc','halation_on','normalized_halation']:
            panels.append((name,Image.fromarray(crop.astype('u1'))))
    if 'baseline_ev1' in refs:
        base=refs['baseline_ev1']
        for n in ('passive','baseline_repeat'):
            if n in refs:metrics[n]['max_difference_from_uninstrumented_baseline']=int(np.abs(refs[n]-base).max())
        if 'normalized_fcc' in refs:
            fixed=refs['normalized_fcc'];delta=np.abs(fixed-base)
            metrics['normalized_fcc']['baseline_mae']=float(delta.mean())
            metrics['normalized_fcc']['baseline_p99_absolute_difference']=float(np.percentile(delta,99))
            metrics['normalized_fcc']['jpeg_size_change_percent']=100*(metrics['normalized_fcc']['jpeg_bytes']/metrics['baseline_ev1']['jpeg_bytes']-1)
    def sheet(items,path):
        canvas=Image.new('RGB',(1230,len(items)*240),'#191919');draw=ImageDraw.Draw(canvas)
        for i,(label,im) in enumerate(items):
            draw.text((10,i*240+7),label,fill='white')
            canvas.paste(im.resize((1230,210),Image.Resampling.NEAREST),(0,i*240+26))
        canvas.save(path)
    sheet(panels,args.output/'comparison.png')
    compact=[panels[0]]+[v for v in panels if v[0] in ('baseline_ev1','normalized_fcc')]
    sheet(compact,args.output/'before_after.png')
    shutil.copy2(root/'source_manifest.json',args.output/'source_manifest.json')
    metrics['_definition']={'roi_display_xyxy':box,'red_pixel_threshold':'R>128, G<90, R-G>80 on pre-JPEG RGB8; diagnostic count, not a perceptual score',
                            'excluded':'baseline_ev0 branch diagnostic buffers and CA fit measurements are not production-parity evidence'}
    (args.output/'metrics.json').write_text(json.dumps(metrics,indent=2))
    print(json.dumps(metrics,indent=2))

if __name__=='__main__':main()
