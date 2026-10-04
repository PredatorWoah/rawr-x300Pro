#!/usr/bin/env python3
"""Build and run the existing offline replay tools on the host Vulkan driver.

On macOS, install Vulkan/MoltenVK and expose its loader and ICD through your
Vulkan SDK environment. These are diagnostic stages, not yet one complete
app-equivalent replay pipeline. See docs/replay.md for current limitations.

Commands:
  dng INPUT.dng OUTPUT_DIR [DNG diagnostic options]
  burst --output-dir OUTPUT_DIR [merge options] INPUT.rzsl|DNG_DIRECTORY ...
  packed --input INPUT.rgba16f --config CONFIG --output-dir OUTPUT_DIR [options]
"""
from pathlib import Path
import os
import re
import shutil
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]
def main():
    if len(sys.argv) < 2 or sys.argv[1] in ('-h', '--help'):
        print(__doc__)
        return 0
    mode, args = sys.argv[1], sys.argv[2:]
    if mode not in ('dng', 'burst', 'packed'):
        raise ValueError('Choose dng, burst, or packed; use --help for usage')
    temp = ROOT/'tmp'
    temp.mkdir(exist_ok=True)
    env = dict(os.environ, TMPDIR=str(temp), UV_CACHE_DIR=str(temp/'uv-cache'),
               UV_PROJECT_ENVIRONMENT=str(temp/'venv'))
    sdk = env.get('ANDROID_HOME') or env.get('ANDROID_SDK_ROOT')
    properties = ROOT/'local.properties'
    if properties.exists():
        for line in properties.read_text().splitlines():
            if line.startswith('sdk.dir='): sdk = line.split('=', 1)[1]
    if sdk:
        gradle = (ROOT/'app/build.gradle.kts').read_text()
        ndk = re.search(r'ndkVersion = "([^"]+)"', gradle)[1]
        cmake = re.search(r'version = "([^"]+)"', gradle)[1]
        host = 'darwin-x86_64' if sys.platform == 'darwin' else 'linux-x86_64'
        paths = [Path(sdk)/'cmake'/cmake/'bin', Path(sdk)/'ndk'/ndk/'shader-tools'/host]
        env['PATH'] = os.pathsep.join(map(str, paths)) + os.pathsep + env['PATH']
    if not shutil.which('cmake', path=env['PATH']): raise ValueError('CMake is required')
    def run(command):
        subprocess.run(list(map(str, command)), cwd=ROOT, env=env, check=True)
    project = 'multiframe_moltenvk_replay' if mode == 'burst' else 'offline_rawr_pipeline'
    build = temp/project
    run(['cmake', '-S', ROOT/'tools'/project, '-B', build, '-G', 'Ninja', '-DCMAKE_BUILD_TYPE=Release'])
    targets = (['rawr_multiframe_moltenvk_replay'] if mode == 'burst' else
               ['rawr_offline_pipeline', 'rawr_dng_decode', 'rawr_uhdr_mux'])
    run(['cmake', '--build', build, '--parallel', '4', '--target', *targets])
    if mode == 'dng':
        run(['uv', 'run', '--no-project', '--with', 'numpy', '--with', 'pillow',
             ROOT/'tools/offline_rawr_pipeline/render_dng_moltenvk.py', *args,
             '--build-dir', build, '--skip-build'])
    else:
        run([build/targets[0], *args])
    return 0
if __name__ == '__main__':
    try:
        sys.exit(main())
    except (ValueError, subprocess.CalledProcessError) as error:
        print(f'Replay failed: {error}', file=sys.stderr)
        sys.exit(1)
