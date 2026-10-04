#!/usr/bin/env python3
"""Replay a DNG through the debug app's production Renderer/JPEG_R path.

The device is the Vulkan backend. The input is an existing DNG, so no camera
capture or mutable camera preference is involved. Intermediate files stay in
the app's private dng_replay directory until copied to the requested output.
"""

import argparse
import hashlib
import json
import subprocess
import sys
import time
from pathlib import Path


PKG = "com.rawr.camera.debug"
ACTIVITY = f"{PKG}/com.rawr.camera.renderer.RendererDngReplayActivity"


def run(argv, *, capture=True, check=True):
    return subprocess.run(argv, stdout=subprocess.PIPE if capture else None,
                          stderr=subprocess.PIPE if capture else None, check=check)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("dng", type=Path)
    parser.add_argument("output", type=Path)
    parser.add_argument("--recipe", type=Path, help="JSON override; otherwise use the embedded RAWR recipe")
    parser.add_argument("--cfa", type=Path, help="normalized float32 Bayer override, before lens shading")
    parser.add_argument("--apk", type=Path, help="install this debug APK before replay")
    parser.add_argument("--adb", default="adb")
    parser.add_argument("--serial", help="ADB device serial")
    parser.add_argument("--diagnostics", action="store_true", help="copy production intermediate dumps")
    parser.add_argument("--timeout", type=int, default=300)
    args = parser.parse_args()
    if not args.dng.is_file():
        parser.error(f"DNG not found: {args.dng}")
    if args.recipe and not args.recipe.is_file():
        parser.error(f"recipe not found: {args.recipe}")
    if args.cfa and not args.cfa.is_file():
        parser.error(f"CFA override not found: {args.cfa}")
    if args.apk and not args.apk.is_file():
        parser.error(f"APK not found: {args.apk}")
    if args.timeout < 1:
        parser.error("timeout must be positive")
    args.output.mkdir(parents=True, exist_ok=True)
    adb = [args.adb] + (["-s", args.serial] if args.serial else [])

    def call(*parts, check=True):
        return run(adb + list(parts), check=check)

    if args.apk:
        print("Installing debug APK...", flush=True)
        call("install", "-r", str(args.apk))
    sha = hashlib.sha256(args.dng.read_bytes()).hexdigest()
    input_name = f"input-{sha[:16]}.dng"
    remote = f"/data/local/tmp/rawr-dng-replay-{sha[:16]}.dng"
    cfa_remote = None
    print(f"Staging {args.dng.name} sha256={sha}...", flush=True)
    call("push", str(args.dng), remote)
    try:
        call("shell", "run-as", PKG, "mkdir", "-p", "files/dng_replay")
        call("shell", "run-as", PKG, "cp", remote, f"files/dng_replay/{input_name}")
        call("shell", "run-as", PKG, "rm", "-f", "files/dng_replay/result.json",
             "files/dng_replay/result.json.tmp", "files/dng_replay/recipe.json")
        if args.recipe:
            recipe = json.loads(args.recipe.read_text())
            if recipe.get("version") != 1:
                raise ValueError("Only RAWR recipe version 1 is supported")
            recipe_remote = f"/data/local/tmp/rawr-dng-replay-{sha[:16]}.json"
            call("push", str(args.recipe), recipe_remote)
            call("shell", "run-as", PKG, "cp", recipe_remote, "files/dng_replay/recipe.json")
            call("shell", "rm", "-f", recipe_remote)
        cfa_name = None
        if args.cfa:
            cfa_sha = hashlib.sha256(args.cfa.read_bytes()).hexdigest()
            cfa_name = f"{cfa_sha[:16]}.f32"
            cfa_remote = f"/data/local/tmp/rawr-dng-replay-{cfa_sha[:16]}.f32"
            print(f"Staging CFA override sha256={cfa_sha}...", flush=True)
            call("push", str(args.cfa), cfa_remote)
            call("shell", "run-as", PKG, "cp", cfa_remote, f"files/dng_replay/{cfa_name}")
        launch = ["shell", "am", "start", "-W", "-n", ACTIVITY,
                  "--es", "inputName", input_name,
                  "--ez", "diagnostics", "true" if args.diagnostics else "false"]
        if cfa_name:
            launch += ["--es", "cfaName", cfa_name]
        call(*launch)
        deadline = time.monotonic() + args.timeout
        result = None
        while time.monotonic() < deadline:
            response = call("exec-out", "run-as", PKG, "cat", "files/dng_replay/result.json", check=False)
            if response.returncode == 0:
                try:
                    result = json.loads(response.stdout)
                    break
                except json.JSONDecodeError:
                    pass
            time.sleep(1)
        if result is None:
            raise TimeoutError(f"DNG replay timed out after {args.timeout}s")
        (args.output / "result.json").write_text(json.dumps(result, indent=2) + "\n")
        if not result.get("success"):
            raise RuntimeError(result.get("error", "DNG replay failed"))
        names = ["render.jpg"] + result.get("dumps", [])
        for name in names:
            response = call("exec-out", "run-as", PKG, "cat", f"files/dng_replay/{name}")
            (args.output / name).write_bytes(response.stdout)
        image = (args.output / "render.jpg").read_bytes()
        if not image.startswith(b"\xff\xd8") or len(image) != result["bytes"]:
            raise RuntimeError("Transferred JPEG failed SOI/length validation")
        (args.output / "input.sha256").write_text(f"{sha}  {args.dng.name}\n")
        print(json.dumps(result, indent=2), flush=True)
    finally:
        call("shell", "rm", "-f", remote, check=False)
        if cfa_remote:
            call("shell", "rm", "-f", cfa_remote, check=False)


if __name__ == "__main__":
    try:
        main()
    except (subprocess.CalledProcessError, RuntimeError, TimeoutError, ValueError) as error:
        if isinstance(error, subprocess.CalledProcessError):
            sys.stderr.write(error.stdout.decode(errors="replace") if error.stdout else "")
            sys.stderr.write(error.stderr.decode(errors="replace") if error.stderr else "")
        sys.exit(f"DNG replay: {error}")
