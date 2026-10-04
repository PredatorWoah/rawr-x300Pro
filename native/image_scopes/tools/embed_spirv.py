#!/usr/bin/env python3
import pathlib, struct, sys
out=pathlib.Path(sys.argv[1]); files=[pathlib.Path(x) for x in sys.argv[2:]]
with out.open('w') as f:
    f.write('#pragma once\n#include <cstddef>\n#include <cstdint>\nnamespace image_scopes::generated {\n')
    for p in files:
        data=p.read_bytes()
        if len(data)%4: raise SystemExit(f'{p}: SPIR-V byte size not divisible by 4')
        words=struct.unpack('<%dI'%(len(data)//4),data)
        name=p.stem.replace('.','_')
        f.write(f'inline constexpr std::uint32_t {name}[] = {{')
        for i,w in enumerate(words):
            if i%8==0:f.write('\n ')
            f.write(f'0x{w:08x}u,')
        f.write('\n};\n')
        f.write(f'inline constexpr std::size_t {name}_bytes = sizeof({name});\n')
    f.write('}\n')
