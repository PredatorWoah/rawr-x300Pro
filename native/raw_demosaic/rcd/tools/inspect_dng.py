#!/usr/bin/env python3
import sys
from pathlib import Path
import tifffile
for arg in sys.argv[1:]:
    p=Path(arg); tf=tifffile.TiffFile(p); page=tf.pages[0]
    def tag(name,default=None):
        x=page.tags.get(name); return x.value if x else default
    print(f'{p.name}: {page.imagewidth}x{page.imagelength} bits={tag("BitsPerSample")} CFA={tag("CFAPattern")} black={tag("BlackLevel")} white={tag("WhiteLevel")}')
