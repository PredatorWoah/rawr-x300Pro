"""Exercise real HEVC/MP4 metadata edits and preserve decoded frames."""
from pathlib import Path
import json
import re
import subprocess
import sys
import tempfile

hevc, patch, ffmpeg, ffprobe, build = sys.argv[1:]
def run(*args):
    try:
        return subprocess.check_output([str(x) for x in args], stderr=subprocess.PIPE)
    except subprocess.CalledProcessError as error:
        sys.stderr.buffer.write(error.stderr)
        raise
def encode(source, target, *args):
    run(ffmpeg, '-y', '-v', 'error', '-f', 'lavfi', '-i', source, *args, target)
def decode(source, target):
    run(ffmpeg, '-y', '-v', 'error', '-i', source, '-fps_mode', 'passthrough', '-f', 'rawvideo', target)
def tags(file):
    return json.loads(run(ffprobe, '-v', 'error', '-show_format', '-show_streams', '-of', 'json', file))

with tempfile.TemporaryDirectory(prefix='video-', dir=build) as directory:
    root = Path(directory)
    for mode in ('tagged', 'untagged', 'reordered'):
        options = 'log-level=error:keyint=5:repeat-headers=1:pools=2'
        if mode != 'reordered': options += ':bframes=0'
        if mode != 'untagged': options += ':colorprim=bt709:transfer=bt709:colormatrix=bt709:range=limited'
        source, corrected = root/f'{mode}.hevc', root/f'{mode}-log.hevc'
        encode('testsrc2=size=128x96:rate=10:duration=1', source,
               '-pix_fmt', 'yuv420p10le', '-c:v', 'libx265', '-x265-params', options, '-f', 'hevc')
        run(hevc, source, corrected)
        decode(source, root/'before.yuv'); decode(corrected, root/'after.yuv')
        assert (root/'before.yuv').read_bytes() == (root/'after.yuv').read_bytes(), mode
        if mode == 'reordered': continue
        for placement in ('tail', 'front'):
            file = root/f'{mode}-{placement}.mp4'
            run(ffmpeg, '-y', '-v', 'error', '-r', '10', '-i', corrected, '-c', 'copy',
                '-color_primaries', 'bt709', '-color_trc', 'bt709', '-colorspace', 'bt709',
                '-movflags', '+faststart' if placement == 'front' else '0', file)
            run(patch, file, '--log'); run(patch, file, '--log')
            stream = tags(file)['streams'][0]
            assert stream.get('color_space') == 'bt709' and stream.get('color_range') == 'tv'
            assert stream.get('color_transfer', 'unknown') == 'unknown'
            assert stream.get('color_primaries', 'unknown') == 'unknown'
            assert b'colrnclx\x00\x02\x00\x02\x00\x01\x00' in file.read_bytes()
            result = subprocess.run([ffmpeg, '-v', 'verbose', '-i', str(file), '-map', '0:v:0',
                                     '-c', 'copy', '-bsf:v', 'trace_headers', '-f', 'null', '-'],
                                    check=True, capture_output=True, text=True)
            for key, expected in [('colour_primaries', 2), ('transfer_characteristics', 2),
                                  ('matrix_coefficients', 1), ('video_full_range_flag', 0)]:
                values = re.findall(r'\b'+key+r'\s+[01]+\s+=\s+(\d+)', result.stderr)
                assert values and all(int(value) == expected for value in values), key
            decode(file, root/'final.yuv')
            assert (root/'before.yuv').read_bytes() == (root/'final.yuv').read_bytes()
    for placement in ('tail', 'front', 'mdta'):
        file = root/f'metadata-{placement}.mp4'
        encode('testsrc=size=320x240:rate=30:duration=1', file,
               '-f', 'lavfi', '-i', 'sine=frequency=440:duration=1', '-c:v', 'libx265',
               '-x265-params', 'log-level=error:pools=2', '-c:a', 'aac', '-shortest',
               '-movflags', '+faststart' if placement == 'front' else '0')
        if placement == 'mdta': run(patch, file, 'OldMake', '')
        before = tags(file)['streams'][0]['nb_frames']
        for _ in range(2):
            run(patch, file)
            actual = tags(file)
            expected = {'make':'vivo', 'model':'V2408A', 'encoder':'RAWR test',
                        'com.android.manufacturer':'vivo', 'com.android.model':'V2408A',
                        'com.rawr.render-profile':'ARRI LogC3 EI800',
                        'com.rawr.color-gamut':'ARRI Wide Gamut 3',
                        'com.rawr.transfer':'LogC3', 'com.rawr.bit-depth':'10'}
            assert expected.items() <= actual['format']['tags'].items()
            assert actual['streams'][0]['nb_frames'] == before
        run(ffmpeg, '-v', 'error', '-i', file, '-f', 'null', '-')
print('Video metadata and decoded-frame checks passed')
