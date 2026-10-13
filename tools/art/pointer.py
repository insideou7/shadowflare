#!/usr/bin/env python3
"""The mouse pointer, for the open content pack.

    python3 tools/art/pointer.py <content pack folder>

Writes System/Common/Pattern/System, numbered like the original's
System.njp so the engine draws it the same way with either
(src/game/pointer.cpp):

- 0: the pointer, a steel arrowhead with a bronze collar, its tip on the
  hot spot at the top left;
- 1: the same with a bronze question mark beside it, shown while
  Identify waits for an item.

and System/Game/Pattern/Elements, the orb shown before a monster's name
under the pointer for each element (fire, water, earth, thunder, holy,
dark, gel, metal), standing in for the original's StatusIcon.njp
patterns 3-10: small glass beads lit from the top left.

The pointer is drawn by hand as pixel art from the strings below; the
orbs are shaded spheres. Needs Pillow.
"""

import sys
from pathlib import Path

from PIL import Image


# k outline, w highlight, s steel, d shaded steel, b bronze, o dark bronze.
ARROW = [
    "k.............",
    "kk............",
    "kwk...........",
    "kwsk..........",
    "kwssk.........",
    "kwsssk........",
    "kwssssk.......",
    "kwsssddk......",
    "kwssssddk.....",
    "kwsssdddkk....",
    "kwssdddkk.....",
    "kwsdbbk.......",
    "kwdkobbk......",
    "kdk.kobbk.....",
    "kk...kobbk....",
    "k.....kobk....",
    ".......kk.....",
]
QUESTION = [
    ".kkkk..",
    "kbbbbk.",
    "kok.kbk",
    ".k..kbk",
    "...kbk.",
    "..kbk..",
    "..kok..",
    "...k...",
    "..kbk..",
    "..kok..",
    "...k...",
]
COLOURS = {
    "k": (16, 14, 18, 255),
    "w": (236, 236, 240, 255),
    "s": (184, 188, 196, 255),
    "d": (112, 116, 128, 255),
    "b": (204, 160, 86, 255),
    "o": (128, 90, 44, 255),
}
W, H = 22, 20


def stamp(img, rows, x0, y0):
    for y, row in enumerate(rows):
        for x, ch in enumerate(row):
            if ch in COLOURS:
                img.putpixel((x0 + x, y0 + y), COLOURS[ch])


# Each element's bead: its body colour.
ELEMENTS = [(196, 44, 30), (40, 104, 206), (150, 98, 54), (214, 186, 34),
            (236, 228, 196), (120, 52, 160), (96, 176, 104), (128, 132, 140)]
ORB = 12  # 12 x 11, as the original's


def orb(colour):
    img = Image.new("RGBA", (ORB, ORB - 1), (0, 0, 0, 0))
    cx, cy, r = 5.5, 5.0, 5.0
    for y in range(ORB - 1):
        for x in range(ORB):
            dx, dy = (x + 0.5 - cx) / r, (y + 0.5 - cy) / r
            d = dx * dx + dy * dy
            if d > 1.0:
                continue
            if d > 0.72:  # the dark rim
                img.putpixel((x, y), tuple(int(c * 0.35) for c in colour) + (255,))
                continue
            z = (1 - d) ** 0.5
            light = max(0.0, -0.55 * dx - 0.6 * dy + 0.58 * z)  # from the top left
            shade = 0.45 + 0.75 * light
            glint = 255 * max(0.0, light - 0.78) * 6  # the bright spot
            px = [min(255, int(c * shade + glint)) for c in colour]
            img.putpixel((x, y), tuple(px) + (255,))
    return img


def elements(out):
    sheet = Image.new("RGBA", (ORB * len(ELEMENTS), ORB - 1), (0, 0, 0, 0))
    for i, colour in enumerate(ELEMENTS):
        sheet.paste(orb(colour), (i * ORB, 0))
    folder = out / "System/Game/Pattern"
    folder.mkdir(parents=True, exist_ok=True)
    sheet.save(folder / "Elements.png", optimize=True)
    with open(folder / "Elements.sheet", "w", newline="\n") as f:
        f.write("# Drawn by tools/art/pointer.py (our own art, see content/README.md).\n")
        f.write("# Element orbs: fire, water, earth, thunder, holy, dark, gel, metal.\n")
        f.write("image=Elements.png\n")
        for i in range(len(ELEMENTS)):
            f.write(f"frame={i * ORB},0,{ORB},{ORB - 1},0,0\n")


def main():
    out = Path(sys.argv[1])
    sheet = Image.new("RGBA", (2 * W, H), (0, 0, 0, 0))
    stamp(sheet, ARROW, 0, 0)
    stamp(sheet, ARROW, W, 0)
    stamp(sheet, QUESTION, W + 14, 7)
    folder = out / "System/Common/Pattern"
    folder.mkdir(parents=True, exist_ok=True)
    sheet.save(folder / "System.png", optimize=True)
    with open(folder / "System.sheet", "w", newline="\n") as f:
        f.write("# Drawn by tools/art/pointer.py (our own art, see content/README.md).\n")
        f.write("# The mouse pointer (hot spot at the top left), then with Identify's question mark.\n")
        f.write("image=System.png\n")
        f.write(f"frame=0,0,{W},{H},0,0\nframe={W},0,{W},{H},0,0\n")
    elements(out)
    print("wrote System/Common/Pattern/System and System/Game/Pattern/Elements")


if __name__ == "__main__":
    main()
