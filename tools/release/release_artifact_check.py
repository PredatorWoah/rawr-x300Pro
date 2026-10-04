#!/usr/bin/env python3
"""Inspect an unsigned or signed release APK without requiring a phone."""
from pathlib import Path
import os
import json
import struct
import subprocess
import sys
import zipfile

apk = Path(sys.argv[1]).resolve()
sdk = os.environ.get('ANDROID_HOME') or os.environ.get('ANDROID_SDK_ROOT')
root = Path(__file__).resolve().parents[2]
local = root / 'local.properties'
if local.exists():
    for line in local.read_text().splitlines():
        if line.startswith('sdk.dir='):
            sdk = line.removeprefix('sdk.dir=')
assert sdk, 'Set ANDROID_HOME'
aapt = Path(sdk) / 'build-tools/36.0.0/aapt2'
badging = subprocess.check_output([str(aapt), 'dump', 'badging', str(apk)], text=True)
assert 'application-debuggable' not in badging, 'Debuggable release'
assert "package: name='com.rawr.camera'" in badging, 'Unexpected release package'
with zipfile.ZipFile(apk) as archive:
    names = archive.namelist()
    assert 'lib/arm64-v8a/librawrcam_native.so' in names
    for name in names:
        if not name.endswith('.so'):
            continue
        elf = archive.read(name)
        assert elf[:6] == b'\x7fELF\x02\x01', f'Expected little-endian ELF64: {name}'
        offset = struct.unpack_from('<Q', elf, 32)[0]
        entry_size, count = struct.unpack_from('<HH', elf, 54)
        for index in range(count):
            header = offset + index * entry_size
            kind = struct.unpack_from('<I', elf, header)[0]
            if kind == 1:  # PT_LOAD
                alignment = struct.unpack_from('<Q', elf, header + 48)[0]
                assert alignment >= 16384, f'ELF segment is not 16 KiB aligned: {name}'
    assert all(name.startswith('lib/arm64-v8a/') for name in names if name.startswith('lib/') and name.endswith('.so'))
    assert all(item.compress_type == zipfile.ZIP_STORED for item in archive.infolist() if item.filename.endswith('.f32'))
    dex = sum(item.file_size for item in archive.infolist() if item.filename.endswith('.dex'))
    budget = json.loads((Path(__file__).with_name('apk_size_budget.json')).read_text())['release']
    assert apk.stat().st_size <= budget['maxApkBytes'], 'Release APK size budget exceeded'
    assert dex <= budget['maxDexBytes'], 'Release DEX size budget exceeded'
    debug = root / 'app/build/outputs/apk/debug/app-debug.apk'
    if debug.exists():
        debug_budget = json.loads(Path(__file__).with_name('apk_size_budget.json').read_text())['debug']
        assert debug.stat().st_size <= debug_budget['maxApkBytes'], 'Debug APK size budget exceeded'
    print(f'APK_BYTES={apk.stat().st_size} DEX_BYTES={dex}')
subprocess.run([str(Path(sdk) / 'build-tools/36.0.0/zipalign'), '-c', '-P', '16', '4', str(apk)], check=True, stdout=subprocess.DEVNULL)
print('RELEASE_ARTIFACT_PASS')
