#!/usr/bin/env python3
"""Adds rendered animations to a pack character as new charts, without
rendering or packing the rest of the character again.

    python3 tools/art/add_chart.py <rendered folder> <pack character folder> <chart>=<animation> ...

for example

    python3 tools/art/add_chart.py art/hero content/Player/Male 10=shoot 11=heal 13=cast
    python3 tools/art/add_chart.py art/shaman content/Character/ENEMY/00000005 7=cast

The rendered folder is what creatures.py wrote with `--only <animations>`
(for the hero, with its layerNN/ subfolders). The new frames go on a strip
under the sheet's picture, in the sheet's own 256 colours (see
add_layer.py), and each chart is written as build_pack.py writes it, the
blow's frame marked with "!". A chart the .anim already has is replaced
(its old frames stay in the sheet until it is built again). Run
trim_sheets.py on the pack afterwards to pack the sheet tightly again.
Needs numpy and Pillow.
"""

import re
import sys
from pathlib import Path

import numpy as np
from PIL import Image

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
from add_layer import to_palette  # noqa: E402
from build_pack import BODY_PRIORITY, cropped, read_frames  # noqa: E402


def main():
    if len(sys.argv) < 4:
        sys.exit(__doc__)
    src, dst = Path(sys.argv[1]), Path(sys.argv[2])
    charts = [(int(a), b) for a, b in (arg.split("=") for arg in sys.argv[3:])]
    (ax, ay), anims, _, strikes = read_frames(src)
    layers = sorted((int(p.name[5:]), p) for p in src.glob("layer[0-9][0-9]") if (p / "frames.txt").exists())
    layer_frames = {number: read_frames(folder) for number, folder in layers}
    anim_path = dst / "Animation00.anim" if (dst / "Animation00.anim").exists() else dst / "Animation.anim"
    sheet_path = dst / "Animation.sheet"
    sheet_lines = sheet_path.read_text().rstrip("\n").split("\n")
    image_name = next(l[6:] for l in sheet_lines if l.startswith("image="))
    count = sum(l.startswith("frame=") for l in sheet_lines)
    sheet = Image.open(dst / image_name)
    sheet.load()
    if sheet.mode != "P":
        sys.exit(f"{dst / image_name} is not shrunk yet: run shrink.py first")
    pal = np.array(sheet.getpalette()[:768], np.int32).reshape(-1, 3)
    pal = np.vstack([pal, np.zeros((256 - len(pal), 3), np.int32)])
    alpha = np.full(256, 255)
    trns = sheet.info["transparency"]
    if isinstance(trns, bytes):
        alpha[:len(trns)] = list(trns)
    else:
        alpha[trns] = 0

    pictures, sections = [], []

    def add(picture):
        pictures.append(picture)
        return count + len(pictures) - 1

    for chart, name in charts:
        if name not in anims:
            sys.exit(f"{src} has no {name} animation")
        frames, loop = anims[name]
        lines = [f"[chart {chart} {'loop' if loop else 'once'}]"]
        for d in ([8] if (src / f"{name}_8_0.png").exists() else range(8)):
            body = [add(cropped(src / f"{name}_{d}_{f}.png", ax, ay)) for f in range(frames)]
            marks = [f"{i}!" if f == strikes.get(name) else str(i) for f, i in enumerate(body)]
            lines.append(f"{d}{f'@{BODY_PRIORITY}' if layers else ''}=" + ",".join(marks))
            for number, folder in layers:
                (lx, ly), layer_anims, priorities, _ = layer_frames[number]
                if name not in layer_anims:
                    continue
                cells = []
                for f in range(frames):
                    im, cx, cy = cropped(folder / f"{name}_{d}_{f}.png", lx, ly)
                    if im.getbbox() is None:
                        cells.append("-")
                        continue
                    cells.append(f"{add((im, cx, cy))}@{priorities.get((name, d, f), 10)}")
                lines.append(f"{d}.{number}=" + ",".join(cells))
        sections.append((chart, lines))

    # Drop the charts being replaced, then add the new ones at the end.
    replaced = {chart for chart, _ in charts}
    kept, skipping = [], False
    for line in anim_path.read_text().rstrip("\n").split("\n"):
        m = re.match(r"\[chart (\d+) ", line)
        if m:
            skipping = int(m.group(1)) in replaced
        if not skipping:
            kept.append(line)
    while kept and not kept[-1]:
        kept.pop()
    for _, lines in sections:
        kept += [""] + lines

    # A strip under the sheet, left to right.
    x = y = shelf = 0
    places = []
    for im, _, _ in pictures:
        if x + im.width > sheet.width:
            x, y, shelf = 0, y + shelf, 0
        places.append((x, sheet.height + y))
        x += im.width
        shelf = max(shelf, im.height)
    clear = int(np.nonzero(alpha == 0)[0][0])
    grown = np.full((sheet.height + y + shelf, sheet.width), clear, np.uint8)
    grown[:sheet.height] = np.asarray(sheet)
    for (im, _, _), (px, py) in zip(pictures, places):
        region = grown[py:py + im.height, px:px + im.width]
        region[:] = np.where(np.asarray(im)[..., 3] > 0, to_palette(im, pal, alpha), region)
    result = Image.fromarray(grown, "P")
    result.putpalette(pal.astype(np.uint8).ravel().tolist())
    result.save(dst / image_name, optimize=True, transparency=trns)
    for (im, cx, cy), (px, py) in zip(pictures, places):
        sheet_lines.append(f"frame={px},{py},{im.width},{im.height},{cx},{cy}")
    sheet_path.write_text("\n".join(sheet_lines) + "\n")
    anim_path.write_text("\n".join(kept) + "\n")
    print(f"{dst}: charts {', '.join(str(c) for c, _ in charts)}, {len(pictures)} frames")


if __name__ == "__main__":
    main()
