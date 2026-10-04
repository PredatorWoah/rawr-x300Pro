"""Prepare tagged CFA and explicit film config; run with uv --with numpy --with tifffile --with pillow.
Current preferences are evidence of current state, never proof of historical settings.
"""
import argparse, hashlib, json, re, struct, subprocess
from pathlib import Path
import numpy as np
from PIL import Image
from tifffile import TiffFile


def wire(data):
    pos = 0
    def varint():
        nonlocal pos
        value = shift = 0
        while True:
            b = data[pos]; pos += 1
            value |= (b & 127) << shift
            if b < 128: return value
            shift += 7
    while pos < len(data):
        tag = varint(); kind = tag & 7
        if kind == 0: value = varint()
        elif kind == 2:
            size = varint(); value = data[pos:pos+size]; pos += size
        elif kind == 5: value = struct.unpack_from('<f', data, pos)[0]; pos += 4
        elif kind == 1: value = struct.unpack_from('<d', data, pos)[0]; pos += 8
        else: raise ValueError(f'Unsupported protobuf wire type {kind}')
        yield tag >> 3, value


def preferences(path):
    out = {}
    for _, entry in wire(path.read_bytes()):
        item = dict(wire(entry)); value = list(wire(item[2]))[0][1]
        out[item[1].decode()] = value.decode() if isinstance(value, bytes) else value
    return out


def rational(tag):
    a = np.asarray(tag.value)
    if tag.dtype in (5, 10): a = a.reshape(-1, 2); a = a[:, 0] / a[:, 1]
    return np.atleast_1d(a).astype(float)


def main():
    p = argparse.ArgumentParser()
    p.add_argument('dng', type=Path); p.add_argument('jpeg', type=Path)
    p.add_argument('--preferences', type=Path, required=True)
    p.add_argument('--calibration', type=Path, required=True)
    p.add_argument('--output', type=Path, required=True)
    args = p.parse_args(); args.output.mkdir(parents=True, exist_ok=True)
    repo = Path(__file__).resolve().parents[2]
    prefs = preferences(args.preferences)
    (args.output/'preferences.json').write_text(json.dumps(prefs, indent=2))
    model = (repo/'app/src/main/kotlin/com/rawr/camera/settings/model/FilmSimModels.kt').read_text()
    keys = []
    for typ in ('Float','Int'):
        block = re.search(typ.lower()+r'ArrayOf\((.*?)\n    \)', model, re.S).group(1)
        block = re.sub(r'if \((\w+)\) 1 else 0', r'\1', block)
        keys.append(re.findall(r'\b[a-zA-Z]\w*\b', block))
    film = {}
    for names, vals in zip(keys, prefs['film_sim_look'].split('|')):
        values = vals.split(',')
        if len(names) != len(values): raise ValueError((len(names), len(values)))
        film.update(zip(names, values))
    # Verified sample metadata overrides current preferences. Other values remain assumptions.
    film.update(film='14', paper='5', rgbToRawMethod='2', process='0',
                filmExposureEv='0', printExposureEv='0', grainEnabled='0', dirCouplersAmount='0.88')
    with TiffFile(args.dng) as tif:
        page = tif.pages[0]; tags = page.tags
        raw = page.asarray().astype(np.float32); height, width = raw.shape
        pattern = list(tags['CFAPattern'].value)
        cfa = {(0,1,1,2):0,(1,0,2,1):1,(1,2,0,1):2,(2,1,1,0):3}[tuple(pattern)]
        black = rational(tags['BlackLevel']); white = rational(tags['WhiteLevel'])[0]
        if len(black) == 1: black = np.repeat(black, 4)
        positions = [(i//2,i%2) for color in (0,1,2) for i,c in enumerate(pattern) if c == color]
        packed = np.empty((height//2,width//2,4), dtype=np.float32)
        for c,(y,x) in enumerate(positions):
            b = black[y*2+x]
            packed[...,c] = np.clip((raw[y::2,x::2]-b)/(white-b),0,1)
        clipped = packed >= .995
        half = packed.astype('<f2'); bits = half.view('<u2'); bits[clipped] |= 0x8000
        bits.tofile(args.output/'input.rgba16f')
        neutral = rational(tags['AsShotNeutral']); wb = 1/neutral; wb /= wb[1]
        wb4 = wb[[0,1,1,2]]
        vals = [rational(tags[n]) for n in ('CalibrationIlluminant1','CalibrationIlluminant2',
                'ColorMatrix1','ColorMatrix2','CameraCalibration1','CameraCalibration2','ForwardMatrix1','ForwardMatrix2')]
        data = ' '.join(str(int(a[0])) for a in vals[:2]) + ' ' + ' '.join(str(v) for a in vals[2:]+[neutral,wb4] for v in a)
        result = subprocess.run([str(args.calibration)],input=data,text=True,capture_output=True,check=True).stdout.splitlines()
        matrix = result[0]
    config = dict(source=args.dng.stem,width=width,height=height,cfa=cfa,
                  wbRgb=','.join(map(str,wb)),wbRggb=','.join(map(str,wb4)),
                  sensorToLinearSrgb=matrix,cameraToWorkingColumnMajor='1,0,0,0,1,0,0,0,1',
                  fccSteps=prefs.get('fcc_steps',1), dualAutoContrast=1,dualContrastPercent=20,
                  filmEnabled=1,recovery=1,diagnostics=1,jpegQuality=98,
                  aePostGain=1,replayName='baseline_ev0',distortionEnabled=0)
    config.update({'film.'+k:v for k,v in film.items()})
    (args.output/'input.rgba16f.txt').write_text(''.join(f'{k}={v}\n' for k,v in config.items()))
    original = Image.open(args.jpeg)
    (args.output/'source_quantization.json').write_text(json.dumps(original.quantization))
    info = dict(source_sha256={str(p):hashlib.sha256(p.read_bytes()).hexdigest() for p in (args.dng,args.jpeg)},
                width=width,height=height,cfa=cfa,black=black.tolist(),white=white,wb=wb.tolist(),
                clipped_fraction=float(clipped.mean()),matrix_source=result[1],matrix=matrix,
                assumptions=['Unrecorded film parameters taken from current preferences',
                             'FCC taken from current preferences; reconstruction enabled pending A/B',
                             'Distortion disabled: coefficients absent in DNG',
                             'Internal exposure gain unknown; compare EV 0, 0.5, 1',
                             'Paired sensor timestamp confirms pairing, not multiframe input identity'],film=film)
    (args.output/'source_manifest.json').write_text(json.dumps(info,indent=2))
    print(json.dumps(info,indent=2))

if __name__ == '__main__': main()
