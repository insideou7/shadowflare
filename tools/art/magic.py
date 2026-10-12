#!/usr/bin/env python3
"""The magic window and the power-mode glow for the open
content pack.

    python3 tools/art/magic.py <content pack folder>

Draws, in the style of the original but from scratch:

- the magic window: the STATUS window's other tab, 320 x 416 of the same
  carved stone as the other windows (ui.py), with the STATUS and MAGIC
  tabs along the top, a sunken field for six spells a page, the box each
  spell's icon sits in, the page arrows and the recess of the bar that
  spells are dragged to, where the engine puts them
  (src/game/magic_window.cpp, after ShadowFlare.exe's).
  System/Game/Pattern/Magic: window, icon box, previous page, next page.
- the increased-power glow around the hero: a cage of blue lightning in
  an egg shape that turns over 30 frames, on black so it can be added to
  what is behind. Player/Common/Powerup (.anim and .sheet).

Needs numpy and Pillow.
"""

import math
import sys
from pathlib import Path

import numpy as np
from PIL import Image

import shrink
import ui

HEADER = "# Drawn by tools/art/magic.py (our own art, see content/README.md).\n"

ROWS = [59 + 48 * k for k in range(6)]  # each spell's icon top
FIELD = (14, 44, 306, 334)
BAR = (27, 354, 289, 390)  # the eight slots from x = 29, 32 apart, y 356-387
PREV, NEXT = (17, 336), (272, 336)


def write_sheet(folder, name, image, frames, comment):
    folder.mkdir(parents=True, exist_ok=True)
    image.save(folder / f"{name}.png", optimize=True)
    with open(folder / f"{name}.sheet", "w", newline="\n") as f:
        f.write(HEADER)
        f.write(f"# {comment}\n")
        f.write(f"image={name}.png\n")
        for fr in frames:
            f.write("frame=" + ",".join(str(v) for v in fr) + "\n")


def lettering(img, x, y, text, colour, font, scale=2):
    """Our own 6 x 12 font (ui.py's), doubled, with a dark shadow."""
    for dx, dy, c in ((1, 1, (10, 8, 8, 255)), (0, 0, colour)):
        for i, ch in enumerate(text.encode("latin-1")):
            cell = font.crop(((ch % 16) * 6, (ch // 16) * 12, (ch % 16) * 6 + 6, (ch // 16) * 12 + 12))
            cell = cell.resize((6 * scale, 12 * scale), Image.NEAREST)
            mask = cell.split()[3]
            img.paste(Image.new("RGBA", cell.size, c), (x + i * 6 * scale + dx * scale, y + dy * scale), mask)


def window(font):
    ui.canvas(320, 416)
    p = ui.Panel(61)
    x0, y0, x1, y1 = FIELD
    p.sunken(x0, y0, x1, y1, depth=4, floor=ui.HOLLOW * 1.1)
    # A faint bronze rule between the rows.
    for y in ROWS[1:]:
        p.bronze((np.abs(ui.yy - (y - 8)) < 0.6) & ui.inside(x0 + 24, 0, x1 - 24, 416), 0.6)
    bx0, by0, bx1, by1 = BAR
    p.sunken(bx0, by0, bx1, by1, depth=3, floor=ui.HOLLOW)
    p.cells(29, 356, 8, 1, ui.BRONZE * 0.6, raised=2)
    # The tabs: STATUS sunk and dim, MAGIC raised and lit.
    p.sunken(10, 8, 154, 34, depth=2, rim=True, floor=ui.STONE * 0.55)
    p.bronze(ui.inside(166, 8, 310, 34) & ~ui.inside(168, 10, 308, 32), 2.5)
    p.boss((160, 21), 5, 4)
    img = p.image()
    lettering(img, 82 - 36, 9, "STATUS", (120, 104, 72, 255), font)
    lettering(img, 238 - 30, 9, "MAGIC", (236, 210, 140, 255), font)
    return img


def icon_box():
    """The 32 x 32 bronze box a spell's icon sits in, its inside dark."""
    ui.canvas(32, 32)
    p = ui.Panel(62)
    p.colour[:] = ui.BRONZE * 0.6
    p.height[:] = 0
    p.height += 3 * ui.box(0, 0, 32, 32, 2)
    p.sunken(3, 3, 29, 29, depth=2, rim=False, floor=ui.HOLLOW * 0.9)
    return p.image()


def arrow(back, lit=True):
    """A page arrow: two bronze chevrons on a dark plate, 33 x 16."""
    a = np.zeros((16, 33, 4), np.uint8)
    yy, xx = np.mgrid[0:16, 0:33].astype(float)
    for cx in (9, 22):
        u = (cx - xx) if back else (xx - cx)
        d = np.abs(np.abs(yy - 7.5) * 0.9 + u)
        ink = (d < 2.2) & (np.abs(yy - 7.5) < 7) & (np.abs(xx - cx) < 8)
        shade = 1.1 - 0.5 * (yy / 16)
        col = (ui.BRONZE * (1.25 if lit else 0.8))[None, None, :] * shade[..., None]
        a[ink, :3] = np.clip(col[ink], 0, 255)
        a[ink, 3] = 255
    return Image.fromarray(a, "RGBA")


def magic_window(out):
    font = Image.open(out / "System/Common/Pattern/Font00.png").convert("RGBA")
    parts = [window(font), icon_box(), arrow(True), arrow(False)]
    sheet = Image.new("RGBA", (320, 416 + 34), (0, 0, 0, 0))
    sheet.paste(parts[0], (0, 0))
    frames = [(0, 0, 320, 416, 0, 0)]
    x = 0
    anchors = [(0, 0), (-PREV[0], -PREV[1]), (-NEXT[0], -NEXT[1])]
    for img, (ax, ay) in zip(parts[1:], anchors):
        sheet.paste(img, (x, 417))
        frames.append((x, 417, img.width, img.height, ax, ay))
        x += img.width + 2
    write_sheet(out / "System/Game/Pattern", "Magic", sheet, frames,
                "The magic window: window, icon box, previous page, next page.")
    ui.canvas(320, 416)


def powerup(out):
    """30 frames of a lightning cage turning around the hero, 96 x 128,
    anchored at the hero's feet 92 pixels down."""
    W, H, FEET = 96, 128, 92
    S = 3
    rng = np.random.default_rng(500)
    y, x = np.mgrid[0:H * S, 0:W * S].astype(float) / S
    # The egg: centred above the feet, wider low down.
    cx, cy = W / 2, FEET - 46
    frames = []
    strands = [(k * 2 * np.pi / 14 + rng.uniform(-0.15, 0.15), rng.uniform(0.7, 1.0)) for k in range(14)]
    for f in range(30):
        turn = f / 30 * 2 * np.pi
        light = np.zeros_like(x)
        for k, (phase, bright) in enumerate(strands):
            # A meridian of the egg seen from the side: x = sin(a) * width(y).
            a = phase + turn
            t = np.clip((y - 6) / (FEET + 4 - 6), 0, 1)
            width = 34 * np.sin(np.pi * t) ** 0.8 * (0.85 + 0.25 * t)
            wobble = 2.5 * np.sin(y / 5 + k * 1.7 + f * 0.9) + 1.2 * np.sin(y / 1.7 + k * 3.1 + f * 2.3)
            mx = cx + np.sin(a) * width + wobble
            d = np.abs(x - mx)
            front = 0.55 + 0.45 * np.cos(a)  # strands in front shine brighter
            light += 0.7 * bright * front * np.exp(-(d / 1.2) ** 2) * ((t > 0.02) & (t < 0.98))
        # A haze inside and sparks flying off.
        e = np.hypot((x - cx) / 30, (y - cy - 6) / 46)
        light += 0.5 * np.exp(-e ** 2 * 1.5) * (0.8 + 0.2 * math.sin(f / 30 * 4 * np.pi))
        for _ in range(14):
            sa = rng.uniform(0, 2 * np.pi)
            sr = rng.uniform(30, 48)
            sx, sy = cx + math.cos(sa) * sr * 0.8, cy + 10 + math.sin(sa) * sr
            light += 0.9 * np.exp(-((x - sx) ** 2 + (y - sy) ** 2) / 0.8)
        c = np.array([0.45, 0.35, 1.0])
        rgb = light[..., None] * c + (np.clip(light - 0.8, 0, 1) ** 1.5)[..., None] * np.array([0.6, 0.6, 0.4])
        img = Image.fromarray((np.clip(rgb, 0, 1) * 255).astype(np.uint8), "RGB").resize((W, H), Image.LANCZOS)
        frames.append(img.convert("RGBA"))
    cols = 6
    sheet = Image.new("RGBA", (cols * W, 5 * H), (0, 0, 0, 255))
    rows = []
    for i, img in enumerate(frames):
        sheet.paste(img, ((i % cols) * W, (i // cols) * H))
        rows.append(((i % cols) * W, (i // cols) * H, W, H, W // 2, FEET))
    folder = out / "Player/Common"
    write_sheet(folder, "Powerup", sheet, rows, "The increased-power glow, frame by frame, on black to be added.")
    # Opacity falls from full to a quarter and back over the 30 frames.
    cells = []
    for i in range(30):
        trans = int(round(1000 - 750 * (1 - math.cos(i / 30 * 2 * np.pi)) / 2))
        cells.append(f"{i}%{trans}")
    with open(folder / "Powerup.anim", "w", newline="\n") as fh:
        fh.write(HEADER)
        fh.write("sheet=Powerup.sheet\n\n[chart 0 loop]\n")
        fh.write("8*=" + ",".join(cells) + "\n")


def main():
    out = Path(sys.argv[1])
    magic_window(out)
    powerup(out)
    for name in ("System/Game/Pattern/Magic.png", "Player/Common/Powerup.png"):
        shrink.shrink(out / name)
    print("wrote the magic window and the power-mode glow")


if __name__ == "__main__":
    main()
