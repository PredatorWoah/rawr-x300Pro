#!/usr/bin/env python3
import struct, subprocess, sys, tempfile
from pathlib import Path
root=Path(__file__).resolve().parents[1]
decoder=root/"tools/decode_gpu_diagnostic.py"
def make(words):
    u=[0]*words
    u[0]=0x564E4734; u[1]=0
    for i in range(2,13): u[i]=struct.unpack("<I",struct.pack("<f",float(i)))[0]
    u[13]=13;u[14]=3
    for i in range(15,19):u[i]=struct.unpack("<I",struct.pack("<f",float(i)))[0]
    if words==147:
        nan=0x7fc00000
        for i in range(19,83):u[i]=nan
        for i in range(83,147):u[i]=struct.unpack("<I",struct.pack("<f",float(i)))[0]
    return struct.pack(f"<{words}I",*u)
for words in (19,147):
    with tempfile.NamedTemporaryFile() as f:
        f.write(make(words));f.flush()
        p=subprocess.run([sys.executable,str(decoder),f.name,"--x","1","--y","2"],text=True,capture_output=True)
        assert p.returncode==0,(words,p.stdout,p.stderr)
        assert "VNG4_GPU_PIXEL_DIAGNOSTIC " in p.stdout
        assert ("VNG4_GPU_TERM_TRACE " in p.stdout)==(words==147)
print("VNG4_DIAGNOSTIC_DECODER_REGRESSION_PASS legacy_words=19 extended_words=147")
