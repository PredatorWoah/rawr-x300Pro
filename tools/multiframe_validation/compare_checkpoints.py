#!/usr/bin/env python3
"""Compare Vulkan multiframe float32 checkpoints against a pinned golden manifest."""

from __future__ import annotations

import argparse
import array
import json
import math
import pathlib
import sys


def load_f32(path: pathlib.Path) -> array.array:
    values = array.array("f")
    with path.open("rb") as stream:
        values.fromfile(stream, path.stat().st_size // values.itemsize)
    if sys.byteorder != "little":
        values.byteswap()
    return values


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("manifest", type=pathlib.Path)
    parser.add_argument("candidate", type=pathlib.Path)
    args = parser.parse_args()
    spec = json.loads(args.manifest.read_text(encoding="utf-8"))
    golden_root = args.manifest.parent
    failed = False
    for stage in spec["stages"]:
        golden = load_f32(golden_root / stage["file"])
        candidate = load_f32(args.candidate / stage["file"])
        if len(golden) != len(candidate) or not golden:
            print(f"FAIL {stage['name']}: element count {len(candidate)} != {len(golden)}")
            failed = True
            continue
        squared = 0.0
        maximum = 0.0
        finite = True
        for expected, actual in zip(golden, candidate):
            if not math.isfinite(expected) or not math.isfinite(actual):
                finite = False
                break
            error = abs(float(actual) - float(expected))
            maximum = max(maximum, error)
            squared += error * error
        rmse = math.sqrt(squared / len(golden)) if finite else math.inf
        passed = finite and maximum <= stage["absolute_tolerance"] and rmse <= stage["rmse_tolerance"]
        print(f"{'PASS' if passed else 'FAIL'} {stage['name']}: max={maximum:.9g} rmse={rmse:.9g}")
        failed |= not passed
    return 1 if failed else 0


if __name__ == "__main__":
    raise SystemExit(main())
