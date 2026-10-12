#!/usr/bin/env python3
"""Side-by-side check of content/skin.txt against the player's own copy of
the original game: each of a bank's pieces as the original draws it, the
props the skin draws instead, and the two laid over each other, with the
foot both stand on marked in red.

    python3 tools/content/skin_preview.py <game folder> <bank> <out.png> [pattern ...]

e.g. `skin_preview.py game 00f_obj00.njp /tmp/fence.png 13 14`. The game
folder is the install folder (with Map/ in it). Needs Pillow. The picture
holds the original's art, so keep it to yourself.
"""

import sys
from pathlib import Path

from PIL import Image, ImageDraw

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE.parent / "re"))
import render  # noqa: E402
import upd  # noqa: E402

CONTENT = HERE.parent.parent / "content"
PROJ = (15, 10)


def find(folder, name):
    for p in folder.iterdir():
        if p.name.lower() == name.lower():
            return p
    raise SystemExit(f"{name} not found in {folder}")


def read_skin(path):
    """props sheet and {bank: {pattern: [(prop, dx, dy)]}} from skin.txt."""
    props, objects, bank = None, {}, None
    for line in path.read_text().split("\n"):
        line = line.split("#")[0].strip()
        if not line:
            continue
        if line.startswith("["):
            kind, _, name = line[1:-1].partition(" ")
            bank = name.lower() if kind == "objects" else None
            if bank:
                objects.setdefault(bank, {})
            continue
        key, _, value = line.partition("=")
        if key == "props":
            props = value
        elif bank:
            pieces = []
            if value != "-":
                for word in value.split():
                    name, _, at = word.partition("@")
                    dx, dy = map(int, at.split(",")) if at else (0, 0)
                    pieces.append((name, dx, dy))  # a name ending in ! is mirrored
            for item in key.split(","):
                a, _, b = item.partition("-")
                for p in range(int(a), int(b or a) + 1):
                    objects[bank][p] = pieces
    return props, objects


def read_sheet(path):
    image, frames = None, {}
    for line in path.read_text().split("\n"):
        if line.startswith("image="):
            image = Image.open(path.parent / line[6:].strip()).convert("RGBA")
        elif line.startswith("frame="):
            v = line[6:].strip().split(",")
            if len(v) == 7:
                x, y, w, h, ax, ay = map(int, v[:6])
                frames[v[6]] = (image.crop((x, y, x + w, y + h)), ax, ay)
    return frames


def main():
    game, bank_name, out = Path(sys.argv[1]), sys.argv[2], Path(sys.argv[3])
    if (game / "Map").is_dir() is False:
        game = next(p.parent for p in game.rglob("Map") if (p / "Pattern").is_dir())
    bank = upd.parse(find(game / "Map" / "Pattern", bank_name).read_bytes())
    props_path, objects = read_skin(CONTENT / "skin.txt")
    props = read_sheet(CONTENT / props_path)
    rules = objects.get(bank_name.lower(), {})
    wanted = [int(a) for a in sys.argv[4:]] or range(len(bank["patterns"]))
    panels = []
    for p in wanted:
        r = render.render_pattern(bank, p)
        if not r:
            continue
        w, h, rgba = r
        orig = Image.frombytes("RGBA", (w, h), bytes(rgba))
        box = orig.getbbox() or (0, 0, w, h)
        foot = ((box[0] + box[2]) // 2, box[3])
        # Lay everything out round the foot.
        pieces = []
        for name, dx, dy in rules.get(p, []):
            mirror = name.endswith("!")
            if name.rstrip("!") in props:
                im, ax, ay = props[name.rstrip("!")]
                if mirror:
                    im, ax = im.transpose(Image.FLIP_LEFT_RIGHT), im.width - ax
                sx, sy = (dx - dy) * PROJ[0] // 100, (dx + dy) * PROJ[1] // 100
                pieces.append((im, foot[0] + sx - ax, foot[1] + sy - ay))
        left = min([0] + [x for _, x, _ in pieces]) - 8
        top = min([0] + [y for _, _, y in pieces]) - 8
        right = max([w] + [x + im.width for im, x, _ in pieces]) + 8
        bottom = max([h] + [y + im.height for im, _, y in pieces]) + 8
        size = (right - left, bottom - top)
        a = Image.new("RGBA", size, (70, 70, 80, 255))
        a.alpha_composite(orig, (-left, -top))
        b = Image.new("RGBA", size, (70, 70, 80, 255))
        for im, x, y in pieces:
            b.alpha_composite(im, (x - left, y - top))
        c = Image.blend(a, b, 0.5)
        panel = Image.new("RGBA", (size[0] * 3 + 8, size[1] + 14), (40, 40, 48, 255))
        for i, im in enumerate((a, b, c)):
            d = ImageDraw.Draw(im)
            fx, fy = foot[0] - left, foot[1] - top
            d.line((fx - 4, fy, fx + 4, fy), fill=(255, 0, 0, 255))
            d.line((fx, fy - 4, fx, fy + 4), fill=(255, 0, 0, 255))
            panel.paste(im, (i * (size[0] + 4), 14))
        label = " ".join(f"{n}@{dx},{dy}" for n, dx, dy in rules.get(p, [])) if p in rules else "(original)"
        ImageDraw.Draw(panel).text((2, 1), f"{p}: {label or '-'}", fill=(255, 255, 0, 255))
        panels.append(panel)
    cols = max(1, 1800 // max(p.width + 6 for p in panels))
    rows = [panels[i:i + cols] for i in range(0, len(panels), cols)]
    sheet = Image.new("RGBA", (max(sum(p.width + 6 for p in row) for row in rows),
                               sum(max(p.height for p in row) + 6 for row in rows)), (30, 30, 36, 255))
    y = 0
    for row in rows:
        x = 0
        for p in row:
            sheet.paste(p, (x, y))
            x += p.width + 6
        y += max(p.height for p in row) + 6
    sheet.save(out)
    print(out, sheet.size)


if __name__ == "__main__":
    main()
