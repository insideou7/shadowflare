#!/usr/bin/env python3
"""Suggest content/skin.txt lines for one of the original's object banks:
for each piece, which of the given props looks most like it and where to
put it, by sliding each prop's silhouette over the original's and keeping
the place where they overlap best (colour breaks near ties).

    python3 tools/content/skin_fit.py <game folder> <bank> <patterns> <prop> [prop ...]

e.g. `skin_fit.py game nomal.njp 0-8 tree_dead tree_autumn tree_dead!`.
<patterns> is a list like 0-8,12. A prop ending in ! is tried mirrored.
Prints `pattern=prop@dx,dy  # overlap` lines; check them with
skin_preview.py before keeping them. Needs Pillow and numpy.
"""

import sys
from pathlib import Path

import numpy as np
from PIL import Image

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE.parent / "re"))
sys.path.insert(0, str(HERE))
import render  # noqa: E402
import upd  # noqa: E402
from skin_preview import CONTENT, PROJ, find, read_sheet, read_skin  # noqa: E402


def patterns(text):
    out = []
    for item in text.split(","):
        a, _, b = item.partition("-")
        out += range(int(a), int(b or a) + 1)
    return out


def solid(im):
    # Our props' shadows are see-through black: leave them out (a black
    # wall face is solid).
    rgb = np.asarray(im.convert("RGB"), dtype=float)
    alpha = np.asarray(im.getchannel("A"))
    a = (alpha == 255) | ((alpha > 128) & (rgb.max(axis=2) > 8))
    return a, rgb[a].mean(axis=0) if a.any() else np.zeros(3)


def overlap(a, b, ox, oy):
    """Overlap (intersection over union) of mask b placed at (ox, oy) on a."""
    h, w = a.shape
    bh, bw = b.shape
    x0, y0, x1, y1 = max(0, ox), max(0, oy), min(w, ox + bw), min(h, oy + bh)
    inter = 0 if x1 <= x0 or y1 <= y0 else np.count_nonzero(a[y0:y1, x0:x1] & b[y0 - oy:y1 - oy, x0 - ox:x1 - ox])
    return inter / (a.sum() + b.sum() - inter)


def fit(orig, prop):
    """Best screen offset of the prop's anchor from the original's foot."""
    a, colour_a = solid(orig)
    im, ax, ay = prop
    b, colour_b = solid(im)
    ys, xs = np.nonzero(a)
    foot = ((xs.min() + xs.max() + 1) // 2, ys.max() + 1)
    by, bx = np.nonzero(b)
    # Start with the bottoms' middles together, then search round that.
    sx0 = foot[0] - (bx.min() + bx.max() + 1) // 2
    sy0 = foot[1] - (by.max() + 1)
    best = (-1, sx0, sy0)
    # Things stand on their feet, so the bottoms stay close together.
    for step, rx, ry, centre in ((4, 48, 12, (sx0, sy0)), (1, 3, 3, None)):
        cx, cy = centre or best[1:]
        for oy in range(cy - ry, cy + ry + 1, step):
            for ox in range(cx - rx, cx + rx + 1, step):
                s = overlap(a, b, ox, oy)
                if s > best[0]:
                    best = (s, ox, oy)
    score, ox, oy = best
    sx, sy = ox + ax - foot[0], oy + ay - foot[1]
    # Screen to world: sx = (x - y) * 15/100, sy = (x + y) * 10/100.
    u, v = sx * 100 / PROJ[0], sy * 100 / PROJ[1]
    colour = np.abs(colour_a - colour_b).mean() / 255
    return score - 0.3 * colour, score, round((u + v) / 2), round((v - u) / 2)


def main():
    game, bank_name = Path(sys.argv[1]), sys.argv[2]
    if not (game / "Map").is_dir():
        game = next(p.parent for p in game.rglob("Map") if (p / "Pattern").is_dir())
    bank = upd.parse(find(game / "Map" / "Pattern", bank_name).read_bytes())
    props_path, _ = read_skin(CONTENT / "skin.txt")
    frames = read_sheet(CONTENT / props_path)
    candidates = []
    for name in sys.argv[4:]:
        im, ax, ay = frames[name.rstrip("!")]
        if name.endswith("!"):
            im, ax = im.transpose(Image.FLIP_LEFT_RIGHT), im.width - ax
        candidates.append((name, (im, ax, ay)))
    for p in patterns(sys.argv[3]):
        r = render.render_pattern(bank, p)
        if not r:
            continue
        w, h, rgba = r
        orig = Image.frombytes("RGBA", (w, h), bytes(rgba))
        if not orig.getbbox():
            continue
        results = sorted(((fit(orig, prop), name) for name, prop in candidates), reverse=True)
        (_, score, dx, dy), name = results[0]
        at = f"@{dx},{dy}" if (dx, dy) != (0, 0) else ""
        print(f"{p}={name}{at}  # {score:.2f}" + (f", then {results[1][1]}" if len(results) > 1 else ""))


if __name__ == "__main__":
    main()
