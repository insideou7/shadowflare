#!/usr/bin/env python3
"""The inventory panel's background for the open content pack.

    python3 tools/art/ui.py <content pack folder>

Draws a 320 x 416 window of dark carved stone in an iron frame with
bronze trim, with sunken recesses for the equipment slots and the 9 x 4
bag, and plates for the totals and the gold, where the engine puts them
(src/game/items.cpp, which follows ShadowFlare.exe's item window). The
picture is built as a height map and shaded with a light from the top
left, so the frame stands out and the recesses sink in. Writes
System/Game/Pattern/Inventory.png + .sheet; the frame's anchor puts it at
x = 320 on the screen. Needs numpy and Pillow.
"""

import sys
from pathlib import Path

import numpy as np
from PIL import Image

W, H = 320, 416
LEFT = 320  # where the window sits on the 640 x 480 screen
CELL = 32
BAG = (336 - LEFT, 264, 9, 4)  # x, y, columns, rows
# Equipment slots (screen x, y, cells across, down), as in items.cpp.
SLOTS = [(560, 16, 2, 2), (560, 88, 2, 3), (560, 192, 2, 2), (480, 16, 2, 4), (480, 160, 2, 3),
         (400, 143, 1, 1), (440, 143, 1, 1), (400, 183, 1, 1), (440, 183, 1, 1)]
STATS_PLATE = (10, 10, 150, 102)  # x0, y0, x1, y1: the totals are written here
GOLD_PLATE = (10, 396, 170, 411)

STONE = np.array([42, 40, 47])
IRON = np.array([50, 48, 49])
BRONZE = np.array([160, 124, 66])
HOLLOW = np.array([24, 20, 24])

rng = np.random.default_rng(7)


def noise(scale, octaves=4):
    """Smooth value noise in 0..1, summed over octaves."""
    total = np.zeros((H, W))
    amp, norm = 1.0, 0.0
    for o in range(octaves):
        step = max(1, int(scale / 2 ** o))
        grid = rng.random((H // step + 2, W // step + 2))
        small = Image.fromarray((grid * 255).astype(np.uint8))
        big = small.resize(((W // step + 2) * step, (H // step + 2) * step), Image.BICUBIC)
        total += amp * np.asarray(big, dtype=float)[:H, :W] / 255
        norm += amp
        amp *= 0.5
    return total / norm


yy, xx = np.mgrid[0:H, 0:W].astype(float)


def box(x0, y0, x1, y1, bevel):
    """1 inside the rectangle, falling to 0 over `bevel` pixels at its edges."""
    d = np.minimum.reduce([xx - x0, x1 - 1 - xx, yy - y0, y1 - 1 - yy])
    return np.clip((d + 1) / bevel, 0, 1)


def inside(x0, y0, x1, y1):
    return (xx >= x0) & (xx < x1) & (yy >= y0) & (yy < y1)


def main():
    out = Path(sys.argv[1])
    height = np.zeros((H, W))
    colour = np.zeros((H, W, 3))
    # Carved stone: blotchy, with fine grain and darker cracks.
    blotch, grain, vein = noise(48), noise(6, 2), noise(24, 3)
    shade = 0.8 + 0.4 * blotch + 0.15 * (grain - 0.5)
    cracks = np.clip(1 - np.abs(vein - 0.5) * 40, 0, 1) * 0.35
    colour[:] = STONE * (shade - cracks)[..., None]
    height += 0.6 * grain + 0.8 * blotch - 2 * cracks

    # The iron frame round the edge, with a bronze line just inside it.
    frame = 1 - box(6, 6, W - 6, H - 6, 1)
    iron = IRON * (0.75 + 0.5 * noise(10, 3))[..., None]
    colour = np.where(frame[..., None] > 0, iron, colour)
    height += 6 * frame * box(0, 0, W, H, 4)
    trim = inside(7, 7, W - 7, H - 7) & ~inside(8, 8, W - 8, H - 8)
    colour[trim] = BRONZE * 0.9
    height[trim] += 2.5
    # Studs along the frame and bigger bosses in the corners.
    studs = [(x, 3) for x in range(26, W - 20, 36)] + [(x, H - 4) for x in range(26, W - 20, 36)]
    studs += [(3, y) for y in range(30, H - 20, 36)] + [(W - 4, y) for y in range(30, H - 20, 36)]
    for (cx, cy), r in [((s[0], s[1]), 2.0) for s in studs] + [((5, 5), 4.5), ((W - 6, 5), 4.5),
                                                                 ((5, H - 6), 4.5), ((W - 6, H - 6), 4.5)]:
        d = np.hypot(xx - cx, yy - cy)
        bump = np.clip(1 - (d / r) ** 2, 0, 1)
        height += 5 * np.sqrt(bump)
        colour = np.where((bump > 0)[..., None], BRONZE * (0.7 + 0.3 * bump)[..., None], colour)

    def sunken(x0, y0, x1, y1, depth=4, rim=True, floor=HOLLOW):
        nonlocal colour
        if rim:  # a raised bronze lip round the hole
            lip = inside(x0 - 2, y0 - 2, x1 + 2, y1 + 2) & ~inside(x0, y0, x1, y1)
            colour[lip] = BRONZE * 0.75
            height[lip] += 1.5
        hole = box(x0, y0, x1, y1, 2.5)
        height[:] -= depth * hole
        dark = floor * (0.8 + 0.4 * noise(16, 2))[..., None]
        colour = np.where((hole > 0)[..., None], dark, colour)

    for sx, sy, cw, ch in SLOTS:
        sunken(sx - LEFT, sy, sx - LEFT + cw * CELL, sy + ch * CELL)
    bx, by, cols, rows = BAG
    sunken(bx, by, bx + cols * CELL, by + rows * CELL)
    # Bag cells: thin raised stone ribs between them.
    for c in range(1, cols):
        x = bx + c * CELL
        rib = inside(x - 1, by, x + 1, by + rows * CELL)
        height[rib] += 3
        colour[rib] = STONE * 0.9
    for r in range(1, rows):
        y = by + r * CELL
        rib = inside(bx, y - 1, bx + cols * CELL, y + 1)
        height[rib] += 3
        colour[rib] = STONE * 0.9
    # Plates for the totals and the gold: shallow, smooth and dark.
    for x0, y0, x1, y1 in (STATS_PLATE, GOLD_PLATE):
        sunken(x0, y0, x1, y1, depth=1.5, rim=False, floor=STONE * 0.6)

    # Light from the top left.
    gy, gx = np.gradient(height)
    light = np.clip(1.0 - 0.18 * gx - 0.18 * gy, 0.35, 1.6)
    image = np.clip(colour * light[..., None], 0, 255).astype(np.uint8)
    alpha = np.full((H, W, 1), 255, dtype=np.uint8)
    folder = out / "System/Game/Pattern"
    folder.mkdir(parents=True, exist_ok=True)
    Image.fromarray(np.concatenate([image, alpha], axis=2), "RGBA").save(folder / "Inventory.png", optimize=True)
    with open(folder / "Inventory.sheet", "w", newline="\n") as f:
        f.write("# Drawn by tools/art/ui.py (our own art, see content/README.md).\n")
        f.write("# The inventory panel; the anchor puts it at x = 320 on the 640 x 480 screen.\n")
        f.write(f"image=Inventory.png\nframe=0,0,{W},{H},{-LEFT},0\n")
    print("wrote the inventory panel")


if __name__ == "__main__":
    main()
