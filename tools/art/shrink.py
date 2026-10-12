#!/usr/bin/env python3
"""Turns a content pack's PNGs into 256-colour images, as the original's
sprites are, so the pack is small enough to keep in the repository.

    python3 tools/art/shrink.py <content pack folder>

A picture without see-through edges (a floor sheet) gets a 255-colour
palette and one transparent colour. Other pictures get 224 colours for the
opaque parts and 7 levels of black for shadows; the renderer's faint, noisy
shadow haze is smoothed into bands and the faintest of it dropped, which is
most of the saving. Needs numpy and Pillow; rewrites the files in place.
"""

import os
import sys
from concurrent.futures import ProcessPoolExecutor
from pathlib import Path

import numpy as np
from PIL import Image, ImageFilter

SHADOWS = 7  # shadow levels; index 0 is transparent
COLOURS = 256 - 1 - SHADOWS


def palette_of(pixels, count):
    """A median-cut palette of `count` colours for an array of RGB pixels."""
    if len(pixels) == 0:
        pixels = np.zeros((1, 3), np.uint8)
    if len(pixels) > 400000:
        pixels = pixels[np.random.default_rng(0).choice(len(pixels), 400000, replace=False)]
    pal = Image.fromarray(pixels.reshape(1, -1, 3)).quantize(count, method=Image.Quantize.MEDIANCUT,
                                                           dither=Image.Dither.NONE)
    colours = pal.getpalette()[:count * 3]
    return colours + [0] * (count * 3 - len(colours))


def nearest(im, colours):
    """Each pixel of an RGBA picture as an index into `colours`."""
    pal = Image.new("P", (1, 1))
    pal.putpalette(colours + [0] * (768 - len(colours)))
    return np.asarray(im.convert("RGB").quantize(palette=pal, dither=Image.Dither.NONE)).astype(int)


def shrink(path):
    im = Image.open(path).convert("RGBA")
    arr = np.asarray(im)
    a = arr[..., 3].astype(int)
    if not np.any((a > 0) & (a < 255)):
        colours = palette_of(arr[..., :3][a == 255], 255)
        out = np.where(a == 255, np.minimum(nearest(im, colours), 254), 255).astype(np.uint8)
        q = Image.fromarray(out, "P")
        q.putpalette(colours[:255 * 3] + [0, 0, 0])
        q.save(path, optimize=True, transparency=255)
        return os.path.getsize(path)
    shadow = (a > 0) & (a < 255) & (arr[..., :3].max(axis=2) < 48)
    solid = (a >= 128) & ~shadow
    colours = palette_of(arr[..., :3][solid], COLOURS)
    out = np.zeros(a.shape, np.uint8)
    out[solid] = (1 + SHADOWS + np.minimum(nearest(im, colours), COLOURS - 1)[solid]).astype(np.uint8)
    soft = Image.fromarray(np.where(shadow | solid, a, 0).astype(np.uint8)).filter(ImageFilter.BoxBlur(3))
    level = np.clip((np.asarray(soft).astype(int) * SHADOWS + 127) // 255, 0, SHADOWS)
    out[shadow] = level[shadow].astype(np.uint8)
    q = Image.fromarray(out, "P")
    q.putpalette([0, 0, 0] * (1 + SHADOWS) + colours)
    alpha = bytes([0] + [round(255 * k / SHADOWS) for k in range(1, SHADOWS + 1)] + [255] * COLOURS)
    q.save(path, optimize=True, transparency=alpha)
    return os.path.getsize(path)


def main():
    files = sorted(Path(sys.argv[1]).rglob("*.png"))
    files = [f for f in files if Image.open(f).mode != "P"]  # already shrunk
    with ProcessPoolExecutor(3) as pool:  # big character sheets take a lot of memory each
        total = sum(pool.map(shrink, files))
    print(f"shrank {len(files)} pictures to {total // 2 ** 20} MB")


if __name__ == "__main__":
    main()
