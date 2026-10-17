#!/usr/bin/env python3
"""Trims the empty space round each frame of a pack's character sheets and
packs the frames tightly, so the game decodes far fewer pixels when it
loads a character (our renders are mostly see-through: a 200 x 200 frame
is about 3% figure).

    python3 tools/art/trim_sheets.py <content pack folder>

Works on the sheets under Character/ENEMY, Character/PEOPLE,
Character/PARTNER and Player, after shrink.py: each picture keeps its
palette and see-through colour, and each frame its anchor, so the game
draws everything where it did. Map/props.sheet is only packed tightly,
each frame keeping its size (tools/content/first_area.py reads the props'
widths; the game trims them as it loads them); add_props.py grows it with
gaps, so run this again after it. Other sheets (fonts, the panel, items)
are left alone, since their frames' sizes can matter. Rewrites the files
in place and leaves a sheet that is already tight as it is.
Frames that show the same picture (a held pose, equipment that does
not move) share one copy in the sheet, which the game then loads once.
"""

import sys
from pathlib import Path

import numpy as np
from PIL import Image

FOLDERS = ("Character/ENEMY", "Character/PEOPLE", "Character/PARTNER", "Player")
PACKED = ("Map/props.sheet",)  # packed, frames kept whole
WIDTH = 2048  # the packed sheet's width


def trim(sheet_path, keep_frames=False):
    lines = sheet_path.read_text().split("\n")
    image_name = next(line[6:] for line in lines if line.startswith("image="))
    image_path = sheet_path.parent / image_name
    im = Image.open(image_path)
    if im.mode == "P":
        # The see-through colours: one index, or an alpha per index.
        clear = im.info.get("transparency")
        if clear is None:
            return False
        if isinstance(clear, bytes):
            alpha = np.full(256, 255, np.uint8)
            alpha[:len(clear)] = np.frombuffer(clear, np.uint8)
        else:
            alpha = np.full(256, 255, np.uint8)
            alpha[clear] = 0
        see_through = alpha[np.array(im)] == 0
    else:
        im = im.convert("RGBA")
        see_through = np.array(im)[:, :, 3] == 0

    # Each frame cut down to what shows in it; frames sharing a rectangle
    # share the cut.
    frames, cuts = [], {}
    for i, line in enumerate(lines):
        if not line.startswith("frame="):
            continue
        fields = line[6:].split(",")
        x, y, w, h, ax, ay = map(int, fields[:6])
        if keep_frames:
            cuts[(x, y, w, h)] = (x, y, x + w, y + h)
        elif (x, y, w, h) not in cuts:
            shows = ~see_through[y:y + h, x:x + w]
            rows, cols = np.nonzero(shows.any(axis=1))[0], np.nonzero(shows.any(axis=0))[0]
            if len(rows) == 0:
                box = (x, y, x + 1, y + 1)
            else:
                box = (x + cols[0], y + rows[0], x + cols[-1] + 1, y + rows[-1] + 1)
            cuts[(x, y, w, h)] = box
        frames.append((i, (x, y, w, h), ax, ay, fields[6:]))
    if not frames:
        return False
    # Frames whose cuts show the same picture (a pose held for several
    # frames, equipment that does not move) share one copy.
    pixels = np.array(im)
    cut_of = dict(cuts)
    same, first = {}, {}
    for key, (bx0, by0, bx1, by1) in cuts.items():
        look = pixels[by0:by1, bx0:bx1]
        same[key] = first.setdefault((look.shape, look.tobytes()), key)
    if not keep_frames:
        cuts = {key: box for key, box in cuts.items() if same[key] == key}
    else:
        same = {key: key for key in cuts}
    used = sum((b[2] - b[0]) * (b[3] - b[1]) for b in cuts.values())
    shared = len(same) - len(cuts)
    if not shared and used >= 0.8 * (im.width * im.height if keep_frames else sum(w * h for (_, _, w, h) in cuts)):
        return False

    # Shelves, tallest first.
    width = max(WIDTH, max(b[2] - b[0] for b in cuts.values()))
    order = sorted(cuts, key=lambda k: -(cuts[k][3] - cuts[k][1]))
    placed, px, py, shelf = {}, 0, 0, 0
    for key in order:
        bx0, by0, bx1, by1 = cuts[key]
        if px + bx1 - bx0 > width:
            px, py, shelf = 0, py + shelf, 0
        placed[key] = (px, py)
        px += bx1 - bx0
        shelf = max(shelf, by1 - by0)
    out = Image.new(im.mode, (width, max(1, py + shelf)), 0)
    if im.mode == "P":
        out.putpalette(im.getpalette())
    for key, (qx, qy) in placed.items():
        out.paste(im.crop(cuts[key]), (qx, qy))

    for i, (x, y, w, h), ax, ay, rest in frames:
        bx0, by0, bx1, by1 = cuts[same[(x, y, w, h)]]
        qx, qy = placed[same[(x, y, w, h)]]
        # Where this frame's own cut began, for its anchor (the shared copy
        # shows the same picture, cut the same way round it).
        cx0, cy0 = cut_of[(x, y, w, h)][:2]
        fields = [qx, qy, bx1 - bx0, by1 - by0, ax - (cx0 - x), ay - (cy0 - y)]
        lines[i] = "frame=" + ",".join(map(str, fields + rest))
    save = {}
    if "transparency" in im.info:
        save["transparency"] = im.info["transparency"]
    out.save(image_path, **save)
    sheet_path.write_text("\n".join(lines))
    return True


def main():
    pack = Path(sys.argv[1])
    for folder in FOLDERS:
        for sheet in sorted((pack / folder).rglob("*.sheet")):
            if trim(sheet):
                print(f"{sheet.relative_to(pack)}: trimmed")
    for name in PACKED:
        if (pack / name).exists() and trim(pack / name, keep_frames=True):
            print(f"{name}: packed")


if __name__ == "__main__":
    main()
