#!/usr/bin/env python3
"""Add newly rendered props to content/'s props sheet without rebuilding
the whole pack: each picture goes on a strip below the sheet, as a named
frame, and the sheet is shrunk back to 256 colours. A prop already on the
sheet is redrawn in place when its new picture is the same size (a
retextured model); otherwise it is replaced (its old picture stays,
unused, until the next full build_pack.py run).

    python3 tools/art/add_props.py <render folder> <listing> [prop ...]

e.g. `add_props.py /tmp/art/deep /tmp/art/deep/deep.txt earth_tall_x`,
where the listing is the `<name> <anchor x> <anchor y>` file the art
scripts write. Needs numpy and Pillow.
"""

import sys
from pathlib import Path

from PIL import Image

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
from build_pack import cropped  # noqa: E402
from shrink import shrink  # noqa: E402

SHEET = HERE.parent.parent / "content" / "Map" / "props.sheet"


def main():
    folder, listing = Path(sys.argv[1]), Path(sys.argv[2])
    anchors = {n: (int(x), int(y)) for n, x, y in (l.split() for l in listing.read_text().split("\n") if l)}
    names = sys.argv[3:] or list(anchors)
    lines = SHEET.read_text().split("\n")
    image_name = next(l[6:].strip() for l in lines if l.startswith("image="))
    sheet = Image.open(SHEET.parent / image_name).convert("RGBA")
    pieces = [(n, *cropped(folder / f"{n}.png", *anchors[n])) for n in names]
    # Same-size pictures are drawn over their old ones.
    frames = {l.split(",")[-1]: l for l in lines if l.startswith("frame=")}
    redrawn = []
    for name, im, ax, ay in pieces:
        if name in frames:
            fx, fy, fw, fh = map(int, frames[name][6:].split(",")[:4])
            if (fw, fh) == im.size:
                sheet.paste(im, (fx, fy))
                lines[lines.index(frames[name])] = f"frame={fx},{fy},{fw},{fh},{ax},{ay},{name}"
                redrawn.append(name)
    pieces = [p for p in pieces if p[0] not in redrawn]
    if not pieces:
        sheet.save(SHEET.parent / image_name)
        SHEET.write_text("\n".join(lines).rstrip("\n") + "\n")
        shrink(SHEET.parent / image_name)
        print(f"redrew {', '.join(redrawn)} in {SHEET}")
        return
    # Lay the new pictures out left to right on one strip.
    width = max(sheet.width, sum(im.width + 2 for _, im, _, _ in pieces))
    strip = max(im.height for _, im, _, _ in pieces) + 2
    grown = Image.new("RGBA", (width, sheet.height + strip))
    grown.paste(sheet, (0, 0))
    x = 0
    added = {name for name, _, _, _ in pieces}
    lines = [l for l in lines if not (l.startswith("frame=") and l.split(",")[-1] in added)]
    while lines and not lines[-1]:
        lines.pop()
    for name, im, ax, ay in pieces:
        grown.paste(im, (x, sheet.height))
        lines.append(f"frame={x},{sheet.height},{im.width},{im.height},{ax},{ay},{name}")
        x += im.width + 2
    grown.save(SHEET.parent / image_name)
    SHEET.write_text("\n".join(lines) + "\n")
    shrink(SHEET.parent / image_name)
    print(f"added {', '.join(added)} to {SHEET}" + (f", redrew {', '.join(redrawn)}" if redrawn else ""))


if __name__ == "__main__":
    main()
