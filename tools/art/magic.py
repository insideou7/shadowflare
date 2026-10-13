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
- the status window: the MAGIC window's other tab, the same stone, with
  STATUS lit, a plate for each stat with its name cut in, where the
  engine writes the numbers (src/game/status.cpp, after ShadowFlare.exe's),
  the element names with the grooves their bars fill, and the attribute
  disc with its eight element gems. System/Game/Pattern/StatusWindow:
  window, the element bar at levels -10 to +10, the hero's mark on the
  disc.
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


# The status window's plates (x0, y0, x1, y1) and the names cut into
# them, as status.cpp lays the fields out; values end at the right.
STAT_PLATES = [
    ((16, 40, 80, 56), ""), ((88, 40, 224, 56), ""), ((232, 40, 304, 56), "Lv"),
    ((176, 64, 304, 80), "Experience"), ((16, 88, 128, 104), "HP"),
    ((16, 112, 96, 128), "Strength"), ((104, 112, 200, 128), "Attack"), ((208, 112, 304, 128), "Defense"),
    ((16, 136, 152, 152), "Hit Rate"), ((168, 136, 304, 152), "Evasion Rate"),
    ((16, 160, 152, 176), "Walking Speed"), ((168, 160, 304, 176), "Attack Speed"),
    ((16, 192, 128, 208), "MP"), ((16, 216, 152, 232), "M.Attack"), ((168, 216, 304, 232), "M.Defense"),
    ((16, 240, 152, 256), "M.Hit Rate"), ((168, 240, 304, 256), "M.Evasion Rate"),
]
# The elements in the order of their bars, with the colour of each gem
# and its letter on the disc, clockwise from the top.
ELEMENTS = [("Fire", (220, 60, 40), "F"), ("Water", (60, 130, 230), "W"), ("Earth", (170, 110, 60), "E"),
            ("Thunder", (235, 205, 50), "T"), ("Holy", (90, 160, 240), "H"), ("Dark", (160, 90, 200), "D"),
            ("Gel", (170, 170, 175), "G"), ("Metal", (120, 120, 130), "M")]
DISC_ORDER = ["F", "M", "T", "H", "W", "G", "E", "D"]  # clockwise from the top
DISC = (80, 330, 44)  # centre and radius of the attribute disc
BARS = (239, 269)  # the first bar's left and top; rows are 16 apart, 63 x 9 at most


def gem(p, cx, cy, colour, r=4.5):
    """A round coloured stone set in bronze."""
    d = np.hypot(ui.xx - cx, ui.yy - cy)
    ring = (d >= r) & (d < r + 1.5)
    p.bronze(ring, 1.2)
    stone = np.clip(1 - (d / r) ** 2, 0, 1)
    p.height += 2.5 * np.sqrt(stone)
    shine = np.clip(1.3 - np.hypot(ui.xx - cx + 1.5, ui.yy - cy + 1.5) / r, 0.5, 1.4)
    p.colour = np.where((stone > 0)[..., None], np.array(colour)[None, None, :] * shine[..., None], p.colour)


def status_window(font):
    ui.canvas(320, 416)
    p = ui.Panel(63)
    for (x0, y0, x1, y1), name in STAT_PLATES:
        p.sunken(x0, y0, x1, y1, depth=2, floor=ui.STONE * 0.55)
        if name:
            ui.engrave(p, name, x0 + 3, y0 + 4)
    ui.engrave(p, "BODY", 276, 94, ui.BRONZE * 0.55)
    ui.engrave(p, "MENTAL", 264, 198, ui.BRONZE * 0.55)
    # The elements: a gem and the name on a plate, then the bar's groove.
    for i, (name, colour, _) in enumerate(ELEMENTS):
        y = BARS[1] + 16 * i
        p.sunken(166, y - 3, 230, y + 11, depth=1.5, rim=False, floor=ui.STONE * 0.6)
        gem(p, 173, y + 4, colour, 3.5)
        ui.engrave(p, name, 180, y)
        p.sunken(BARS[0] - 2, y - 2, BARS[0] + 65, y + 11, depth=2.5, floor=ui.HOLLOW)
    # The attribute disc: a dark octagon sunk in the stone, crossed by
    # four lines, its eight element gems round the edge.
    cx, cy, r = DISC
    ang = np.arctan2(ui.yy - cy, ui.xx - cx)
    rad = np.hypot(ui.xx - cx, ui.yy - cy)
    octagon = rad * np.cos((ang + np.pi / 8) % (np.pi / 4) - np.pi / 8) < r
    p.height -= 3 * octagon
    p.colour = np.where(octagon[..., None], np.array([22, 30, 52]) * (0.85 + 0.3 * p.noise(12, 2))[..., None], p.colour)
    rim = (rad * np.cos((ang + np.pi / 8) % (np.pi / 4) - np.pi / 8) < r + 2) & ~octagon
    p.bronze(rim, 1.5)
    for k in range(4):
        a = k * np.pi / 4
        line = (np.abs((ui.xx - cx) * np.sin(a) - (ui.yy - cy) * np.cos(a)) < 0.6) & (rad < r + 12)
        p.colour[line] = ui.BRONZE * 0.7
    letters = []
    colours = {letter: colour for _, colour, letter in ELEMENTS}
    for k, letter in enumerate(DISC_ORDER):
        a = -np.pi / 2 + k * np.pi / 4
        gx, gy = cx + (r + 16) * np.cos(a), cy + (r + 16) * np.sin(a)
        gem(p, gx - 4, gy, colours[letter], 4.5)
        letters.append((letter, int(gx + 3), int(gy - 4)))
    # The tabs: STATUS raised and lit, MAGIC sunk and dim (the magic
    # window's, the other way round).
    p.bronze(ui.inside(10, 8, 154, 34) & ~ui.inside(12, 10, 152, 32), 2.5)
    p.sunken(166, 8, 310, 34, depth=2, rim=True, floor=ui.STONE * 0.55)
    p.boss((160, 21), 5, 4)
    img = p.image()
    for letter, x, y in letters:
        lettering(img, x, y, letter, (230, 220, 200, 255), font, scale=1)
    lettering(img, 82 - 36, 9, "STATUS", (236, 210, 140, 255), font)
    lettering(img, 238 - 30, 9, "MAGIC", (120, 104, 72, 255), font)
    return img


def element_bar(level):
    """The bar of an element level from -10 to +10: 3 pixels wide at -10,
    3 more a level, green, lit along its top."""
    w = 3 + 3 * (level + 10)
    a = np.zeros((9, w, 4), np.uint8)
    shade = np.array([1.35, 1.2, 1.05, 1.0, 0.95, 0.9, 0.85, 0.75, 0.6])
    a[..., :3] = np.clip(np.array([60, 190, 80])[None, None, :] * shade[:, None, None], 0, 255)
    a[..., 3] = 255
    return Image.fromarray(a, "RGBA")


def disc_mark():
    """The hero's place on the disc: a small bright green spark."""
    yy, xx = np.mgrid[0:11, 0:11].astype(float)
    d = np.hypot(xx - 5, yy - 5)
    glow = np.clip(1 - d / 5.5, 0, 1)
    a = np.zeros((11, 11, 4), np.uint8)
    a[..., 0] = np.clip(120 + 135 * glow, 0, 255)
    a[..., 1] = 255
    a[..., 2] = np.clip(120 + 100 * glow, 0, 255)
    a[..., 3] = np.clip(255 * glow ** 0.7, 0, 255)
    return Image.fromarray(a, "RGBA")


def status_sheet(out):
    font = Image.open(out / "System/Common/Pattern/Font00.png").convert("RGBA")
    window_img = status_window(font)
    bars = [element_bar(level) for level in range(-10, 11)]
    sheet = Image.new("RGBA", (320, 416 + 2 + 21 * 10 + 12), (0, 0, 0, 0))
    sheet.paste(window_img, (0, 0))
    frames = [(0, 0, 320, 416, 0, 0)]
    for k, bar in enumerate(bars):
        sheet.paste(bar, (0, 418 + 10 * k))
        frames.append((0, 418 + 10 * k, bar.width, 9, -BARS[0], -BARS[1]))
    mark = disc_mark()
    sheet.paste(mark, (70, 418))
    frames.append((70, 418, 11, 11, 5, 5))
    write_sheet(out / "System/Game/Pattern", "StatusWindow", sheet, frames,
                "The status window: window, the element bar at levels -10 to +10, the hero's mark on the disc.")
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
    only = set(sys.argv[2:])  # magic, status, powerup: to redraw just those
    if not only or "magic" in only:
        magic_window(out)
    if not only or "status" in only:
        status_sheet(out)
    if not only or "powerup" in only:
        powerup(out)
    # The status window stays full colour: 256 colours turn its small
    # element gems all bronze.
    names = {"magic": "System/Game/Pattern/Magic.png", "powerup": "Player/Common/Powerup.png"}
    for key, name in names.items():
        if only and key not in only:
            continue
        shrink.shrink(out / name)
    print("wrote " + ", ".join(k for k in ("magic", "status", "powerup") if not only or k in only))


if __name__ == "__main__":
    main()
