#!/usr/bin/env python3
"""The interface pictures for the open content pack.

    python3 tools/art/ui.py <content pack folder> [inventory shop balloon font missions compasses map]

Draws, in the style of the original but from scratch:

- the inventory panel: a 320 x 416 window of dark carved stone in an iron
  frame with bronze trim, with sunken recesses for the equipment slots and
  the 9 x 4 bag, and plates for the gold, the mines' power and stock
  (with a round socket for our mine picture) and the weight with its
  gauge, labelled as the original's are, where the engine writes them
  (src/game/items.cpp, which follows ShadowFlare.exe's item window), and
  the Warehouse Transfer Box, a strongbox in a recess. Also the mine, the
  gauge's marks and the box lit up. Written as
  System/Game/Pattern/Inventory; its anchor puts it at x = 320 on the
  screen.
- the shop window: the same stone and frame with a carved header and the
  9 x 10 cells of goods at (16, 72), as the original's (src/game/talk.cpp).
  System/Game/Pattern/Shop, at x = 0.
- the speech balloon: four 9 x 9 corners (top left, bottom left, top
  right, bottom right; the engine stretches their last row and column into
  the edges), the tail pointing at the speaker and a thought bubble.
  System/Game/Pattern/Hukidasi, the original's name, so the engine finds
  it the same way.
- the font: 6 x 12 cells by byte value on a 16 x 16 grid, white so the
  engine can colour it, with lowercase drawn as small capitals as the
  original's are. System/Common/Pattern/Font00.
- the mission list: a 640 x 415 window with a deep maroon field, the page
  buttons I and II (unlit and lit), the closed and open locks of taken and
  cleared missions and the box a mission's description opens in, where
  the engine puts them (src/game/missions.cpp, after ShadowFlare.exe's).
  System/Game/Pattern/Missions.
- the compass the area's name is shown beside on arrival: a bronze rose
  in a ring, seen at a slant, shrinking over eight frames as the
  original's does. Player/Common/compasses (.anim and .sheet).
- the area map's window: the same stone and iron with a hole the map shows
  through, a gem by the area's name, the hero's arrow and a PLAYER label
  (src/game/area_map.cpp). System/Game/Pattern/Map.

The panels are built as height maps and shaded with a light from the top
left, so frames stand out and recesses sink in, and reduced to 256
colours by shrink.py. Needs numpy and Pillow.
"""

import sys
from pathlib import Path

import numpy as np
from PIL import Image

import shrink

HEADER = "# Drawn by tools/art/ui.py (our own art, see content/README.md).\n"

W, H = 320, 416
LEFT = 320  # where the inventory sits on the 640 x 480 screen
CELL = 32
BAG = (336 - LEFT, 264, 9, 4)  # x, y, columns, rows
# Equipment slots (screen x, y, cells across, down), as in items.cpp.
SLOTS = [(560, 16, 2, 2), (560, 88, 2, 3), (560, 192, 2, 2), (480, 16, 2, 4), (480, 160, 2, 3),
         (400, 143, 1, 1), (440, 143, 1, 1), (400, 183, 1, 1), (440, 183, 1, 1)]
# The totals in the top left, where items.cpp writes them after the
# original's item window (x0, y0, x1, y1 in the panel): the gold, the mine
# socket with the mines' power and stock, and the weight with its gauge.
GOLD_PLATE = (16, 22, 152, 46)
POWER_PLATE = (96, 80, 152, 97)
STOCK_PLATE = (64, 115, 152, 132)
MINE_SOCKET = (40, 108, 22)  # centre and radius; the mine sits at (16, 84), 48 x 48
WEIGHT_PLATE = (96, 222, 152, 239)
GAUGE = (16, 237, 152, 247)  # the groove the 33 marks fill from (18, 239)
LABELS = [("LANDMINE", 18, 68), ("POWER", 64, 85), ("STOCK", 68, 104), ("WEIGHT", 18, 225), ("OVER", 126, 251)]
# The Warehouse Transfer Box (x0, y0, x1, y1 in the panel), as kTransferBox
# in items.cpp: an item let go here goes to the town warehouse.
TRANSFER_BOX = (16, 140, 72, 212)
GOODS = (16, 72, 9, 10)  # the shop's cells: x, y, columns, rows, as in talk.cpp

STONE = np.array([42, 40, 47])
IRON = np.array([50, 48, 49])
BRONZE = np.array([160, 124, 66])
HOLLOW = np.array([24, 20, 24])
MAROON = np.array([34, 14, 16])

yy, xx = np.mgrid[0:H, 0:W].astype(float)


def canvas(w, h):
    """Sets the size the panel helpers draw at (320 x 416 by default)."""
    global W, H, yy, xx
    W, H = w, h
    yy, xx = np.mgrid[0:H, 0:W].astype(float)


def box(x0, y0, x1, y1, bevel):
    """1 inside the rectangle, falling to 0 over `bevel` pixels at its edges."""
    d = np.minimum.reduce([xx - x0, x1 - 1 - xx, yy - y0, y1 - 1 - yy])
    return np.clip((d + 1) / bevel, 0, 1)


def inside(x0, y0, x1, y1):
    return (xx >= x0) & (xx < x1) & (yy >= y0) & (yy < y1)


class Panel:
    """A 320 x 416 window of carved stone in an iron frame."""

    def __init__(self, seed):
        self.rng = np.random.default_rng(seed)
        self.height = np.zeros((H, W))
        self.colour = np.zeros((H, W, 3))
        # Carved stone: blotchy, with fine grain and darker cracks.
        blotch, grain, vein = self.noise(48), self.noise(6, 2), self.noise(24, 3)
        shade = 0.8 + 0.4 * blotch + 0.15 * (grain - 0.5)
        cracks = np.clip(1 - np.abs(vein - 0.5) * 40, 0, 1) * 0.35
        self.colour[:] = STONE * (shade - cracks)[..., None]
        self.height += 0.6 * grain + 0.8 * blotch - 2 * cracks

        # The iron frame round the edge, with a bronze line just inside it.
        frame = 1 - box(6, 6, W - 6, H - 6, 1)
        iron = IRON * (0.75 + 0.5 * self.noise(10, 3))[..., None]
        self.colour = np.where(frame[..., None] > 0, iron, self.colour)
        self.height += 6 * frame * box(0, 0, W, H, 4)
        trim = inside(7, 7, W - 7, H - 7) & ~inside(8, 8, W - 8, H - 8)
        self.colour[trim] = BRONZE * 0.9
        self.height[trim] += 2.5
        # Studs along the frame and bigger bosses in the corners.
        studs = [(x, 3) for x in range(26, W - 20, 36)] + [(x, H - 4) for x in range(26, W - 20, 36)]
        studs += [(3, y) for y in range(30, H - 20, 36)] + [(W - 4, y) for y in range(30, H - 20, 36)]
        for c, r in [(s, 2.0) for s in studs] + [((5, 5), 4.5), ((W - 6, 5), 4.5), ((5, H - 6), 4.5), ((W - 6, H - 6), 4.5)]:
            self.boss(c, r)

    def noise(self, scale, octaves=4):
        """Smooth value noise in 0..1, summed over octaves."""
        total = np.zeros((H, W))
        amp, norm = 1.0, 0.0
        for o in range(octaves):
            step = max(1, int(scale / 2 ** o))
            grid = self.rng.random((H // step + 2, W // step + 2))
            small = Image.fromarray((grid * 255).astype(np.uint8))
            big = small.resize(((W // step + 2) * step, (H // step + 2) * step), Image.BICUBIC)
            total += amp * np.asarray(big, dtype=float)[:H, :W] / 255
            norm += amp
            amp *= 0.5
        return total / norm

    def boss(self, centre, r, height=5):
        """A round bronze stud."""
        d = np.hypot(xx - centre[0], yy - centre[1])
        bump = np.clip(1 - (d / r) ** 2, 0, 1)
        self.height += height * np.sqrt(bump)
        self.colour = np.where((bump > 0)[..., None], BRONZE * (0.7 + 0.3 * bump)[..., None], self.colour)

    def sunken(self, x0, y0, x1, y1, depth=4, rim=True, floor=HOLLOW):
        if rim:  # a raised bronze lip round the hole
            lip = inside(x0 - 2, y0 - 2, x1 + 2, y1 + 2) & ~inside(x0, y0, x1, y1)
            self.colour[lip] = BRONZE * 0.75
            self.height[lip] += 1.5
        hole = box(x0, y0, x1, y1, 2.5)
        self.height -= depth * hole
        dark = floor * (0.8 + 0.4 * self.noise(16, 2))[..., None]
        self.colour = np.where((hole > 0)[..., None], dark, self.colour)

    def cells(self, x0, y0, cols, rows, colour, raised=3):
        """Thin raised ribs between the cells of a sunken grid."""
        for c in range(1, cols):
            x = x0 + c * CELL
            rib = inside(x - 1, y0, x + 1, y0 + rows * CELL)
            self.height[rib] += raised
            self.colour[rib] = colour
        for r in range(1, rows):
            y = y0 + r * CELL
            rib = inside(x0, y - 1, x0 + cols * CELL, y + 1)
            self.height[rib] += raised
            self.colour[rib] = colour

    def bronze(self, mask, raised=2.0):
        self.colour[mask] = BRONZE * 0.85
        self.height[mask] += raised

    def image(self):
        # Light from the top left.
        gy, gx = np.gradient(self.height)
        light = np.clip(1.0 - 0.18 * gx - 0.18 * gy, 0.35, 1.6)
        image = np.clip(self.colour * light[..., None], 0, 255).astype(np.uint8)
        alpha = np.full((H, W, 1), 255, dtype=np.uint8)
        return Image.fromarray(np.concatenate([image, alpha], axis=2), "RGBA")


def write_sheet(folder, name, image, frames, comment):
    """Saves `image` and a .sheet listing its frames (x, y, w, h, ox, oy)."""
    folder.mkdir(parents=True, exist_ok=True)
    image.save(folder / f"{name}.png", optimize=True)
    with open(folder / f"{name}.sheet", "w", newline="\n") as f:
        f.write(HEADER)
        f.write(f"# {comment}\n")
        f.write(f"image={name}.png\n")
        for fr in frames:
            f.write("frame=" + ",".join(str(v) for v in fr) + "\n")


def engrave(p, text, x0, y0, colour=None):
    """Cuts `text` into the panel in our font (lowercase as small
    capitals on the same baseline), filled with bronze."""
    colour = BRONZE * 0.8 if colour is None else colour
    for i, ch in enumerate(text):
        rows, top = (CAPS[ch], 0) if ch in CAPS else (SMALL[ch], 2) if ch in SMALL else ([], 0)
        for y, row in enumerate(rows):
            for x, bit in enumerate(row):
                if bit == "#":
                    p.colour[y0 + top + y, x0 + i * 6 + x] = colour
                    p.height[y0 + top + y, x0 + i * 6 + x] -= 1.5


def strongbox(p):
    """The Warehouse Transfer Box: a bronze-rimmed recess holding a small
    oak strongbox seen from the front, its lid domed, two iron straps
    round it and a bronze lock plate with a keyhole."""
    x0, y0, x1, y1 = TRANSFER_BOX
    p.sunken(x0, y0, x1, y1, depth=4)
    cx = (x0 + x1) / 2
    bx0, bx1, top, lid, bottom = cx - 20, cx + 20, y0 + 14, y0 + 30, y1 - 10
    oak = np.array([104, 66, 36])
    grain = 0.8 + 0.35 * p.noise(5, 2)
    # Planks across the body, a darker seam between each.
    body = inside(bx0, lid, bx1, bottom)
    seam = body & (((yy - lid) % 9) < 1)
    p.colour[body] = (oak * grain[..., None])[body]
    p.colour[seam] *= 0.55
    p.height[body] += 5
    p.height[seam] -= 1
    # The domed lid, its edge overhanging the body a little.
    d = np.clip(1 - ((xx - cx) / 21) ** 2, 0, 1)
    dome = (yy >= lid - (lid - top) * np.sqrt(d)) & (yy < lid + 1) & (np.abs(xx - cx) < 22)
    p.colour[dome] = (oak * 1.1 * grain[..., None])[dome]
    p.height[dome] += 5 + 3 * np.sqrt(d)[dome]
    rim = inside(bx0 - 2, lid - 1, bx1 + 2, lid + 3)
    p.colour[rim] = IRON * 1.3
    p.height[rim] += 7
    # Iron straps down the body and over the lid, with rivets.
    for sx in (bx0 + 7, bx1 - 9):
        strap = (inside(sx, top - 2, sx + 3, bottom) & (body | dome | rim))
        p.colour[strap] = IRON * 1.4
        p.height[strap] += 1.5
        for ry in range(int(lid + 6), int(bottom) - 2, 9):
            p.boss((sx + 1.5, ry), 1.2, height=1.5)
    base = inside(bx0 - 1, bottom - 3, bx1 + 1, bottom)
    p.colour[base] = IRON * 1.2
    p.height[base] += 6
    # The lock plate and keyhole.
    plate = inside(cx - 6, lid + 1, cx + 6, lid + 15)
    p.bronze(plate, 2.5)
    key = (np.hypot(xx - cx, yy - (lid + 6)) < 1.8) | inside(cx - 0.5, lid + 6, cx + 1, lid + 11)
    p.colour[key] = HOLLOW * 0.5
    p.height[key] -= 2


def lit_box(panel):
    """The box lit up, shown fading in while an item is held over it: the
    panel's own picture of it, brighter and warmer."""
    x0, y0, x1, y1 = TRANSFER_BOX
    crop = np.asarray(panel.crop((x0, y0, x1, y1))).astype(float)
    rgb = np.clip(crop[..., :3] * np.array([1.7, 1.5, 1.1]) + np.array([40, 30, 10]), 0, 255).astype(np.uint8)
    alpha = np.full(rgb.shape[:2] + (1,), 255, dtype=np.uint8)
    return Image.fromarray(np.concatenate([rgb, alpha], axis=2), "RGBA")


def mine_picture():
    """The mine shown in the socket while any are carried: a squat iron
    drum seen from above, banded in bronze, with a pressure cap in the
    middle and six studs round its rim."""
    n = 48
    gy, gx = np.mgrid[0:n, 0:n].astype(float)
    d = np.hypot(gx - 23.5, gy - 23.5)
    body = d < 19
    height = np.where(body, np.sqrt(np.clip(1 - (d / 19) ** 2, 0, 1)) * 8, 0)
    colour = np.zeros((n, n, 3)) + IRON * 1.2
    band = (d > 12) & (d < 14.5)
    colour[band] = BRONZE * 0.75
    height[band] += 1
    cap = d < 6
    colour[cap] = IRON * 1.6
    height += np.where(cap, 3 * np.sqrt(np.clip(1 - (d / 6) ** 2, 0, 1)), 0)
    for k in range(6):
        a = k * np.pi / 3 + np.pi / 6
        sd = np.hypot(gx - (23.5 + 16 * np.cos(a)), gy - (23.5 + 16 * np.sin(a)))
        stud = sd < 2.2
        colour[stud] = BRONZE
        height += np.where(stud, 2 * np.sqrt(np.clip(1 - (sd / 2.2) ** 2, 0, 1)), 0)
    dy, dx = np.gradient(height)
    light = np.clip(1.0 - 0.3 * dx - 0.3 * dy, 0.3, 1.7)
    rgb = np.clip(colour * light[..., None], 0, 255).astype(np.uint8)
    alpha = np.where(d < 19.5, 255, 0).astype(np.uint8)[..., None]
    return Image.fromarray(np.concatenate([rgb, alpha], axis=2), "RGBA")


# The weight gauge's marks: light, darker and red as it fills, and the
# white it flashes as it starts to move.
GAUGE_MARKS = [(222, 190, 140), (182, 148, 92), (176, 30, 24), (255, 255, 255)]


def inventory(out):
    p = Panel(7)
    for sx, sy, cw, ch in SLOTS:
        p.sunken(sx - LEFT, sy, sx - LEFT + cw * CELL, sy + ch * CELL)
    bx, by, cols, rows = BAG
    p.sunken(bx, by, bx + cols * CELL, by + rows * CELL)
    p.cells(bx, by, cols, rows, STONE * 0.9)
    # Plates for the totals: shallow, smooth and dark.
    for x0, y0, x1, y1 in (GOLD_PLATE, POWER_PLATE, STOCK_PLATE, WEIGHT_PLATE):
        p.sunken(x0, y0, x1, y1, depth=1.5, rim=False, floor=STONE * 0.6)
    x0, y0, x1, y1 = GAUGE
    p.sunken(x0, y0, x1, y1, depth=2, floor=HOLLOW)
    # The round socket the mine sits in, ringed in bronze.
    cx, cy, r = MINE_SOCKET
    d = np.hypot(xx - cx, yy - cy)
    ring = (d >= r) & (d < r + 2)
    p.bronze(ring, 1.5)
    hollow = np.clip((r - d) / 3, 0, 1)
    p.height -= 4 * hollow
    p.colour = np.where((hollow > 0)[..., None], HOLLOW * (0.8 + 0.4 * p.noise(16, 2))[..., None], p.colour)
    strongbox(p)
    for text, x, y in LABELS:
        engrave(p, text, x, y)
    panel = p.image()
    bw, bh = TRANSFER_BOX[2] - TRANSFER_BOX[0], TRANSFER_BOX[3] - TRANSFER_BOX[1]
    sheet = Image.new("RGBA", (W, H + max(48, bh)), (0, 0, 0, 0))
    sheet.paste(panel, (0, 0))
    sheet.paste(mine_picture(), (0, H))
    frames = [(0, 0, W, H, -LEFT, 0), (0, H, 48, 48, -(LEFT + MINE_SOCKET[0] - 24), -(MINE_SOCKET[1] - 24))]
    for k, rgb in enumerate(GAUGE_MARKS):
        mark = Image.new("RGBA", (3, 5), rgb + (255,))
        # A darker lower edge, so the marks read as raised.
        for x in range(3):
            mark.putpixel((x, 4), tuple(int(c * 0.7) for c in rgb) + (255,))
        sheet.paste(mark, (52 + k * 5, H))
        frames.append((52 + k * 5, H, 3, 5, 0, 0))
    sheet.paste(lit_box(panel), (80, H))
    frames.append((80, H, bw, bh, -(LEFT + TRANSFER_BOX[0]), -TRANSFER_BOX[1]))
    write_sheet(out / "System/Game/Pattern", "Inventory", sheet, frames,
                "The inventory panel (the anchor puts it at x = 320 on the 640 x 480 screen), the mine in its "
                "socket, the weight gauge's marks (light, darker, red, flash) and the Warehouse Transfer Box "
                "lit up.")


def shop(out):
    p = Panel(8)
    # The header: a carved band with a frieze of bronze scrolls along its
    # top, a round boss in the middle and a plate either side with a
    # rosette at each end and a running vine.
    p.sunken(14, 12, W - 14, 60, depth=1.5, rim=True, floor=STONE * 0.7)
    centre = inside(W // 2 - 28, 12, W // 2 + 28, 60)
    frieze = inside(20, 15, W - 20, 27) & ~centre
    for k in range(0, W, 12):  # S-scrolls: half circles, alternately up and down
        cx, up = k + 6, (k // 12) % 2 == 0
        d = np.hypot(xx - cx, yy - 21)
        arc = (np.abs(d - 4.5) < 0.8) & ((yy < 21) if up else (yy > 21))
        p.bronze(arc & frieze, 2.0)
        p.boss((k, 21), 1.0, 1.5) if 24 < k < W - 24 and abs(k - W // 2) > 30 else None
    rule = (np.abs(yy - 28.5) < 0.6) & inside(20, 0, W - 20, H) & ~centre
    p.bronze(rule, 1.5)
    for x0 in (22, W - 22 - 96):
        p.sunken(x0, 33, x0 + 96, 54, depth=1.0, rim=True, floor=STONE * 0.55)
        for rx in (x0 + 8, x0 + 88):
            p.boss((rx, 43.5), 3.4, 3)
            p.boss((rx, 43.5), 1.3, -1.5)
        t = (xx - x0 - 16) / 64 * np.pi * 4
        vine = (np.abs(yy - (43.5 + 4 * np.sin(t))) < 0.8) & inside(x0 + 15, 35, x0 + 81, 53)
        p.bronze(vine, 1.5)
        for k in range(8):
            lx = x0 + 16 + (k + 0.5) * 8
            ly = 43.5 + 4 * np.sin((lx - x0 - 16) / 64 * np.pi * 4)
            side = -1 if k % 2 else 1
            p.bronze(((xx - lx) / 1.6) ** 2 + ((yy - ly - side * 3.2) / 2.4) ** 2 < 1, 1.2)
    p.sunken(W // 2 - 26, 14, W // 2 + 26, 58, depth=1.0, rim=False, floor=STONE * 0.8)
    p.boss((W // 2, 36), 16, 7)
    p.boss((W // 2, 36), 10, -3)
    for a in range(8):  # a ring of studs round the boss
        p.boss((W // 2 + 13 * np.cos(a * np.pi / 4), 36 + 13 * np.sin(a * np.pi / 4)), 1.6, 1.5)
    p.boss((W // 2, 36), 4.5, 4)
    # The goods: a deep recess of dark red cloth with bronze ribs.
    gx, gy, cols, rows = GOODS
    p.sunken(gx, gy, gx + cols * CELL, gy + rows * CELL, floor=MAROON)
    p.cells(gx, gy, cols, rows, BRONZE * 0.7, raised=2)
    write_sheet(out / "System/Game/Pattern", "Shop", p.image(), [(0, 0, W, H, 0, 0)],
                "The shop window, on the left of the 640 x 480 screen.")


INK = (0, 0, 0, 255)
PAPER = (248, 248, 248, 255)
SHADE = (196, 196, 204, 255)


def balloon(out):
    """Corners of a round white balloon with a black edge and a grey
    shadow inside its lower right; the tail and a thought bubble."""
    n = 9
    sheet = Image.new("RGBA", (n * 4 + 14 + 15, 16), (0, 0, 0, 0))
    r = 7.5  # corner radius, about the pixel centres
    for k, (left, top) in enumerate([(True, True), (True, False), (False, True), (False, False)]):
        for y in range(n):
            for x in range(n):
                # Distance from the corner's circle centre, mirrored so
                # every corner is the top left one.
                mx = x if left else n - 1 - x
                my = y if top else n - 1 - y
                d = np.hypot(max(0, r - mx), max(0, r - my))
                if d > r + 0.5:
                    continue
                c = INK if d > r - 0.6 else PAPER
                if c == PAPER and (not top or not left) and d > r - 1.6:
                    c = SHADE
                sheet.putpixel((k * n + x, y), c)
    # The tail: a curved wedge from the balloon's bottom edge to a point
    # down and to the left, black edged, its top row open to join the
    # balloon.
    tx = n * 4
    for y in range(14):
        t = y / 13
        left, right = 3 - 2 * t, 11 - 10 * t ** 0.7
        for x in range(14):
            if left - 1 <= x <= right + 1:
                edge = (x < left or x > right or y == 13) and y > 0
                c = INK if edge else (SHADE if x > right - 1.5 and y > 0 else PAPER)
                sheet.putpixel((tx + x, y), c)
    # A thought bubble: three shrinking ovals.
    bx = tx + 14
    for cx, cy, rx, ry in ((9.0, 4.0, 5.6, 3.6), (4.0, 10.6, 3.2, 2.4), (1.5, 14.3, 1.6, 1.6)):
        for y in range(16):
            for x in range(15):
                d = ((x - cx) / rx) ** 2 + ((y - cy) / ry) ** 2
                if d <= 1.0:
                    edge = ((x - cx) / (rx - 1)) ** 2 + ((y - cy) / (ry - 1)) ** 2 > 1.0 if rx > 1.5 else d > 0.3
                    sheet.putpixel((bx + x, y), INK if edge else PAPER)
    frames = [(k * n, 0, n, n, 0, 0) for k in range(4)] + [(tx, 0, 14, 14, 0, 0), (bx, 0, 15, 16, -1, -1)]
    write_sheet(out / "System/Game/Pattern", "Hukidasi", sheet, frames,
                "The speech balloon: corners (top left, bottom left, top right, bottom right), tail, thought bubble.")


# Capitals and figures are 5 x 8, from the cell's second row; lowercase
# letters are small capitals, 5 x 6 on the same baseline.
CAPS = {
'A': [".###.","#...#","#...#","#...#","#####","#...#","#...#","#...#"],
'B': ["####.","#...#","#...#","####.","#...#","#...#","#...#","####."],
'C': [".###.","#...#","#....","#....","#....","#....","#...#",".###."],
'D': ["###..","#..#.","#...#","#...#","#...#","#...#","#..#.","###.."],
'E': ["#####","#....","#....","####.","#....","#....","#....","#####"],
'F': ["#####","#....","#....","####.","#....","#....","#....","#...."],
'G': [".###.","#...#","#....","#....","#.###","#...#","#...#",".####"],
'H': ["#...#","#...#","#...#","#####","#...#","#...#","#...#","#...#"],
'I': [".###.","..#..","..#..","..#..","..#..","..#..","..#..",".###."],
'J': ["..###","...#.","...#.","...#.","...#.","#..#.","#..#.",".##.."],
'K': ["#...#","#..#.","#.#..","##...","#.#..","#..#.","#...#","#...#"],
'L': ["#....","#....","#....","#....","#....","#....","#....","#####"],
'M': ["#...#","##.##","#.#.#","#.#.#","#...#","#...#","#...#","#...#"],
'N': ["#...#","##..#","##..#","#.#.#","#.#.#","#..##","#..##","#...#"],
'O': [".###.","#...#","#...#","#...#","#...#","#...#","#...#",".###."],
'P': ["####.","#...#","#...#","#...#","####.","#....","#....","#...."],
'Q': [".###.","#...#","#...#","#...#","#...#","#.#.#","#..#.",".##.#"],
'R': ["####.","#...#","#...#","#...#","####.","#.#..","#..#.","#...#"],
'S': [".###.","#...#","#....",".###.","....#","....#","#...#",".###."],
'T': ["#####","..#..","..#..","..#..","..#..","..#..","..#..","..#.."],
'U': ["#...#","#...#","#...#","#...#","#...#","#...#","#...#",".###."],
'V': ["#...#","#...#","#...#","#...#",".#.#.",".#.#.","..#..","..#.."],
'W': ["#...#","#...#","#...#","#.#.#","#.#.#","#.#.#","##.##","#...#"],
'X': ["#...#","#...#",".#.#.","..#..","..#..",".#.#.","#...#","#...#"],
'Y': ["#...#","#...#",".#.#.","..#..","..#..","..#..","..#..","..#.."],
'Z': ["#####","....#","...#.","..#..",".#...","#....","#....","#####"],
'0': [".###.","#...#","#..##","#.#.#","#.#.#","##..#","#...#",".###."],
'1': ["..#..",".##..","..#..","..#..","..#..","..#..","..#..",".###."],
'2': [".###.","#...#","....#","...#.","..#..",".#...","#....","#####"],
'3': [".###.","#...#","....#","..##.","....#","....#","#...#",".###."],
'4': ["...#.","..##.",".#.#.","#..#.","#####","...#.","...#.","...#."],
'5': ["#####","#....","####.","....#","....#","....#","#...#",".###."],
'6': ["..##.",".#...","#....","####.","#...#","#...#","#...#",".###."],
'7': ["#####","....#","...#.","...#.","..#..","..#..","..#..","..#.."],
'8': [".###.","#...#","#...#",".###.","#...#","#...#","#...#",".###."],
'9': [".###.","#...#","#...#","#...#",".####","....#","...#.",".##.."],
'!': ["..#..","..#..","..#..","..#..","..#..",".....","..#..","..#.."],
'"': [".#.#.",".#.#.",".#.#.",".....",".....",".....",".....","....."],
'#': [".....",".#.#.","#####",".#.#.",".#.#.","#####",".#.#.","....."],
'$': ["..#..",".####","#.#..",".###.","..#.#","####.","..#..","....."],
'%': ["##...","##..#","...#.","..#..",".#...","#..##","...##","....."],
'&': [".##..","#..#.","#.#..",".#...","#.#.#","#..#.","#..#.",".##.#"],
"'": ["..#..","..#..",".#...",".....",".....",".....",".....","....."],
'(': ["...#.","..#..",".#...",".#...",".#...",".#...","..#..","...#."],
')': [".#...","..#..","...#.","...#.","...#.","...#.","..#..",".#..."],
'*': [".....","..#..","#.#.#",".###.","#.#.#","..#..",".....","....."],
'+': [".....","..#..","..#..","#####","..#..","..#..",".....","....."],
',': [".....",".....",".....",".....",".....","..#..","..#..",".#..."],
'-': [".....",".....",".....","#####",".....",".....",".....","....."],
'.': [".....",".....",".....",".....",".....",".....",".##..",".##.."],
'/': ["....#","....#","...#.","..#..","..#..",".#...","#....","#...."],
':': [".....",".##..",".##..",".....",".....",".##..",".##..","....."],
';': [".....",".##..",".##..",".....",".....",".##..","..#..",".#..."],
'<': ["...#.","..#..",".#...","#....",".#...","..#..","...#.","....."],
'=': [".....",".....","#####",".....","#####",".....",".....","....."],
'>': [".#...","..#..","...#.","....#","...#.","..#..",".#...","....."],
'?': [".###.","#...#","....#","...#.","..#..","..#..",".....","..#.."],
'@': [".###.","#...#","#.###","#.#.#","#.###","#....","#...#",".###."],
'[': [".###.",".#...",".#...",".#...",".#...",".#...",".#...",".###."],
'\\':["#....","#....",".#...","..#..","..#..","...#.","....#","....#"],
']': [".###.","...#.","...#.","...#.","...#.","...#.","...#.",".###."],
'^': ["..#..",".#.#.","#...#",".....",".....",".....",".....","....."],
'_': [".....",".....",".....",".....",".....",".....",".....","#####"],
'`': [".#...","..#..",".....",".....",".....",".....",".....","....."],
'{': ["...##","..#..","..#..",".#...","..#..","..#..","..#..","...##"],
'|': ["..#..","..#..","..#..","..#..","..#..","..#..","..#..","..#.."],
'}': ["##...","..#..","..#..","...#.","..#..","..#..","..#..","##..."],
'~': [".....",".....",".#...","#.#.#","...#.",".....",".....","....."],
'\xa5': [".....",".....",".....","..#..",".###.","..#..",".....","....."],
}
SMALL = {
'a': [".###.","#...#","#...#","#####","#...#","#...#"],
'b': ["####.","#...#","####.","#...#","#...#","####."],
'c': [".####","#....","#....","#....","#....",".####"],
'd': ["###..","#..#.","#...#","#...#","#..#.","###.."],
'e': ["#####","#....","####.","#....","#....","#####"],
'f': ["#####","#....","####.","#....","#....","#...."],
'g': [".####","#....","#.###","#...#","#...#",".###."],
'h': ["#...#","#...#","#####","#...#","#...#","#...#"],
'i': [".###.","..#..","..#..","..#..","..#..",".###."],
'j': ["..###","...#.","...#.","...#.","#..#.",".##.."],
'k': ["#...#","#..#.","###..","#..#.","#...#","#...#"],
'l': ["#....","#....","#....","#....","#....","#####"],
'm': ["#...#","##.##","#.#.#","#...#","#...#","#...#"],
'n': ["#...#","##..#","#.#.#","#.#.#","#..##","#...#"],
'o': [".###.","#...#","#...#","#...#","#...#",".###."],
'p': ["####.","#...#","#...#","####.","#....","#...."],
'q': [".###.","#...#","#...#","#.#.#","#..#.",".##.#"],
'r': ["####.","#...#","####.","#.#..","#..#.","#...#"],
's': [".####","#....",".###.","....#","....#","####."],
't': ["#####","..#..","..#..","..#..","..#..","..#.."],
'u': ["#...#","#...#","#...#","#...#","#...#",".###."],
'v': ["#...#","#...#",".#.#.",".#.#.","..#..","..#.."],
'w': ["#...#","#...#","#.#.#","#.#.#","##.##","#...#"],
'x': ["#...#",".#.#.","..#..","..#..",".#.#.","#...#"],
'y': ["#...#",".#.#.","..#..","..#..","..#..","..#.."],
'z': ["#####","...#.","..#..",".#...","#....","#####"],
}


# The mission list, as laid out in src/game/missions.cpp.
MISSION_FIELD = (32, 40, 608, 380)  # x0, y0, x1, y1 of the field the rows sit on
PAGE_BUTTONS = ((276, 10), (344, 10))
DESCRIPTION = (172, 76, 296, 260)  # x, y, w, h
PLUM = np.array([34, 10, 48])


def numeral(img, x0, y0, strokes, colour):
    """Roman numeral strokes (I, II) 2 pixels wide with serifs."""
    for k in range(strokes):
        x = x0 + k * 5
        for y in range(y0, y0 + 11):
            for dx in (0, 1):
                img.putpixel((x + dx, y), colour)
        for dx in (-1, 2):
            img.putpixel((x + dx, y0), colour)
            img.putpixel((x + dx, y0 + 10), colour)


def page_button(lit, strokes):
    canvas(20, 23)
    p = Panel(20 + strokes + 2 * lit)
    p.colour[:] = (BRONZE * (0.55 if lit else 0.4))
    p.height[:] = 0
    p.sunken(3, 3, 17, 20, depth=2, rim=False, floor=STONE * (0.75 if lit else 0.5))
    img = p.image()
    ink = (236, 214, 150, 255) if lit else (150, 128, 84, 255)
    numeral(img, 10 - 2 - (strokes - 1) * 2 - (0 if strokes == 1 else 1), 6, strokes, ink)
    return img


def lock(open_):
    """A padlock 13 x 18 (closed) or 18 x 18 (open, its shackle swung up and
    out to the right), bronze with a dark keyhole."""
    w = 18 if open_ else 13
    img = Image.new("RGBA", (w, 18), (0, 0, 0, 0))
    body = (1, 8, 12, 17)
    for y in range(body[1], body[3] + 1):
        for x in range(body[0], body[2] + 1):
            edge = x in (body[0], body[2]) or y in (body[1], body[3])
            shade = 1.0 - 0.35 * (y - body[1]) / 9 - (0.25 if x > 8 else 0)
            c = (BRONZE * shade * (0.6 if edge else 1)).astype(int)
            img.putpixel((x, y), (*c, 255))
    for x, y in ((6, 11), (6, 12), (6, 13), (5, 11), (7, 11)):
        img.putpixel((x, y), (20, 14, 10, 255))
    # The shackle: a 2-pixel arch, set in the body or lifted out of it.
    cx, top, half = (6.5, 1, 4) if not open_ else (11.5, 0, 4)
    for a in np.linspace(0, np.pi, 40):
        for r in (half - 0.5, half + 0.5):
            x, y = cx - r * np.cos(a), top + half - r * np.sin(a)
            img.putpixel((int(round(x)), int(round(y))), (120, 112, 104, 255))
    legs = [(cx - half, top + half, 8), (cx + half, top + half, 8)] if not open_ else [(cx + half, top + half, 6)]
    for lx, y0, y1 in legs:
        for y in range(int(y0), y1):
            for dx in (-0.5, 0.5):
                img.putpixel((int(round(lx + dx)), y), (120, 112, 104, 255))
    return img


def description_box():
    x, y, w, h = DESCRIPTION
    canvas(w, h)
    p = Panel(30)
    p.sunken(8, 8, w - 8, h - 8, depth=3, rim=True, floor=PLUM)
    return p.image()


def missions(out):
    canvas(640, 415)
    p = Panel(9)
    x0, y0, x1, y1 = MISSION_FIELD
    p.sunken(x0, y0, x1, y1, depth=4, floor=MAROON * 1.15)
    # The page buttons sit in a carved plate either side of a boss.
    p.sunken(262, 6, 378, 37, depth=1.5, rim=True, floor=STONE * 0.7)
    p.boss((W // 2, 21), 9, 5)
    p.boss((W // 2, 21), 4, -2)
    # A bronze rule along the field's top and bottom with studs.
    for y in (y0 - 9, y1 + 8):
        p.bronze((np.abs(yy - y) < 0.8) & inside(x0 + 20, 0, x1 - 20, H), 1.5)
        for x in range(x0 + 40, x1 - 30, 64):
            p.boss((x, y), 2.2, 2)
    window = p.image()
    parts = [window]
    parts += [page_button(False, 1), page_button(False, 2), page_button(True, 1), page_button(True, 2)]
    parts += [lock(False), lock(True), description_box()]
    canvas(320, 416)
    # One sheet: the window on top, the small parts in a row under it.
    sheet = Image.new("RGBA", (640, 415 + 260), (0, 0, 0, 0))
    sheet.paste(window, (0, 0))
    frames = [(0, 0, 640, 415, 0, 0)]
    x = 0
    anchors = [(-PAGE_BUTTONS[0][0], -PAGE_BUTTONS[0][1]), (-PAGE_BUTTONS[1][0], -PAGE_BUTTONS[1][1])] * 2
    anchors += [(-5, -3), (-5, -3), (-DESCRIPTION[0], -DESCRIPTION[1])]
    for img, (ox, oy) in zip(parts[1:], anchors):
        sheet.paste(img, (x, 415))
        frames.append((x, 415, img.width, img.height, ox, oy))
        x += img.width + 2
    write_sheet(out / "System/Game/Pattern", "Missions", sheet, frames,
                "The mission list: window, page I and II unlit, then lit, closed lock, open lock, description box.")


def compasses(out):
    """A bronze compass rose in a ring, flattened to the ground's slant,
    drawn large once and shrunk for each of the eight frames: from 170 x
    124 to 100 x 73 about the same centre (52, 37) on the screen."""
    S = 4 * 180  # drawn four times over and shrunk, for smooth edges
    y, x = (np.mgrid[0:S, 0:S].astype(float) - S / 2) / (S / 2)
    r, a = np.hypot(x, y), np.arctan2(y, x)
    rgb = np.zeros((S, S, 3))
    alpha = np.zeros((S, S))
    # The ring: bronze, with a darker groove and eight studs.
    ring = (r > 0.80) & (r < 0.97)
    groove = np.abs(r - 0.885) < 0.012
    rgb[ring] = BRONZE * (0.8 + 0.25 * np.cos(a[ring] + 2.4))[:, None]
    rgb[groove] = BRONZE * 0.45
    alpha[ring] = 1
    for k in range(8):
        ang = k * np.pi / 4 + np.pi / 8
        d = np.hypot(x - 0.885 * np.cos(ang), y - 0.885 * np.sin(ang))
        stud = d < 0.045
        rgb[stud] = BRONZE * (1.2 - 6 * d[stud])[:, None]
    # Inside the ring: dark stone, faintly blue, so the rose stands out.
    disc = r <= 0.80
    rgb[disc] = np.array([30, 34, 44]) * (1.0 - 0.3 * r[disc])[:, None]
    alpha[disc] = 0.85
    # The rose: four long points and four short ones, each lit on one side.
    # North points up and right, along the map's north as the original's
    # needle does: 45 degrees here, about 36 once flattened.
    for k in range(8):
        ang = k * np.pi / 4 - np.pi / 4
        length = 0.78 if k % 2 == 0 else 0.48
        width = 0.13 if k % 2 == 0 else 0.10
        along = x * np.cos(ang) + y * np.sin(ang)
        across = -x * np.sin(ang) + y * np.cos(ang)
        point = (along > 0) & (np.abs(across) < width * (1 - along / length))
        lit = across > 0
        colour = np.where(lit[..., None], BRONZE * 1.15, BRONZE * 0.6)
        if k == 0:  # north is pale silver
            colour = np.where(lit[..., None], np.array([230, 226, 214]), np.array([140, 138, 132]))
        rgb = np.where(point[..., None], colour, rgb)
        alpha = np.where(point, 1, alpha)
    hub = r < 0.06
    rgb[hub] = BRONZE * 1.3
    rgba = np.concatenate([np.clip(rgb, 0, 255), alpha[..., None] * 255], axis=2).astype(np.uint8)
    rose = Image.fromarray(rgba, "RGBA")

    sizes = [(170, 124), (165, 118), (154, 111), (142, 102), (130, 94), (118, 86), (106, 76), (100, 73)]
    sheet = Image.new("RGBA", (sum(w + 2 for w, _ in sizes), 124), (0, 0, 0, 0))
    frames, sx = [], 0
    for w, h in sizes:
        sheet.paste(rose.resize((w, h), Image.LANCZOS), (sx, 0))
        frames.append((sx, 0, w, h, w // 2 - 52, h // 2 - 37))
        sx += w + 2
    folder = out / "Player/Common"
    write_sheet(folder, "compasses", sheet, frames, "The compass beside the area's name, frame by frame as it shrinks.")
    with open(folder / "compasses.anim", "w", newline="\n") as f:
        f.write(HEADER)
        f.write("sheet=compasses.sheet\n\n[chart 0 once]\n8=" + ",".join(str(i) for i in range(len(sizes))) + "\n")


MAP_HOLE = (32, 40, 292, 376)  # x0, y0, x1, y1: where the map shows through, inside its clip
MAP_GEM = (36, 44, 28, 23)  # x, y, w, h, where area_map.cpp puts the gem


def map_gem():
    """A cut blue stone in a bronze collet, 28 x 23, white so the engine can
    dim it as it pulses."""
    w, h = MAP_GEM[2], MAP_GEM[3]
    y, x = np.mgrid[0:h, 0:w].astype(float)
    nx, ny = (x + 0.5 - w / 2) / (w / 2), (y + 0.5 - h / 2) / (h / 2)
    r = np.hypot(nx, ny)
    rgb = np.zeros((h, w, 3))
    alpha = np.zeros((h, w))
    collet = (r <= 1.0) & (r > 0.78)
    rgb[collet] = BRONZE * (0.85 - 0.3 * ny[collet])[:, None]
    alpha[collet] = 1
    # The stone: eight facets round a flat table, lit from the top left.
    stone = r <= 0.78
    facet = np.floor((np.arctan2(ny, nx) + np.pi) / (np.pi / 4)) % 8
    shade = np.where(r < 0.35, 1.15, 0.65 + 0.12 * ((facet + 3) % 8) / 2)
    base = np.array([96, 150, 220])
    rgb[stone] = (base * shade[..., None])[stone]
    alpha[stone] = 1
    glint = np.hypot(nx + 0.3, ny + 0.35) < 0.14
    rgb[glint] = (240, 248, 255)
    rgba = np.concatenate([np.clip(rgb, 0, 255), alpha[..., None] * 255], axis=2).astype(np.uint8)
    return Image.fromarray(rgba, "RGBA")


def map_arrow():
    """The hero's mark: a small blue arrow head pointing down, 7 x 8."""
    rows = ["#######", "#######", ".#####.", ".#####.", "..###..", "..###..", "...#...", "...#..."]
    img = Image.new("RGBA", (7, 8), (0, 0, 0, 0))
    for yy_, row in enumerate(rows):
        for xx_, bit in enumerate(row):
            if bit == "#":
                edge = xx_ == 0 or xx_ == 6 or row[max(0, xx_ - 1)] == "." or row[min(6, xx_ + 1)] == "."
                img.putpixel((xx_, yy_), (40, 70, 170, 255) if edge else (90, 140, 240, 255))
    return img


def map_label():
    """PLAYER in the small capitals, blue with a dark outline."""
    text = "PLAYER"
    img = Image.new("RGBA", (len(text) * 6 + 1, 10), (0, 0, 0, 0))
    ink = []
    for i, ch in enumerate(text):
        for y, row in enumerate(CAPS[ch]):
            for x, bit in enumerate(row):
                if bit == "#":
                    ink.append((1 + i * 6 + x, 1 + y))
    for x, y in ink:
        for dx, dy in ((-1, 0), (1, 0), (0, -1), (0, 1)):
            img.putpixel((x + dx, y + dy), (10, 14, 40, 255))
    for x, y in ink:
        img.putpixel((x, y), (110, 160, 255, 255))
    return img


def area_map(out):
    """The area map's window: 320 x 415, the panel's stone and iron with a
    hole the map shows through, a bronze rim round it, studs down its sides
    and a socket for the gem by the area's name; then the gem, the hero's
    arrow and the PLAYER label (src/game/area_map.cpp)."""
    canvas(320, 415)
    p = Panel(11)
    x0, y0, x1, y1 = MAP_HOLE
    lip = inside(x0 - 3, y0 - 3, x1 + 3, y1 + 3) & ~inside(x0, y0, x1, y1)
    p.bronze(lip, 2.0)
    for y in list(range(y0 + 30, y0 + 120, 22)) + list(range(y1 - 110, y1 - 20, 22)):
        p.boss((x0 - 12, y), 2.5, 3)
        p.boss((x1 + 12, y), 2.5, 3)
    for x in range(x0 + 30, x1 - 20, 40):
        p.boss((x, y0 - 16), 2.2, 2)
    frame = p.image()
    hole = np.asarray(frame).copy()
    hole[y0:y1, x0:x1, 3] = 0
    frame = Image.fromarray(hole, "RGBA")
    canvas(320, 416)

    parts = [map_gem(), map_arrow(), map_label()]
    sheet = Image.new("RGBA", (320, 415 + 26), (0, 0, 0, 0))
    sheet.paste(frame, (0, 0))
    frames = [(0, 0, 320, 415, 0, 0)]
    anchors = [(-MAP_GEM[0], -MAP_GEM[1]), (3, 8), (parts[2].width // 2, 18)]
    x = 0
    for img, (ax, ay) in zip(parts, anchors):
        sheet.paste(img, (x, 415))
        frames.append((x, 415, img.width, img.height, ax, ay))
        x += img.width + 2
    write_sheet(out / "System/Game/Pattern", "Map", sheet, frames,
                "The area map: window, gem, the hero's arrow, the PLAYER label.")


def font(out):
    sheet = Image.new("RGBA", (16 * 6, 16 * 12), (0, 0, 0, 0))
    for table, top in ((CAPS, 1), (SMALL, 3)):
        for ch, rows in table.items():
            code = ord(ch)
            ox, oy = (code % 16) * 6, (code // 16) * 12
            for y, row in enumerate(rows):
                for x, bit in enumerate(row):
                    if bit == "#":
                        sheet.putpixel((ox + x, oy + top + y), (255, 255, 255, 255))
    write_sheet(out / "System/Common/Pattern", "Font00", sheet, [(0, 0, 96, 192, 0, 0)],
                "The font: 6 x 12 cells by byte value, 16 to a row.")


def main():
    out = Path(sys.argv[1])
    only = set(sys.argv[2:])  # the names below, to redraw just those
    sheets = {"inventory": inventory, "shop": shop, "balloon": balloon, "font": font,
              "missions": missions, "compasses": compasses, "map": area_map}
    for name, draw in sheets.items():
        if not only or name in only:
            draw(out)
    # The panels to 256 colours, as the rest of the pack; the balloon and
    # the font have only a few already.
    for name, png in (("inventory", "Inventory"), ("shop", "Shop"), ("missions", "Missions"), ("map", "Map")):
        if not only or name in only:
            shrink.shrink(out / "System/Game/Pattern" / f"{png}.png")
    if not only or "compasses" in only:
        shrink.shrink(out / "Player/Common/compasses.png")
    print("wrote " + ", ".join(n for n in sheets if not only or n in only))

if __name__ == "__main__":
    main()
