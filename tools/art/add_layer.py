#!/usr/bin/env python3
"""Adds one rendered equipment layer to a pack's hero sheet, without
rendering or packing the rest of the hero again.

    python3 tools/art/add_layer.py <rendered hero folder> <layer> <pack Player/Male or Player/Female>

The rendered folder is what creatures.py wrote with `--layers <layer>`
(its layerNN/ subfolder). The new frames go on a strip under the sheet's
picture, in the sheet's own 256 colours (shrink.py's layout: index 0 see
through, then the shadow levels, then the opaque colours), and each of the
player's charts gets a `<d>.<layer>=` line, as build_pack.py writes them.
Run trim_sheets.py on the pack afterwards to pack the sheet tightly again.
A layer the .anim already has is replaced. Needs numpy and Pillow.
"""

import re
import sys
from pathlib import Path

import numpy as np
from PIL import Image

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
from build_pack import PLAYER_CHARTS, cropped, read_frames  # noqa: E402
from shrink import SHADOWS  # noqa: E402


def to_palette(im, pal, alpha):
    """An RGBA picture as indices into a shrunk sheet's palette."""
    a = np.asarray(im).astype(np.int32)
    rgb, al = a[..., :3], a[..., 3]
    shadow = (al > 0) & (al < 255) & (rgb.max(axis=2) < 48)
    solid = (al >= 128) & ~shadow
    opaque = np.nonzero(alpha == 255)[0]
    out = np.zeros(al.shape, np.uint8)
    if solid.any():
        colours = pal[opaque]
        d = ((rgb[solid][:, None, :] - colours[None, :, :]) ** 2).sum(axis=2)
        out[solid] = opaque[d.argmin(axis=1)]
    levels = np.clip((al * SHADOWS + 127) // 255, 0, SHADOWS)
    out[shadow] = levels[shadow].astype(np.uint8)
    return out


def main():
    src, layer, dst = Path(sys.argv[1]), int(sys.argv[2]), Path(sys.argv[3])
    folder = src / f"layer{layer:02d}"
    (lx, ly), anims, priorities, _ = read_frames(folder)
    anim_path, sheet_path = dst / "Animation00.anim", dst / "Animation.sheet"
    lines = [l for l in anim_path.read_text().split("\n") if not re.match(rf"\d\.{layer}=", l)]
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
    pictures, new_lines = [], {}
    for chart, name in PLAYER_CHARTS:
        if name not in anims:
            continue  # rendered without this animation
        frames = anims[name][0]
        for d in range(8):
            cells = []
            for f in range(frames):
                im, cx, cy = cropped(folder / f"{name}_{d}_{f}.png", lx, ly)
                if im.getbbox() is None:
                    cells.append("-")
                    continue
                cells.append(f"{count + len(pictures)}@{priorities.get((name, d, f), 10)}")
                pictures.append((im, cx, cy))
            new_lines[(chart, d)] = f"{d}.{layer}=" + ",".join(cells)
    # Each new line goes after the body's and the lower layers' lines for
    # its direction.
    chart = None
    out = []
    for i, line in enumerate(lines):
        m = re.match(r"\[chart (\d+) ", line)
        if m:
            chart = int(m.group(1))
        out.append(line)
        m = re.match(r"(\d)(?:\.(\d+))?[@=]", line)
        if m and (chart, int(m.group(1))) in new_lines:
            d = int(m.group(1))
            here = int(m.group(2) or 0)
            following = re.match(rf"{d}\.(\d+)=", lines[i + 1]) if i + 1 < len(lines) else None
            if here < layer and not (following and int(following.group(1)) < layer):
                out.append(new_lines.pop((chart, d)))
    if new_lines:
        sys.exit(f"no place for {sorted(new_lines)} in {anim_path}")
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
    for (im, ax, ay), (px, py) in zip(pictures, places):
        sheet_lines.append(f"frame={px},{py},{im.width},{im.height},{ax},{ay}")
    sheet_path.write_text("\n".join(sheet_lines) + "\n")
    anim_path.write_text("\n".join(out))
    print(f"{dst}: layer {layer}, {len(pictures)} frames")


if __name__ == "__main__":
    main()
