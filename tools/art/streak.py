#!/usr/bin/env python3
"""The companions' speed streak for the open content pack.

    python3 tools/art/streak.py <content pack folder>

The original's run chart (Character/PARTNER/*/Animation.Caf, chart 2) has
a third layer: a long grey streak drawn additively over the running dog,
which the engine shows only while a dash is going flat out (docs/partners.md).
This paints our own streak for each of our companions from noise: a
teardrop of light along the dog's run, brightest at its head and trailing
behind in streaks, flickering from frame to frame. Its direction, length
and place come from our own dog's run frames, so it lines up with our art.

It adds the frames to each partner folder's sheet and a `<d>.2@-1*` line
(layer 2, in front of the body, added to the screen) to chart 2 of its
.anim, alternating 800 and 700 opacity as the original does. A folder that
already has the streak is left alone. build_pack.py calls this for the
folders it writes; run by hand on a pack that is already shrunk, it gives
the sheet's 16 least used colours over to greys. Needs numpy and Pillow.
"""

import math
import re
import sys
from pathlib import Path

import numpy as np
from PIL import Image

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
from effects import DIRS, noise  # noqa: E402

LAYER = 2
PRIORITY = -1  # in front of the body (priority 0); lower is nearer
OPACITY = (800, 700)  # every other frame, as the original
LENGTH = 2.6  # times the body's length along the run
WIDTH = 0.8  # times the body's length, at least MIN_WIDTH
MIN_WIDTH = 40
BEHIND = 0.35  # the streak's middle sits this far (body lengths) behind the body's
RAISE = 6  # pixels above the body's middle
PEAK = 255  # brightest grey; it is added to the screen


def read_sheet(path):
    lines = path.read_text().split("\n")
    frames = [tuple(map(int, l[6:].split(",")[:6])) for l in lines if l.startswith("frame=")]
    image = next(l[6:] for l in lines if l.startswith("image="))
    return lines, frames, image


def chart_lines(anim, chart):
    """The .anim's lines of one chart, as {key: value}, and where it ends."""
    lines = anim.split("\n")
    start = lines.index(next(l for l in lines if re.match(rf"\[chart {chart} ", l)))
    end = next((i for i in range(start + 1, len(lines)) if lines[i].startswith("[")), len(lines))
    while end > start + 1 and not lines[end - 1].strip():
        end -= 1
    keys = dict(l.split("=", 1) for l in lines[start + 1:end] if "=" in l)
    return lines, keys, end


def body_axis(sheet_image, frame):
    """Where the body is (from the anchor), the unit vector along it and its length."""
    x, y, w, h, ax, ay = frame
    a = np.asarray(sheet_image.crop((x, y, x + w, y + h)).convert("RGBA"), np.float32)
    # The body's opaque pixels; the soft black shadow is left out.
    m = (a[..., 3] > 200) & (a[..., :3].max(axis=2) > 24)
    ys, xs = np.nonzero(m)
    cx, cy = xs.mean() - ax, ys.mean() - ay
    cov = np.cov(np.vstack([xs, ys]).astype(float))
    ev, vec = np.linalg.eigh(cov)
    ux, uy = vec[0, 1], vec[1, 1]
    return cx, cy, ux, uy, 4 * math.sqrt(ev[1])


def paint(length, width, ux, uy, frame, seed):
    """A streak `length` long heading along (ux, uy); returns the picture
    and its middle's place in it."""
    span = int(length * abs(ux) + width * abs(uy)) + 8, int(length * abs(uy) + width * abs(ux)) + 8
    w, h = span
    ys, xs = np.mgrid[0:h, 0:w].astype(np.float32)
    px, py = xs - w / 2, ys - h / 2
    s = (px * ux + py * uy) / (length / 2)  # -1 tail .. 1 head
    t = (-px * uy + py * ux) / (width / 2)  # across, -1 .. 1
    # A teardrop: blunt at the head, drawn out to a point at the tail.
    u = np.clip((s + 1) / 2, 0, 1)
    half = np.where(s < 1, np.power(u, 0.6) * np.power(np.clip(1 - u, 0, 1), 0.25) * 1.45, 0)
    across = t / np.maximum(half, 1e-3)  # -1 .. 1 within the teardrop
    inside = np.clip(1 - across ** 2, 0, 1) ** 0.5 * (np.abs(s) < 1) * (np.abs(across) < 1)
    # Lines of light along the run, closing in to the tail: noise read
    # across the teardrop, changing only slowly along it and drifting each
    # frame so the streak flickers.
    n = noise(256, 64, 3, seed, 2)
    nu = np.clip(((u * 0.18 + frame * 0.07) % 1.0) * 255, 0, 255).astype(int)
    nt = np.clip((across + 1) / 2 * 63, 0, 63).astype(int)
    lines = np.clip((n[nt, nu] - 0.38) * 3.0, 0, 1)
    # A brighter rim, and most light at the head.
    rim = np.exp(-((np.abs(across) - 0.85) / 0.12) ** 2)
    glow = (0.15 + 0.85 * u ** 1.8) * (1 - np.clip((u - 0.92) / 0.08, 0, 1) * 0.4)
    v = inside * glow * (0.3 + 0.75 * lines + 0.5 * rim)
    v = np.clip(v, 0, 1) * PEAK
    a = np.where(v > 6, 255, 0).astype(np.uint8)
    g = v.astype(np.uint8)
    im = Image.fromarray(np.dstack([g, g, g, a]), "RGBA")
    box = im.getbbox() or (0, 0, 1, 1)
    return im.crop(box), w / 2 - box[0], h / 2 - box[1]


GREYS = 16  # palette slots a shrunk sheet gives up for the streak


def grey_slots(sheet):
    """Frees the GREYS least used opaque colours of a shrunk sheet (their
    pixels take the nearest colour left) and makes them a grey ramp.
    Returns the new pixels, palette and the grey slots' indices."""
    pixels = np.asarray(sheet).copy()
    pal = np.array(sheet.getpalette()[:768], np.int32).reshape(-1, 3)
    pal = np.vstack([pal, np.zeros((256 - len(pal), 3), np.int32)])
    trns = sheet.info.get("transparency")
    alpha = np.full(256, 255)
    if isinstance(trns, bytes):
        alpha[:len(trns)] = list(trns)
    elif isinstance(trns, int):
        alpha[trns] = 0
    opaque = np.nonzero(alpha == 255)[0]
    counts = np.bincount(pixels.ravel(), minlength=256)
    freed = opaque[np.argsort(counts[opaque], kind="stable")[:GREYS]]
    kept = np.setdiff1d(opaque, freed)
    for i in freed:
        if counts[i]:
            pixels[pixels == i] = kept[((pal[kept] - pal[i]) ** 2).sum(axis=1).argmin()]
    for k, i in enumerate(freed):
        pal[i] = round(255 * (k + 1) / GREYS)
    clear = int(np.nonzero(alpha == 0)[0][0])
    return pixels, pal, freed, clear


def to_greys(im, pal, slots, clear):
    """`im`, a grey picture, as indices into the grey slots."""
    a = np.asarray(im)
    levels = pal[slots][:, 0]
    idx = slots[np.abs(a[..., 0].astype(np.int32)[..., None] - levels[None, None]).argmin(axis=2)]
    return np.where(a[..., 3] > 0, idx, clear).astype(np.uint8)


def add(folder):
    anim_path, sheet_path = folder / "Animation.anim", folder / "Animation.sheet"
    anim = anim_path.read_text()
    lines, keys, end = chart_lines(anim, 2)
    if any(k.split("@")[0].endswith(f".{LAYER}") for k in keys):
        print(f"{folder}: has its streak already")
        return
    sheet_lines, frames, image_name = read_sheet(sheet_path)
    sheet = Image.open(folder / image_name)
    sheet.load()
    rgba = sheet.convert("RGBA")
    pictures, out = [], []
    for d in range(8):
        cells = [int(c.split("@")[0]) for c in keys[str(d)].split(",")]
        cx, cy, ux, uy, blen = body_axis(rgba, frames[cells[0]])
        hx, hy = DIRS[d]
        if ux * hx + uy * hy < 0:
            ux, uy = -ux, -uy
        length, width = blen * LENGTH, max(blen * WIDTH, MIN_WIDTH)
        mx, my = cx - ux * blen * BEHIND, cy - uy * blen * BEHIND - RAISE
        row = []
        for f in range(len(cells)):
            im, ox, oy = paint(length, width, ux, uy, f, 40 + d * 16 + f)
            row.append(f"{len(frames) + len(pictures)}%{OPACITY[f % 2]}")
            # The anchor sits where the character stands.
            pictures.append((im, round(ox - mx), round(oy - my)))
        out.append(f"{d}.{LAYER}@{PRIORITY}*=" + ",".join(row))
    # A strip under the sheet, left to right.
    x = y = shelf = 0
    places = []
    for im, _, _ in pictures:
        if x + im.width > sheet.width:
            x, y, shelf = 0, y + shelf, 0
        places.append((x, sheet.height + y))
        x += im.width
        shelf = max(shelf, im.height)
    grown_h = sheet.height + y + shelf
    if sheet.mode == "P":
        base, pal, slots, clear = grey_slots(sheet)
        grown = np.full((grown_h, sheet.width), clear, np.uint8)
        grown[:sheet.height] = base
        for (im, _, _), (px, py) in zip(pictures, places):
            region = grown[py:py + im.height, px:px + im.width]
            region[:] = np.where(np.asarray(im)[..., 3] > 0, to_greys(im, pal, slots, clear), region)
        result = Image.fromarray(grown, "P")
        result.putpalette(pal.astype(np.uint8).ravel().tolist())
        result.save(folder / image_name, optimize=True, transparency=sheet.info["transparency"])
    else:
        result = Image.new("RGBA", (sheet.width, grown_h))
        result.paste(rgba, (0, 0))
        for (im, _, _), place in zip(pictures, places):
            result.paste(im, place)
        result.save(folder / image_name, optimize=True)
    while sheet_lines and not sheet_lines[-1]:
        sheet_lines.pop()
    for (im, ax, ay), (px, py) in zip(pictures, places):
        sheet_lines.append(f"frame={px},{py},{im.width},{im.height},{ax},{ay}")
    sheet_path.write_text("\n".join(sheet_lines) + "\n")
    lines[end:end] = out
    anim_path.write_text("\n".join(lines))
    print(f"{folder}: {len(pictures)} streak frames")


def main():
    pack = Path(sys.argv[1])
    for folder in sorted((pack / "Character" / "PARTNER").iterdir()):
        if (folder / "Animation.anim").exists():
            add(folder)


if __name__ == "__main__":
    main()
