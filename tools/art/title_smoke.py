#!/usr/bin/env python3
"""The steam on the title screen, for the open content pack.

    python3 tools/art/title_smoke.py <content pack folder>

Draws System/Title/Pattern/Smoke00-09 (.anim, .sheet, .png), our own
versions of the original's ten jets of steam: each one chart, drawn with no
direction (8) and additively, 46 frames, one for each of the original's
ticks (30 a second). The engine places jet i at the mouth of a pipe on the
title picture (src/logic/title_smoke.h; tools/art/title_screen.py leaves
those pipe ends open) and plays it now and then. The even jets blow to the
right, the odd ones to the left.

Made from scratch with numpy: puffs of steam are thrown out of the mouth
for the first few frames, slow down, swell, rise a little and thin out,
and each frame is the sum of soft blobs, smeared along their motion so
fast ones read as streaks, broken up by drifting noise.
"""

import math
import sys
from pathlib import Path

import numpy as np
from PIL import Image

sys.path.insert(0, str(Path(__file__).resolve().parent))
from ui import write_sheet  # noqa: E402

FRAMES = 46
# Per jet: how far it reaches in pixels, how wide it fans (degrees), how
# long it keeps blowing and how fast it gets to its full length (frames),
# and its seed. Jets 0 and 1 are short
# bursts that hang as a small cloud; the rest are long jets that break up
# into puffs, about as long as the original's.
JETS = [
    (66, 22, 18, 2, 1), (66, 22, 18, 2, 2),
    (150, 5, 14, 4, 3), (150, 5, 14, 4, 4),
    (190, 7, 16, 5, 5), (190, 7, 16, 5, 6),
    (195, 8, 16, 5, 7), (195, 8, 16, 5, 8),
    (160, 8, 15, 5, 9), (160, 8, 15, 5, 10),
]
COLOUR = np.array([226, 232, 242], dtype=float)
# The brightest the steam gets (0..1).
PEAK = 0.95
# A frame's canvas, with the mouth at MOUTH; trimmed to what is drawn.
CW, CH = 280, 120
MOUTH = (16, 70)


def noise(rng, w, h, scale):
    small = rng.random((h // scale + 3, w // scale + 3))
    im = Image.fromarray((small * 255).astype(np.uint8)).resize(((w // scale + 3) * scale, (h // scale + 3) * scale),
                                                               Image.BICUBIC)
    return np.asarray(im, dtype=float)[:h, :w] / 255


def stretched(rng, w, h, sx, sy):
    """Noise drawn out sx by sy pixels: long grains for streaming steam."""
    small = rng.random((h // sy + 3, w // sx + 3))
    im = Image.fromarray((small * 255).astype(np.uint8)).resize(((w // sx + 3) * sx, (h // sy + 3) * sy), Image.BICUBIC)
    return np.asarray(im, dtype=float)[:h, :w] / 255


def jet(reach, fan, blow, grow, seed):
    """The jet's frames, blowing to the right, as float images 0..1.

    While it blows (frames 1 to `blow`) the steam is a cone growing out of
    the mouth to `reach`, widening by `fan` degrees, bright at the mouth and
    streaked along its length. Then it lifts a little, swells, falls apart
    into lumps and fades out by the last frame."""
    rng = np.random.default_rng(seed)
    gy, gx = np.mgrid[0:CH, 0:CW].astype(float)
    d = gx - MOUTH[0]
    streaks = stretched(rng, CW, CH, 22, 2)
    fine = stretched(rng, CW, CH, 5, 3)
    lumps = [noise(np.random.default_rng(seed * 10 + k), CW + 80, CH + 32, sc) for k, sc in enumerate((13, 6))]
    slope = math.tan(math.radians(fan))
    frames = [np.zeros((CH, CW))]
    for t in range(1, FRAMES):
        # How far it has reached: fast at first, slowing to a stop.
        length = reach * (1 - math.exp(-t / grow)) + 6
        late = max(0, t - blow)
        lift = 0.35 * late
        swell = 1 + 0.025 * late
        half = (1.5 + slope * np.clip(d, 0, None)) * swell
        if fan > 10:
            # A burst bunches up into a round cloud rather than a fan.
            half = np.minimum(half, 13 * swell)
        dy = gy - (MOUTH[1] - lift * np.clip(d / reach, 0.2, 1))
        across = np.exp(-1.2 * (dy / half) ** 2)
        # A soft, rounded front.
        along = np.clip(d / 3, 0, 1) * np.exp(-(np.clip(d - 0.65 * length, 0, None) / (0.2 * length)) ** 2)
        # Dense near the mouth, thinner toward the tip.
        along *= 0.55 + 0.45 * np.clip(1 - d / reach, 0, 1)
        density = 2.0 * along * across * (0.6 + 0.4 * streaks) * (0.6 + 0.4 * fine)
        if fan > 10 and t < 8:
            # A burst first throws out thin rays, which fill in as it goes.
            ang = np.arctan2(dy, np.clip(d, 1, None))
            rays = 0.5 + 0.5 * np.cos(ang * 60 + np.sin(ang * 23) * 3)
            k = t / 8
            density *= k + (1 - k) * rays ** 3
        if late:
            a, b = 80 - int(late * 0.8), 80 - int(late * 0.4)
            lump = 0.6 * lumps[0][16:16 + CH, a:a + CW] + 0.4 * lumps[1][8:8 + CH, b:b + CW]
            gone = min(1.0, late / (FRAMES - 1 - blow))
            density *= np.clip((lump - 0.15 - 0.4 * gone) * 1.8, 0, 1.2) * (1 - 0.5 * gone)
        frames.append(PEAK * (1 - np.exp(-1.5 * density)))
    return frames


def main():
    out = Path(sys.argv[1]) / "System/Title/Pattern"
    for n, (reach, fan, blow, grow, seed) in enumerate(JETS):
        left = n % 2 == 1
        frames = jet(reach, fan, blow, grow, seed)
        pieces = []
        for f in frames:
            if left:
                f = f[:, ::-1]
            ys, xs = np.nonzero(f > 0.02)
            if len(xs) == 0:
                pieces.append((np.zeros((1, 1, 4), np.uint8), 0, 0))
                continue
            x0, x1, y0, y1 = xs.min(), xs.max() + 1, ys.min(), ys.max() + 1
            v = f[y0:y1, x0:x1]
            rgba = np.zeros((y1 - y0, x1 - x0, 4), np.uint8)
            rgba[..., :3] = np.clip(COLOUR * v[..., None], 0, 255).astype(np.uint8)
            rgba[..., 3] = np.where(v > 0.02, 255, 0)
            pieces.append((rgba, x0, y0))
        mx = CW - 1 - MOUTH[0] if left else MOUTH[0]
        # Pack the frames in rows.
        width = 1024
        x = y = row = 0
        rects = []
        for rgba, _, _ in pieces:
            h, w = rgba.shape[:2]
            if x + w > width:
                x, y, row = 0, y + row + 1, 0
            rects.append((x, y))
            x += w + 1
            row = max(row, h)
        sheet = np.zeros((y + row + 1, width, 4), np.uint8)
        frame_list = []
        for (rgba, fx, fy), (sx, sy) in zip(pieces, rects):
            h, w = rgba.shape[:2]
            sheet[sy:sy + h, sx:sx + w] = rgba
            frame_list.append((sx, sy, w, h, mx - fx, MOUTH[1] - fy))
        name = f"Smoke{n:02d}"
        write_sheet(out, name, Image.fromarray(sheet), frame_list,
                    f"Title steam jet {n} (the original's {name}.njp), drawn with tools/art/title_smoke.py.")
        sheet_file = out / f"{name}.sheet"
        sheet_file.write_text(sheet_file.read_text().replace("tools/art/ui.py", "tools/art/title_smoke.py"))
        (out / f"{name}.anim").write_text(
            "# Drawn by tools/art/title_smoke.py (our own art, see content/README.md).\n"
            f"sheet={name}.sheet\n\n[chart 0 once]\n8*=" + ",".join(str(i) for i in range(FRAMES)) + "\n")
        print("wrote", name)


if __name__ == "__main__":
    main()
