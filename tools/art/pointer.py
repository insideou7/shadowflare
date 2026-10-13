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

Drawn by hand as pixel art from the strings below. Needs Pillow.
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
    print("wrote System/Common/Pattern/System")


if __name__ == "__main__":
    main()
