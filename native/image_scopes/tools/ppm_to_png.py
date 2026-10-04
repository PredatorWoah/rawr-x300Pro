#!/usr/bin/env python3
import pathlib,struct,zlib,sys

def png(path,w,h,rgb):
 raw=b''.join(b'\x00'+rgb[y*w*3:(y+1)*w*3] for y in range(h))
 def ch(t,d):return struct.pack('>I',len(d))+t+d+struct.pack('>I',zlib.crc32(t+d)&0xffffffff)
 path.write_bytes(b'\x89PNG\r\n\x1a\n'+ch(b'IHDR',struct.pack('>IIBBBBB',w,h,8,2,0,0,0))+ch(b'IDAT',zlib.compress(raw,9))+ch(b'IEND',b''))
def read_ppm(p):
 with p.open('rb') as f:
  if f.readline().strip()!=b'P6':raise ValueError(p)
  line=f.readline()
  while line.startswith(b'#'):line=f.readline()
  w,h=map(int,line.split());mx=int(f.readline());
  if mx!=255:raise ValueError('maxval')
  return w,h,f.read(w*h*3)
root=pathlib.Path(sys.argv[1] if len(sys.argv)>1 else '.')
for p in root.glob('*.ppm'):
 w,h,rgb=read_ppm(p);png(p.with_suffix('.png'),w,h,rgb)
print(root)
