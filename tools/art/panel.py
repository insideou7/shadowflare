#!/usr/bin/env python3
"""The game panel along the bottom of the screen, for the open content pack.

    python3 tools/art/panel.py <content pack folder>

Writes System/Game/Pattern/Bar (.png and .sheet) with the same pattern
numbers and screen positions as the original's Bar.njp, so the engine
draws either the same way (src/game/panel.cpp lists them). Everything is
drawn from scratch in the style of the rest of tools/art/ui.py: a wall of
sandstone blocks with a slate plaque on the right, bronze-framed glass
gauges for HP and MP, see-through pockets for the belt, small plates for
RUN and WALK, the MINE button, the increased-power button and the level.

The parts sit where the original's clicks and gauges are (0x4039f0,
0x445d10), since that is where the engine looks for them. Needs numpy and
Pillow.
"""

import sys
from pathlib import Path

import numpy as np
from PIL import Image

import shrink
import ui
from ui import BRONZE, CAPS, Panel, box, inside, write_sheet

TOP = 400  # the panel's pictures start at this screen row (pattern 7), the body at 412 (pattern 8)
SAND = np.array([104, 90, 54])
MORTAR = np.array([20, 18, 14])
GOLD = np.array([206, 170, 96])
SMOKE = np.array([40, 40, 44])
SLATE = np.array([48, 46, 42])
GLASS = np.array([18, 16, 20])

HP_TUBE = (81, 426, 206, 12)  # x, y, w, h on the screen
MP_TUBE = (106, 453, 206, 12)
BELT = [(357 + 32 * k, 413) for k in range(4)] + [(405 + 32 * k, 445) for k in range(4)]
MINE = (496, 424, 16, 16)
RUN = (360, 448, 32, 16)
WALK = (368, 464, 32, 16)
POWER = (25, 408, 34, 23)
LEVEL_PLATE = (25, 452, 56, 27)
PLAQUE_LEFT = (515, 536)  # the slate's left edge above and below y 445
STATUS_LABEL = (540, 424)
ITEM_LABEL = (595, 434)
MENU_TAB = (583, 401, 57, 13)


def text(img, x, y, s, colour, shadow=(0, 0, 0, 255)):
    """The pack's 5 x 8 capitals, one pixel apart, with a shadow below right."""
    for dx, dy, c in ((1, 1, shadow), (0, 0, colour)):
        if c is None:
            continue
        cx = x
        for ch in s:
            for row, bits in enumerate(CAPS.get(ch, ["....."] * 8)):
                for col, bit in enumerate(bits):
                    if bit == "#" and 0 <= cx + col + dx < img.width and 0 <= y + row + dy < img.height:
                        img.putpixel((cx + col + dx, y + row + dy), c)
            cx += 6


def wall(p):
    """Courses of sandstone blocks 8 high, each block a little different,
    in dark mortar."""
    rng = np.random.default_rng(3)
    h, w = p.height.shape
    for course in range(-1, h // 8 + 1):
        y0 = course * 8 + 2
        x = -int(rng.integers(0, 12))
        while x < w:
            bw = int(rng.integers(13, 21))
            block = box(x + 1, y0 + 1, x + bw, y0 + 8, 2)
            tone = 0.75 + 0.35 * rng.random()
            tint = np.array([1.0, 1.0 - 0.08 * rng.random(), 1.0 - 0.15 * rng.random()])
            mask = block > 0
            p.colour[mask] = (SAND * tint * tone)[None, :] * (0.85 + 0.3 * p.noise(5, 2)[mask])[:, None]
            p.height += 2.5 * block
            x += bw
    joints = p.height < 0.5
    p.colour[joints] = MORTAR


def tube_frame(p, x, y, w, h):
    """A bronze frame round a gauge's glass, with a ring and a stud at
    either end."""
    yc = y - TOP
    outline = inside(x - 7, yc - 5, x + w + 7, yc + h + 5)
    p.colour[outline] = MORTAR
    p.height[outline] = 0
    outer = inside(x - 6, yc - 4, x + w + 6, yc + h + 4) & ~inside(x - 3, yc - 1, x + w + 3, yc + h + 1)
    p.colour[outer] = GOLD
    p.height[outer] += 2.5
    groove = inside(x - 6, yc - 3, x + w + 6, yc - 2) | inside(x - 6, yc + h + 1, x + w + 6, yc + h + 2)
    p.colour[groove] = GOLD * 0.55
    for cx in (x - 10, x + w + 10):  # a ring at either end, and leaves beyond it
        ring = np.abs(np.hypot(ui.xx - cx, ui.yy - (yc + h / 2)) - 6) < 1.4
        p.colour[ring] = GOLD
        p.height[ring] += 3
        side = -1 if cx < x else 1
        for k in (-1, 1):
            leaf = np.abs(np.hypot(ui.xx - (cx + side * 9), ui.yy - (yc + h / 2 + k * 9)) - 7) < 0.9
            leaf &= (ui.xx - cx) * side > 3
            p.colour[leaf] = GOLD * 0.9
            p.height[leaf] += 2
    for cx in (x + 16, x + w - 16):  # bands round the glass
        band = inside(cx - 1, yc - 6, cx + 2, yc + h + 6)
        p.colour[band] = GOLD
        p.height[band] += 3
    p.colour[inside(x - 1, yc - 1, x + w + 1, yc + h + 1)] = GLASS
    p.height[inside(x - 1, yc - 1, x + w + 1, yc + h + 1)] = -3


STONE_DARK = ui.STONE * 0.55


# The crack between the STATUS and ITEM stones, top to bottom, in screen
# pixels. It keeps clear of both buttons' click areas (src/game/panel.cpp)
# and of the original's highlight rectangles, which the stones fill.
CRACK = [(592, 410), (588, 418), (583, 425), (580, 431), (581, 437), (578, 445), (575, 453), (573, 461)]
BAND_TOP = 463  # the carved band below the two stones


def crack_x(y):
    """Where the crack runs at screen row y, with a little wobble."""
    ys = [py for _, py in CRACK]
    xs = [px for px, _ in CRACK]
    return np.interp(y, ys, xs) + 0.8 * np.sin(y * 1.7)


def stones(slate):
    """Masks of the STATUS stone, the ITEM stone and the carved band."""
    yy, xx = ui.yy, ui.xx
    sy = yy + TOP
    cx = crack_x(sy)
    menu = (xx >= MENU_TAB[0] - 1) & (sy < MENU_TAB[1] + MENU_TAB[3] + 1)
    under_band = sy >= BAND_TOP - 1 + 1.2 * np.sin(xx / 7)
    status = slate & (sy >= 403) & (xx < cx - 1) & ~menu & ~under_band & (sy < BAND_TOP - 2)
    item = slate & (sy >= 417) & (xx > cx + 1) & ~under_band & (sy < BAND_TOP - 2)
    band = slate & (sy >= BAND_TOP + 1.2 * np.sin(xx / 7) + 1)
    return status, item, band


def bevel(mask):
    """1 inside a mask, falling off over 2 pixels at its edges."""
    m = mask.astype(float)
    out = m.copy()
    for shift in (1, 2):
        for dy, dx in ((shift, 0), (-shift, 0), (0, shift), (0, -shift)):
            out = np.minimum(out, np.roll(np.roll(m, dy, 0), dx, 1) * (1 - 0.3 * (shift - 1)) + 0.3 * (shift - 1))
    return out * m


def carve(p, band):
    """The band below the stones: a row of carved rings and bars."""
    yy, xx = ui.yy, ui.xx
    mid = BAND_TOP - TOP + 9
    for k, x in enumerate(range(PLAQUE_LEFT[1] + 8, 640, 13)):
        if k % 2 == 0:
            mark = np.abs(np.hypot(xx - x, (yy - mid) * 1.3) - 4) < 1.1
        else:
            mark = (np.abs(xx - x) < 1.1) & (np.abs(yy - mid) < 6)
        mark &= band
        p.colour[mark] = SLATE * 0.4
        p.height[mark] -= 2


def piece_mask(rect, which):
    """A highlight for a clicked button: its stone, white, cut to the
    original's rectangle for that highlight."""
    ui.canvas(640, 80)
    slate = np.ones((80, 640), bool)
    left = np.where(ui.yy < 445 - TOP, PLAQUE_LEFT[0], PLAQUE_LEFT[1]) + 3 * np.sin(ui.yy / 5)
    slate &= ui.xx >= left
    if which == "menu":
        mask = inside(MENU_TAB[0], MENU_TAB[1] - TOP, 640, MENU_TAB[1] - TOP + MENU_TAB[3])
    else:
        status, item, _ = stones(slate)
        mask = status if which == "status" else item
    x, y, w, h = rect
    crop = mask[y - TOP:y - TOP + h, x:x + w]
    out = np.zeros((h, w, 4), np.uint8)
    out[..., :3] = 255
    out[..., 3] = np.where(crop, 255, 0)
    return Image.fromarray(out, "RGBA")


def body():
    """Patterns 7 and 8 together: 640 x 80, from screen row 400."""
    ui.canvas(640, 80)
    p = Panel(41)
    wall(p)
    yy, xx = ui.yy, ui.xx
    # The top of the wall: a straight course of blocks from row 410, with
    # a small notch where two blocks meet.
    alpha = (yy >= 10).astype(float)
    joints = np.where(p.height[13] < 0.5)[0]  # the mortar between the top course's blocks
    for jx in joints:
        for dx, depth in ((-1, 1), (0, 2), (1, 1)):
            if 0 <= jx + dx < 640:
                alpha[10:10 + depth, jx + dx] = 0

    # Smoke-dark stone behind the gauges, its edges ragged.
    ragged = 6 * p.noise(10, 2)
    smoke = (xx > 58 + ragged) & (xx < 336 - ragged) & (yy > 17 + ragged / 2) & (yy < 76 - ragged / 2)
    p.colour[smoke] = (SMOKE * (0.7 + 0.6 * p.noise(14, 3))[..., None])[smoke]
    p.height[smoke] = 0.8 * p.noise(4, 2)[smoke]

    # The slate plaque on the right, broken by cracks into a stone button
    # under each word: STATUS, ITEM, and below them a carved band.
    left = np.where(yy < 445 - TOP, PLAQUE_LEFT[0], PLAQUE_LEFT[1]) + 3 * np.sin(yy / 5)
    slate = (xx >= left) & (yy >= 1)
    tone = 0.6 + 0.7 * p.noise(30, 4) + 0.25 * (p.noise(3, 2) - 0.5)
    p.colour[slate] = (SLATE * tone[..., None])[slate]
    p.height[slate] = 2.0 * p.noise(6, 3)[slate]
    status, item, band = stones(slate)
    for piece, lift in ((status, 4.0), (item, 4.0), (band, 2.0)):
        p.height += lift * bevel(piece)
    cracks = slate & ~(status | item | band)
    cracks &= ~inside(MENU_TAB[0], 0, 640, MENU_TAB[1] - TOP + MENU_TAB[3])
    p.colour[cracks] = SLATE * 0.25
    p.height[cracks] -= 1.5
    carve(p, band)
    edge = slate & ~((xx >= left + 2) & (yy >= 3))
    p.bronze(edge & (xx < 639), 1.5)
    alpha[slate] = 1
    # The MENU tab, a raised bronze-edged plate at the top right.
    mx, my, mw, mh = MENU_TAB
    p.sunken(mx + 2, my - TOP + 1, mx + mw - 1, my - TOP + mh, depth=-1.5, rim=True, floor=SLATE * 0.6)
    alpha[inside(mx, my - TOP - 1, 640, my - TOP + mh + 2)] = 1

    # The left column: a tall bronze plate with a flame cut into it.
    p.sunken(1, 13, 23, 79, depth=1.5, rim=True, floor=BRONZE * 0.45)
    d = np.hypot((xx - 12) / 6, (yy - 52) / 14)
    flame = (d < 1) & (yy > 40 + 6 * np.abs(np.sin((xx - 12) / 3)))
    p.bronze(flame, -1.5)
    p.boss((12, 66), 3.5, 3)
    # Seats for the power button and the level plate.
    px, py, pw, ph = POWER
    p.sunken(px, py - TOP, px + pw, py - TOP + ph, depth=2, rim=True, floor=BRONZE * 0.35)
    alpha[inside(px - 2, py - TOP - 2, px + pw + 2, 80)] = 1
    lx, ly, lw, lh = LEVEL_PLATE
    p.sunken(lx, ly - TOP, lx + lw, ly - TOP + lh, depth=1.5, rim=True, floor=BRONZE * 0.3)

    for tube in (HP_TUBE, MP_TUBE):
        tube_frame(p, *tube)
    # The belt's pockets: a bronze lip round each; their insides are see-through.
    for bx, by in BELT:
        lip = inside(bx, by - TOP, bx + 32, by - TOP + 32) & ~inside(bx + 2, by - TOP + 2, bx + 30, by - TOP + 30)
        p.bronze(lip, 1.5)
    # The MINE button's well.
    mx, my, mw, mh = MINE
    p.sunken(mx, my - TOP, mx + mw, my - TOP + mh, depth=2.5, rim=True, floor=STONE_DARK)

    img = p.image()
    a = np.asarray(img).copy()
    a[..., 3] = (alpha * 255).astype(np.uint8)
    for bx, by in BELT:  # half see-through pockets
        a[by - TOP + 2:by - TOP + 30, bx + 2:bx + 30] = (14, 12, 16, 120)
    img = Image.fromarray(a, "RGBA")

    # Labels and marks.
    gold, grey = (226, 196, 120, 255), (196, 196, 188, 255)
    for k, (bx, by) in enumerate(BELT):
        text(img, bx + 24, by - TOP + 22, str(k + 1), (150, 128, 84, 255))
    text(img, *xy(STATUS_LABEL), "STATUS", grey)
    text(img, *xy(ITEM_LABEL), "ITEM", grey)
    text(img, MENU_TAB[0] + 17, MENU_TAB[1] - TOP + 3, "MENU", gold)
    text(img, LEVEL_PLATE[0] + 5, LEVEL_PLATE[1] - TOP + 11, "LV", gold)
    paste(img, button(RUN, "RUN", True), RUN)
    paste(img, button(WALK, "WALK", True), WALK)
    paste(img, mine(False), MINE)
    paste(img, power("idle"), POWER)
    return img


def xy(point):
    return point[0], point[1] - TOP


def paste(img, part, rect):
    img.alpha_composite(part, (rect[0], rect[1] - TOP))


def button(rect, label, lit):
    """RUN or WALK: a small plate with a lamp, lit or dark, and its name."""
    img = Image.new("RGBA", rect[2:], (0, 0, 0, 0))
    plate = (52, 46, 40, 255) if lit else (34, 30, 28, 255)
    width = min(rect[2], len(label) * 6 + 8)
    for y in range(1, 12):
        for x in range(width):
            img.putpixel((x, y), plate)
    for x in range(width):
        img.putpixel((x, 12), (110, 92, 58, 255))
    lamp = (230, 60, 40, 255) if lit else (70, 30, 26, 255)
    for x, y in ((2, 5), (3, 4), (3, 5), (3, 6), (4, 5), (2, 4), (4, 6), (2, 6), (4, 4)):
        img.putpixel((x - 1, y + 1), lamp)
    text(img, 6, 3, label, (220, 214, 200, 255) if lit else (110, 104, 96, 255))
    return img


def mine(lit):
    """The MINE button's face: a mine's dome on its rim, glowing when one is ready."""
    img = Image.new("RGBA", (16, 16), (0, 0, 0, 0))
    dome = (210, 150, 60, 255) if lit else (96, 90, 82, 255)
    rim = (240, 200, 120, 255) if lit else (130, 124, 112, 255)
    for x in range(3, 13):
        img.putpixel((x, 11), rim)
        img.putpixel((x, 12), (40, 36, 32, 255))
    for y in range(6, 11):
        half = int(round(4.5 * np.sqrt(max(0, 1 - ((10.5 - y) / 5) ** 2))))
        for x in range(8 - half, 8 + half):
            img.putpixel((x, y), dome)
    img.putpixel((7, 4), (250, 90, 40, 255) if lit else (90, 50, 40, 255))
    img.putpixel((8, 4), (250, 90, 40, 255) if lit else (90, 50, 40, 255))
    img.putpixel((7, 5), rim)
    img.putpixel((8, 5), rim)
    return img


def power(state):
    """The increased-power button: a sunburst on a bronze plate, dull,
    lit (blinking when power can be raised) or pressed."""
    w, h = POWER[2], POWER[3]
    img = Image.new("RGBA", (w, h), (0, 0, 0, 0))
    base = {"idle": 0.45, "lit": 1.1, "pressed": 0.7}[state]
    cy, cx = h / 2, w / 2
    y, x = np.mgrid[0:h, 0:w].astype(float)
    a = np.arctan2(y - cy, (x - cx) * 0.8)
    r = np.hypot((x - cx) * 0.8, y - cy)
    ray = (r < 9) & (np.cos(a * 8) > 0.55 - 0.05 * r) | (r < 4)
    plate = np.clip(BRONZE * base * (1.1 - 0.02 * r[..., None]), 0, 255)
    out = np.zeros((h, w, 4), np.uint8)
    inner = (x > 1) & (x < w - 2) & (y > 1) & (y < h - 2)
    out[inner, :3] = (plate * 0.55)[inner].astype(np.uint8)
    out[inner, 3] = 255
    out[ray & inner, :3] = np.clip(plate * 1.5, 0, 255)[ray & inner].astype(np.uint8)
    if state == "pressed":
        out[inner, :3] = (out[inner, :3] * 0.8).astype(np.uint8)
    return Image.fromarray(out, "RGBA")


def tube(colour, kind):
    """A gauge's liquid, 206 x 12: lit from above, with a bright streak
    along the top and darker ends."""
    w, h = 206, 12
    y, x = np.mgrid[0:h, 0:w].astype(float)
    across = 1 - np.abs((y - 4.5) / 6.5) ** 1.6
    shade = 0.35 + 0.85 * np.clip(across, 0, 1)
    shade *= 0.85 + 0.15 * np.clip(np.minimum(x, w - 1 - x) / 10, 0, 1)
    c = np.array(colour, float)[None, None, :] * shade[..., None]
    c += (np.exp(-((y - 2.5) ** 2) / 0.8) * 120)[..., None]  # the streak
    factor = {"full": 1.0, "gain": 1.45, "loss": 0.45}[kind]
    c = np.clip(c * factor, 0, 255)
    out = np.zeros((h, w, 4), np.uint8)
    out[..., :3] = c.astype(np.uint8)
    out[..., 3] = 255
    out[0, :, 3] = out[-1, :, 3] = 0
    out[[1, -2], :, 3] = 160
    return Image.fromarray(out, "RGBA")


def rail_rows(fx, step, rising, low=6, high=2, slope=4):
    """The five rows of a stepped gauge's track at column fx of its frame:
    rows low..low+4 on one side of the step and high..high+4 on the other,
    sloping between over `slope` columns. The EXP rail's stay inside the
    rows the engine shows its gauge in (frame rows 2-10)."""
    t = np.clip((fx - step) / slope, 0, 1)
    if not rising:
        t = 1 - t
    top = int(round(low + (high - low) * t))
    return tuple(range(top, top + 5))


def rail_frame(w, h, step, rising, gauge_x, label, tag_x, ink=(150, 200, 255, 255), label_row=8, bed=(22, 26, 44),
               shape=None, plate=True):
    """A gauge's track: a dark channel in a black outline that steps up
    (or down) once, and its name, beside the low part or under the high
    part."""
    img = Image.new("RGBA", (w, h), (0, 0, 0, 0))
    for fx in range(gauge_x, w):
        rows = rail_rows(fx, step, rising, **(shape or {}))
        for fy in range(rows[0] - 1, rows[-1] + 2):
            if 0 <= fy < h:
                img.putpixel((fx, fy), (0, 0, 0, 255))
        for fy in rows:
            img.putpixel((fx, fy), bed + (255,))
    if not plate:  # the name on its own, with a shadow
        tiny_text(img, tag_x, label_row, label, ink)
        return img
    # A black plate under the name, so it reads like part of the rail.
    for fy in range(label_row - 1, min(h, label_row + 6)):
        for fx in range(max(0, tag_x - 1), min(w, tag_x + 4 * len(label))):
            img.putpixel((fx, fy), (0, 0, 0, 255))
    for k, ch in enumerate(label):
        for yy, row in enumerate(TINY[ch]):
            for xx, bit in enumerate(row):
                if bit == "#":
                    img.putpixel((tag_x + k * 4 + xx, label_row + yy), ink)
    return img


def rail_gauge(w, h, offset, step, rising, colour, pale, start=0, shape=None):
    """What fills a rail_frame's track, from `colour` to `pale` along it."""
    img = Image.new("RGBA", (w, h), (0, 0, 0, 0))
    for gx in range(max(0, start - offset[0]), w):
        rows = rail_rows(gx + offset[0], step, rising, **(shape or {}))
        t = gx / (w - 1)
        c = tuple(int(a + (b - a) * t) for a, b in zip(colour, pale))
        for fy, shade in zip(rows, (1.2, 1.1, 1.0, 0.9, 0.7)):
            gy = fy - offset[1]
            if 0 <= gy < h:
                img.putpixel((gx, gy), tuple(min(255, int(v * shade)) for v in c) + (255,))
    return img


TINY = {"E": ["###", "#..", "##.", "#..", "###"], "X": ["#.#", "#.#", ".#.", "#.#", "#.#"],
        "P": ["##.", "#.#", "##.", "#..", "#.."], "H": ["#.#", "#.#", "###", "#.#", "#.#"],
        "A": [".#.", "#.#", "###", "#.#", "#.#"], "R": ["##.", "#.#", "##.", "#.#", "#.#"],
        "T": ["###", ".#.", ".#.", ".#.", ".#."], "N": ["#.#", "###", "###", "###", "#.#"],
        "C": ["###", "#..", "#..", "#..", "###"], "I": ["###", ".#.", ".#.", ".#.", "###"],
        "V": ["#.#", "#.#", "#.#", "#.#", ".#."], " ": ["...", "...", "...", "...", "..."],
        "-": ["...", "...", "###", "...", "..."]}


def digit(n):
    """A level digit, 6 x 9: the pack's figure with its strokes doubled."""
    img = Image.new("RGBA", (6, 9), (0, 0, 0, 0))
    for row, bits in enumerate(CAPS[str(n)]):
        for col, bit in enumerate(bits):
            if bit == "#":
                for dx in (0, 1):
                    if col + dx < 6:
                        img.putpixel((col + dx, row), (236, 206, 130, 255))
    return img


def tiny_text(img, x, y, word, colour):
    """TINY letters 4 apart with a black shadow below right."""
    for dx, c in ((1, (0, 0, 0, 255)), (0, colour)):
        cx = x
        for ch in word:
            for yy, row in enumerate(TINY[ch]):
                for xx, bit in enumerate(row):
                    if bit == "#" and 0 <= cx + xx + dx < img.width and 0 <= y + yy + dx < img.height:
                        img.putpixel((cx + xx + dx, y + yy + dx), c)
            cx += 4


def mode_label(word, colour):
    """-ACTIVE- or -INACTIVE- over the low end of the companion's gauge."""
    img = Image.new("RGBA", (56, 8), (0, 0, 0, 0))
    word = f"-{word}-"
    tiny_text(img, (56 - len(word) * 4) // 2, 1, word, colour)
    return img


def main():
    out = Path(sys.argv[1])
    panel = body()
    red, blue = (200, 40, 44), (60, 70, 210)
    # (picture, screen x, screen y) by pattern number, as Bar.njp has them.
    parts = [
        (tube(red, "full"), 81, 426), (tube(red, "gain"), 81, 426), (tube(red, "loss"), 81, 426),
        (tube(blue, "full"), 106, 453), (tube(blue, "gain"), 106, 453), (tube(blue, "loss"), 106, 453),
        (Image.new("RGBA", (1, 1), (255, 250, 220, 255)), 0, 0),
        (panel.crop((0, 0, 640, 12)), 0, 400), (panel.crop((0, 12, 640, 80)), 0, 412),
        (mine(True), *MINE[:2]), (button(WALK, "WALK", False), *WALK[:2]), (button(RUN, "RUN", False), *RUN[:2]),
        (power("lit"), *POWER[:2]), (power("pressed"), *POWER[:2]),
        (rail_gauge(109, 10, (15, 2), 50, True, (40, 90, 220), (236, 244, 255), 15), 530, 395),
        (rail_frame(126, 14, 50, True, 14, "EXP", 1, label_row=9, bed=(24, 32, 70)), 515, 393),
        (piece_mask((572, 416, 68, 46), "item"), 572, 416), (piece_mask((583, 401, 57, 13), "menu"), 583, 401),
        (piece_mask((509, 408, 96, 54), "status"), 509, 408),
    ]
    parts += [(digit(n), 0, 0) for n in range(10)]
    # The companion's gauge: its name low on the left, the rail stepping
    # up beyond it; the exe fills it from the right (partner.cpp).
    # The companion's gauge mirrors the EXP rail, with a longer drop: high
    # on the left with its name under it, then down to the right, where
    # ACTIVE or INACTIVE sits over it.
    drop = {"low": 18, "high": 2, "slope": 10}
    parts += [(rail_gauge(109, 26, (1, 0), 52, False, (250, 236, 236), (220, 50, 60), shape=drop), 1, 385),
              (rail_frame(112, 26, 52, False, 0, "PARTNER HP", 4, (255, 110, 110, 255), label_row=10, bed=(80, 22, 28),
                          shape=drop, plate=False), 0, 385),
              (mode_label("ACTIVE", (255, 110, 110, 255)), 58, 393), (mode_label("INACTIVE", (170, 170, 170, 255)), 58, 393)]
    # One sheet: the panel's two pieces on top, the small parts in rows under them.
    width, x, y, row = 640, 0, 80, 0
    places = []
    for img, _, _ in parts:
        if img.width == 640:
            continue
        if x + img.width > width:
            x, y, row = 0, y + row + 2, 0
        places.append((x, y))
        x += img.width + 2
        row = max(row, img.height)
    sheet = Image.new("RGBA", (width, y + row), (0, 0, 0, 0))
    frames, k = [], 0
    for n, (img, sx, sy) in enumerate(parts):
        if img.width == 640:
            px, py = 0, 0 if n == 7 else 12
        else:
            px, py = places[k]
            k += 1
        sheet.alpha_composite(img, (px, py))
        frames.append((px, py, img.width, img.height, -sx, -sy))
    folder = out / "System/Game/Pattern"
    write_sheet(folder, "Bar", sheet, frames, "The game panel, numbered as the original's Bar.njp (see src/game/panel.cpp).")
    sheet_file = folder / "Bar.sheet"
    sheet_file.write_text(sheet_file.read_text().replace("tools/art/ui.py", "tools/art/panel.py"))
    shrink.shrink(out / "System/Game/Pattern" / "Bar.png")
    print("wrote the game panel")


if __name__ == "__main__":
    main()
