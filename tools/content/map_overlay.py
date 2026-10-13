#!/usr/bin/env python3
"""Lay the open pack's first-area maps over the original's walkable ground
and say how closely they match: where the player can walk in one but not
the other. Needs the player's own copy of the game.

    python3 tools/content/map_overlay.py <game folder> <content pack> [out.png] [map ...]

Prints, for each map, the share of the original's walkable ground (cells
of 160 world units) that the open map lets the player walk on too, and the
share of the open map's walkable ground the original has. With out.png,
draws every map top down: grey where both can be walked on, red where only
the original can, blue where only the open map can.
"""

import sys
from collections import Counter
from pathlib import Path

import numpy as np
from PIL import Image, ImageDraw

sys.path.insert(0, str(Path(__file__).resolve().parent))
import first_area as fa  # noqa: E402


def read_open_map(path):
    """The judge grid of an open map, as a function of world point -> walkable."""
    cell, judge, rows, section = (160, 160), (0, 0), [], None
    for line in path.read_text().split("\n"):
        line = line.split("#")[0].strip()
        if line.startswith("["):
            section = line
        elif section is None and line.startswith("cell="):
            cell = tuple(int(v) for v in line[5:].split(","))
        elif section is None and line.startswith("judge="):
            judge = tuple(int(v) for v in line[6:].split(",")[:2])
        elif section == "[judge]" and line:
            rows.append([c != "x" for c in line])
    grid = np.array(rows, bool)

    def walk(x, y):
        cx, cy = np.floor(x / cell[0]).astype(int) - judge[0], np.floor(y / cell[1]).astype(int) - judge[1]
        ok = (cx >= 0) & (cy >= 0) & (cx < grid.shape[1]) & (cy < grid.shape[0])
        out = np.zeros(np.shape(x), bool)
        out[ok] = grid[cy[ok], cx[ok]]
        return out
    return walk


def origin(o, scenario):
    """Where the open map's (0, 0) is in the original's world: the shift
    most of its entries share with the original's."""
    ours = {}
    for line in scenario.read_text().split("\n"):
        if line.startswith("entry="):
            v = [int(t) for t in line[6:].split(",")]
            ours[v[0]] = (v[1], v[2])
    shifts = Counter()
    for eid, x, y, _ in o.entries:
        if eid % 4 == 0 and eid // 4 in ours:
            shifts[(x - ours[eid // 4][0], y - ours[eid // 4][1])] += 1
    return shifts.most_common(1)[0][0]


def compare(game, pack, number):
    o = fa.Original(game, number)
    reach = fa.walkable(o)
    ys, xs = np.nonzero(reach)
    pad = 12
    ys, xs = np.arange(ys.min() - pad, ys.max() + pad + 1), np.arange(xs.min() - pad, xs.max() + pad + 1)
    ox, oy = origin(o, pack / "Scenario" / f"{fa.our_number(number):08d}" / "scenario.txt")
    walk = read_open_map(pack / "Map" / f"{o.name}.txt")
    X, Y = np.meshgrid((xs + o.jx0) * fa.CELL + fa.CELL // 2 - ox, (ys + o.jy0) * fa.CELL + fa.CELL // 2 - oy)
    ours = walk(X, Y)
    theirs = np.zeros(ours.shape, bool)
    ok_y, ok_x = (ys >= 0) & (ys < reach.shape[0]), (xs >= 0) & (xs < reach.shape[1])
    theirs[np.ix_(ok_y, ok_x)] = reach[np.ix_(ys[ok_y], xs[ok_x])]
    return o, theirs, ours


def picture(o, theirs, ours, label):
    img = np.full(theirs.shape + (3,), 24, np.uint8)
    img[theirs & ours] = (170, 170, 170)
    img[theirs & ~ours] = (220, 50, 50)
    img[~theirs & ours] = (60, 110, 230)
    pic = Image.fromarray(img)
    s = max(1, 300 // max(pic.size))
    pic = pic.resize((pic.width * s, pic.height * s), Image.NEAREST)
    out = Image.new("RGB", (max(pic.width, 300), pic.height + 16), (0, 0, 0))
    out.paste(pic, (0, 16))
    ImageDraw.Draw(out).text((2, 2), label, fill=(255, 255, 255))
    return out


def main():
    game, pack = Path(sys.argv[1]), Path(sys.argv[2])
    if not (game / "Map").is_dir():
        game = next(p.parent for p in game.rglob("Map") if (p / "Pattern").is_dir())
    out = Path(sys.argv[3]) if len(sys.argv) > 3 else None
    names = set(sys.argv[4:])
    pictures = []
    for number in fa.FIELDS + fa.DUNGEONS:
        o, theirs, ours = compare(game, pack, number)
        if names and o.name not in names:
            continue
        both = (theirs & ours).sum()
        a, b = both / max(1, theirs.sum()), both / max(1, ours.sum())
        print(f"{o.name} {o.title}: {a:.1%} of the original's walkable ground is walkable here, "
              f"{b:.1%} of ours is walkable there")
        if out:
            pictures.append(picture(o, theirs, ours, f"{o.name} {a:.0%} / {b:.0%}"))
    if out and pictures:
        cols = min(6, len(pictures))
        w = max(p.width for p in pictures)
        rows = [pictures[i:i + cols] for i in range(0, len(pictures), cols)]
        h = [max(p.height for p in r) for r in rows]
        sheet = Image.new("RGB", (w * cols, sum(h)), (0, 0, 0))
        y = 0
        for r, rh in zip(rows, h):
            for i, p in enumerate(r):
                sheet.paste(p, (i * w, y))
            y += rh
        sheet.save(out)


if __name__ == "__main__":
    main()
