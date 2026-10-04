#!/usr/bin/env python3
"""Reject local/generated artifacts while preserving source assets and golden fixtures."""
from pathlib import Path
import re
import subprocess

ROOT = Path(__file__).resolve().parents[2]
files = subprocess.check_output(['git', 'ls-files', '-z'], cwd=ROOT).decode().split('\0')
# Deleted tracked files are allowed until the cleanup is staged.
files = [name for name in files if name and (ROOT / name).exists()]
forbidden = re.compile(
    r'(^|/)(?:tmp|\.venv|__pycache__|\.gradle|\.cxx|CMakeFiles|scope_review|release_logs)/'
    r'|^app/build/|^build/|^native/.*/build(?:[-_][^/]*)?/'
    r'|(^|/)(?:local\.properties|signing\.properties|CMakeCache\.txt|compile_commands\.json|\.DS_Store)$'
    r'|\.(?:apk|aab|dex|pyc|o|so|dylib|jks|keystore|logcat)$'
)
bad = [name for name in files if forbidden.search(name)]
assert not bad, 'Generated/local files are tracked:\n' + '\n'.join(bad)
probes = ['tmp/audit.txt', 'local.properties', 'signing.properties', 'release.jks',
          '.venv/pyvenv.cfg', 'app/build/outputs/apk/release/test.apk',
          'native/image_scopes/scope_review/generated.png',
          'native/raw_demosaic/rcd/build-android/build.ninja']
for name in probes:
    assert subprocess.run(['git', 'check-ignore', '-q', '--no-index', name], cwd=ROOT).returncode == 0, name
required = ['gradle/wrapper/gradle-wrapper.jar', 'native/image_scopes/fixtures/portrait_a.ppm',
            'tests/fixtures/capture_job_v1.job', 'native/multiframe/output/CMakeLists.txt',
            'native/spektrafilm/generated/SpektraGeneratedProfileCurves.cpp']
for name in required:
    assert (ROOT / name).is_file(), name
    assert subprocess.run(['git', 'check-ignore', '-q', '--no-index', name], cwd=ROOT).returncode == 1, name
# New artifacts should not silently escape ignore coverage after validation.
untracked = subprocess.check_output(['git', 'ls-files', '--others', '--exclude-standard', '-z'], cwd=ROOT).decode().split('\0')
assert not [name for name in untracked if name and forbidden.search(name)], 'Unignored generated/local files'
print('REPOSITORY_HYGIENE_PASS')
