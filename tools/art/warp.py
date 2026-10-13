#!/usr/bin/env python3
"""The GATE window for the open content pack.

    python3 tools/art/warp.py <content pack folder>

Draws, in the style of the original but from scratch: the window the warp
gates are picked from, 320 x 416 of the same carved stone as the other
windows (ui.py), with GATE carved along the top, a deep maroon field for
ten gates a page and a strip along the bottom for the page arrows and the
page number; then the button each gate's name sits beside (a bronze plate
with a ring in it: as it is, pushed in, and lit with the mouse over it)
and the two page arrows, where the engine puts them
(src/game/warp_gate.cpp, after ShadowFlare.exe's). Written as
System/Game/Pattern/Warp: window, previous page, next page, button,
button pushed, button lit.

Needs numpy and Pillow.
"""

import sys
from pathlib import Path

import numpy as np
from PIL import Image

import magic
import shrink
import ui

HEADER = "# Drawn by tools/art/warp.py (our own art, see content/README.md).\n"

FIELD = (28, 44, 292, 360)
STRIP = (28, 366, 292, 390)
PREV, NEXT = (45, 368), (243, 368)  # where the page arrows go
BUTTON = (54, 61)  # the first row's button; rows are 30 apart


def window(font):
    ui.canvas(320, 416)
    p = ui.Panel(71)
    x0, y0, x1, y1 = FIELD
    p.sunken(x0, y0, x1, y1, depth=4, floor=ui.MAROON * 1.2)
    sx0, sy0, sx1, sy1 = STRIP
    p.sunken(sx0, sy0, sx1, sy1, depth=3, floor=ui.MAROON * 0.9)
    # A bronze rule under the title with studs, and a boss either side of it.
    p.bronze((np.abs(ui.yy - 36) < 0.8) & ui.inside(x0 + 24, 0, x1 - 24, 416), 1.5)
    for x in range(x0 + 44, x1 - 30, 48):
        p.boss((x, 36), 2.2, 2)
    for x in (98, 222):
        p.boss((x, 20), 4, 4)
    img = p.image()
    magic.lettering(img, 160 - 24, 8, "GATE", (220, 196, 132, 255), font)
    return img


def button(state):
    """20 x 20: a bronze plate with a dark ring set in it. Pushed, the plate
    sinks and darkens; lit, a pale glow fills the ring."""
    ui.canvas(20, 20)
    p = ui.Panel(72 + state)
    p.colour[:] = ui.BRONZE * (0.45 if state == 1 else 0.65)
    p.height[:] = 0
    p.height += (1 if state == 1 else 3) * ui.box(0, 0, 20, 20, 2)
    d = np.hypot(ui.xx - 9.5, ui.yy - 9.5)
    ring = (d > 3.5) & (d < 6.2)
    p.colour[ring] = ui.STONE * 0.45
    p.height[ring] -= 1.5
    p.colour[d <= 3.5] = ui.BRONZE * (0.35 if state == 1 else 0.5)
    img = np.asarray(p.image()).astype(float)
    if state == 2:
        glow = np.clip(1 - d / 8, 0, 1) ** 1.5
        img[..., :3] = np.clip(img[..., :3] + glow[..., None] * np.array([210, 200, 170]), 0, 255)
    ui.canvas(320, 416)
    return Image.fromarray(img.astype(np.uint8), "RGBA")


def main():
    out = Path(sys.argv[1])
    font = Image.open(out / "System/Common/Pattern/Font00.png").convert("RGBA")
    parts = [magic.arrow(True), magic.arrow(False), button(0), button(1), button(2)]
    anchors = [(-PREV[0], -PREV[1]), (-NEXT[0], -NEXT[1])] + [(-BUTTON[0] + 52, -BUTTON[1] + 59)] * 3
    sheet = Image.new("RGBA", (320, 416 + 22), (0, 0, 0, 0))
    sheet.paste(window(font), (0, 0))
    frames = [(0, 0, 320, 416, 0, 0)]
    x = 0
    for img, (ax, ay) in zip(parts, anchors):
        sheet.paste(img, (x, 417))
        frames.append((x, 417, img.width, img.height, ax, ay))
        x += img.width + 2
    folder = out / "System/Game/Pattern"
    folder.mkdir(parents=True, exist_ok=True)
    sheet.save(folder / "Warp.png", optimize=True)
    with open(folder / "Warp.sheet", "w", newline="\n") as f:
        f.write(HEADER)
        f.write("# The GATE window: window, previous page, next page, button, button pushed, button lit.\n")
        f.write("image=Warp.png\n")
        for fr in frames:
            f.write("frame=" + ",".join(str(v) for v in fr) + "\n")
    shrink.shrink(folder / "Warp.png")
    print("wrote the GATE window")


if __name__ == "__main__":
    main()
