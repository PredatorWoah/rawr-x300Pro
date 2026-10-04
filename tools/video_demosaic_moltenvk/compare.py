#!/usr/bin/env python3
"""Create review images for the offline RAW video demosaic experiment."""

from pathlib import Path

import numpy as np
from PIL import Image, ImageDraw, ImageFont
import rawpy


ROOT = Path(__file__).resolve().parents[2]
OUT = ROOT / "tmp/video_demosaic"
RAW_WIDTH, RAW_HEIGHT = 4080, 3064
CROP_X, CROP_Y = 120, 452


def gpu_output(name: str, width: int, height: int) -> np.ndarray:
    return np.memmap(OUT / f"{name}.rgba16f", dtype=np.float16,
                     mode="r", shape=(height, width, 4))[:, :, :3].astype(np.float32)


def image_of(linear: np.ndarray, exposure: float) -> Image.Image:
    # Same exposure and display curve for all panels; this is a demosaic
    # comparison, not the final Rawr video tonemap.
    display = np.clip(linear / exposure, 0, 1) ** (1 / 2.2)
    return Image.fromarray(np.uint8(np.round(display * 255)), "RGB")


def panel(image: Image.Image, label: str, width: int, height: int) -> Image.Image:
    canvas = Image.new("RGB", (width, height + 48), "#171717")
    image.thumbnail((width, height), Image.Resampling.LANCZOS)
    canvas.paste(image, ((width - image.width) // 2,
                         48 + (height - image.height) // 2))
    draw = ImageDraw.Draw(canvas)
    font = ImageFont.truetype("/System/Library/Fonts/Supplemental/Arial.ttf", 24)
    draw.text((14, 10), label, fill="white", font=font)
    return canvas


def main() -> None:
    reference = np.load(OUT / "ahd_raw.npy", mmap_mode="r").astype(np.float32) / 65535
    mhc_4k = gpu_output("4k", 3840, 2160)
    reduced = gpu_output("1080_fused_mhc", 1920, 1080)
    # Use a common exposure picked from the source crop, keeping relative
    # brightness and color differences visible rather than normalizing each.
    exposure = float(np.percentile(reference[CROP_Y:CROP_Y + 2160,
                                           CROP_X:CROP_X + 3840, 1], 99.5))
    ref_crop = reference[CROP_Y:CROP_Y + 2160, CROP_X:CROP_X + 3840]
    image_of(reduced, exposure).save(OUT / "1080_overview.png")
    overview = Image.new("RGB", (1800, 438), "#111111")
    for index, (array, label) in enumerate([
        (ref_crop, "LibRaw AHD reference"),
        (mhc_4k, "GPU MHC 4K"),
        (reduced, "GPU fused MHC + area 1080p"),
    ]):
        overview.paste(panel(image_of(array, exposure), label, 600, 390),
                       (index * 600, 0))
    overview.save(OUT / "comparison_overview.png")

    # A common 600x600 sensor patch. 1080p covers it with 300x300 pixels;
    # nearest-neighbor enlargement exposes the sampling difference clearly.
    source_x, source_y, side = 1710, 1030, 600
    x, y = source_x - CROP_X, source_y - CROP_Y
    detail = Image.new("RGB", (1800, 648), "#111111")
    for index, (array, label, factor) in enumerate([
        (reference, "LibRaw AHD at 100%", 1),
        (mhc_4k, "GPU MHC 4K at 100%", 1),
        (reduced, "GPU 1080p enlarged 2x", 2),
    ]):
        patch_x = source_x if index == 0 else x // factor
        patch_y = source_y if index == 0 else y // factor
        patch_side = side // factor
        crop = array[patch_y:patch_y + patch_side,
                     patch_x:patch_x + patch_side]
        view = image_of(crop, exposure)
        if factor == 2:
            view = view.resize((side, side), Image.Resampling.NEAREST)
        detail.paste(panel(view, label, 600, 600), (index * 600, 0))
    detail.save(OUT / "comparison_detail.png")

    before = gpu_output("1080_before", 1920, 1080)
    strong = gpu_output("1080_prefilter_strong", 1920, 1080)
    balanced = gpu_output("1080_prefilter_balanced", 1920, 1080)
    # BOX makes a 2x2 area average of the AHD reference. This is a useful
    # downsampling target, though AHD and the video shader have different
    # demosaic behavior and neither is the final processed recording.
    reference_1080 = image_of(ref_crop, exposure).resize(
        (1920, 1080), Image.Resampling.BOX)
    source_x, source_y, side = 1710, 1030, 600
    x, y = (source_x - CROP_X) // 2, (source_y - CROP_Y) // 2
    compare_1080 = Image.new("RGB", (3000, 648), "#111111")
    for index, (view, label) in enumerate([
        (reference_1080.crop((x, y, x + side // 2, y + side // 2)),
         "AHD + area downscale"),
        (image_of(before[y:y + side // 2, x:x + side // 2], exposure),
         "Bayer reduction before"),
        (image_of(strong[y:y + side // 2, x:x + side // 2], exposure),
         "Prefilter, stronger luma"),
        (image_of(balanced[y:y + side // 2, x:x + side // 2], exposure),
         "Prefilter, balanced luma"),
        (image_of(reduced[y:y + side // 2, x:x + side // 2], exposure),
         "Fused MHC + area 1080p"),
    ]):
        compare_1080.paste(panel(view.resize((600, 600), Image.Resampling.NEAREST),
                                 label, 600, 600), (index * 600, 0))
    compare_1080.save(OUT / "1080_before_after.png")

    # Independent CPU evaluation of the same 5x5 equations on a 256px patch.
    # This catches CFA phase, black-level, stride, and push-constant mistakes.
    raw = np.memmap(OUT / "sample.raw16", dtype="<u2", mode="r",
                    shape=(RAW_HEIGHT, RAW_WIDTH)).astype(np.float32)
    black, white = 1024.0, 8712.0
    raw = np.clip((raw - black) / (white - black), 0, 1)
    sy, sx, size = 1200, 1800, 256

    def tap(dx: int, dy: int) -> np.ndarray:
        return raw[sy + dy:sy + dy + size, sx + dx:sx + dx + size]

    c = tap(0, 0)
    h1, v1 = tap(-1, 0) + tap(1, 0), tap(0, -1) + tap(0, 1)
    h2, v2 = tap(-2, 0) + tap(2, 0), tap(0, -2) + tap(0, 2)
    diagonals = tap(-1, -1) + tap(1, -1) + tap(-1, 1) + tap(1, 1)
    green = (4*c + 2*(h1 + v1) - (h2 + v2)) / 8
    opposite = (6*c + 2*diagonals - 1.5*(h2 + v2)) / 8
    horizontal = (5*c + 4*h1 - diagonals - h2 + .5*v2) / 8
    vertical = (5*c + 4*v1 - diagonals - v2 + .5*h2) / 8
    yy, xx = np.indices((size, size))
    rr = ((yy + sy) % 2 == 0) & ((xx + sx) % 2 == 0)
    bb = ((yy + sy) % 2 == 1) & ((xx + sx) % 2 == 1)
    gr = ((yy + sy) % 2 == 0) & ((xx + sx) % 2 == 1)
    expected = np.empty((size, size, 3), dtype=np.float32)
    expected[:, :, 0] = np.where(rr, c, np.where(bb, opposite,
                                                  np.where(gr, horizontal, vertical)))
    expected[:, :, 1] = np.where(rr | bb, green, c)
    expected[:, :, 2] = np.where(bb, c, np.where(rr, opposite,
                                                  np.where(gr, vertical, horizontal)))
    expected = np.maximum(expected, 0) * np.array([2.2260857, 1, 1.6897687])
    actual = mhc_4k[sy - CROP_Y:sy - CROP_Y + size,
                    sx - CROP_X:sx - CROP_X + size]
    difference = np.abs(actual - expected)
    (OUT / "parity.txt").write_text(
        f"Independent CPU MHC comparison, source x={sx} y={sy} {size}x{size}\n"
        f"Mean absolute error: {difference.mean():.8f}\n"
        f"99th percentile absolute error: {np.percentile(difference, 99):.8f}\n"
        f"Max absolute error: {difference.max():.8f}\n"
        "GPU output is RGBA16F; small differences are expected from float16 rounding.\n")
    print((OUT / "parity.txt").read_text())

    with rawpy.imread(str(ROOT / "tmp/device_dng/RAWR_20260926_000643_129.dng")) as dng:
        flower_reference = dng.postprocess(
            demosaic_algorithm=rawpy.DemosaicAlgorithm.AHD,
            output_color=rawpy.ColorSpace.raw, gamma=(1, 1),
            no_auto_bright=True, use_camera_wb=True, output_bps=16,
            user_flip=0).astype(np.float32) / 65535
    flower_4k = gpu_output("flowers_4k", 3840, 2160)
    flower_open = gpu_output("flowers_open_gate", 4080, 3064)
    flower_crop = flower_reference[CROP_Y:CROP_Y + 2160,
                                   CROP_X:CROP_X + 3840]
    flower_open_crop = flower_open[CROP_Y:CROP_Y + 2160,
                                   CROP_X:CROP_X + 3840]
    flower_exposure = float(np.percentile(flower_crop[:, :, 1], 99.5))
    flowers = Image.new("RGB", (1800, 438), "#111111")
    for index, (array, label) in enumerate([
        (flower_crop, "LibRaw AHD reference"),
        (flower_4k, "GPU MHC 4K crop"),
        (flower_open_crop, "GPU MHC Open Gate crop"),
    ]):
        flowers.paste(panel(image_of(array, flower_exposure), label, 600, 390),
                      (index * 600, 0))
    flowers.save(OUT / "flowers_comparison.png")

    flower_1080 = gpu_output("flowers_1080", 1920, 1080)
    flower_reference_1080 = image_of(flower_crop, flower_exposure).resize(
        (1920, 1080), Image.Resampling.BOX)
    flowers_reduced = Image.new("RGB", (1200, 438), "#111111")
    for index, (view, label) in enumerate([
        (flower_reference_1080, "AHD + area downscale"),
        (image_of(flower_1080, flower_exposure), "Fused MHC + area 1080p"),
    ]):
        flowers_reduced.paste(panel(view, label, 600, 390), (index * 600, 0))
    flowers_reduced.save(OUT / "flowers_1080.png")


if __name__ == "__main__":
    main()
