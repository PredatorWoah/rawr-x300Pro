#!/usr/bin/env python3
import struct,subprocess,sys,tempfile
from pathlib import Path
root=Path(__file__).resolve().parents[1]
u=[0]*375;u[0]=0x564e4734
with tempfile.NamedTemporaryFile() as f:
    f.write(struct.pack("<375I",*u));f.flush()
    p=subprocess.run([sys.executable,str(root/"tools/decode_gpu_diagnostic.py"),f.name,"--x","0","--y","0"],text=True,capture_output=True)
    assert p.returncode==0,p.stderr
    assert "VNG4_GPU_TERM_TRACE " in p.stdout
    assert "VNG4_GPU_WORKING4_5X5 " in p.stdout
print("VNG4_EXTENDED_DIAGNOSTIC_375_PASS")
