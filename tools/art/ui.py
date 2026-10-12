#!/usr/bin/env python3
"""The interface pictures for the open content pack.

    python3 tools/art/ui.py <content pack folder>

Draws, in the style of the original but from scratch:

- the inventory panel: a 320 x 416 window of dark carved stone in an iron
  frame with bronze trim, with sunken recesses for the equipment slots and
  the 9 x 4 bag, and plates for the totals and the gold, where the engine
  puts them (src/game/items.cpp, which follows ShadowFlare.exe's item
  window). Written as System/Game/Pattern/Inventory; its anchor puts it at
  x = 320 on the screen.
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
STATS_PLATE = (10, 10, 150, 102)  # x0, y0, x1, y1: the totals are written here
GOLD_PLATE = (10, 396, 170, 411)
GOODS = (16, 72, 9, 10)  # the shop's cells: x, y, columns, rows, as in talk.cpp

STONE = np.array([42, 40, 47])
IRON = np.array([50, 48, 49])
BRONZE = np.array([160, 124, 66])
HOLLOW = np.array([24, 20, 24])
MAROON = np.array([34, 14, 16])

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


def inventory(out):
    p = Panel(7)
    for sx, sy, cw, ch in SLOTS:
        p.sunken(sx - LEFT, sy, sx - LEFT + cw * CELL, sy + ch * CELL)
    bx, by, cols, rows = BAG
    p.sunken(bx, by, bx + cols * CELL, by + rows * CELL)
    p.cells(bx, by, cols, rows, STONE * 0.9)
    # Plates for the totals and the gold: shallow, smooth and dark.
    for x0, y0, x1, y1 in (STATS_PLATE, GOLD_PLATE):
        p.sunken(x0, y0, x1, y1, depth=1.5, rim=False, floor=STONE * 0.6)
    write_sheet(out / "System/Game/Pattern", "Inventory", p.image(), [(0, 0, W, H, -LEFT, 0)],
                "The inventory panel; the anchor puts it at x = 320 on the 640 x 480 screen.")


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
    inventory(out)
    shop(out)
    balloon(out)
    font(out)
    # The panels to 256 colours, as the rest of the pack; the balloon and
    # the font have only a few already.
    for name in ("Inventory", "Shop"):
        shrink.shrink(out / "System/Game/Pattern" / f"{name}.png")
    print("wrote the inventory, the shop window, the speech balloon and the font")


if __name__ == "__main__":
    main()
