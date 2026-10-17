#!/usr/bin/env python3
"""Writes content/item_skin.txt: which of our item pictures stand in for the
original's, so a player's copy drawn with our art (--our-art, F12) shows our
bag and ground pictures instead of the original's. Needs the player's own
copy of the game.

    python3 tools/content/item_skin.py <game folder> <content pack>

The original's item table gives every item an inventory picture (bank,
pattern) and a ground picture (folder, picture). An item that exists in
our items.txt or uniques.txt under the same class and code (the base
items and the unique items) lends its pictures to every original picture
it shares: a prefixed variant such as Speed Short Sword uses its base
item's pictures. Pictures nobody here has drawn are left out, and the
engine draws the original's in their place.

Lines: `icon <bank>,<pattern> = <our bank>,<frame>` and
`ground <folder>,<picture> = <our folder>,<frame>`.
"""

import re
import sys
from pathlib import Path

from item_ibn import CLASSES, read_items


def our_pictures(pack):
    """(class index, code) -> (icon bank, icon frame, ground folder, ground frame)."""
    found = {}
    for name in ("items.txt", "uniques.txt"):
        text = (pack / name).read_text(encoding="utf-8")
        for block in re.split(r"^(?=\[item )", text, flags=re.M):
            head = re.match(r"\[item (\w+) (-?\d+)\]", block)
            if not head:
                continue
            icon = re.search(r"^icon=(\d+),(\d+)", block, re.M)
            ground = re.search(r"^ground=(\d+),(\d+)", block, re.M)
            if icon:
                found[(CLASSES.index(head.group(1)), int(head.group(2)))] = (
                    int(icon.group(1)), int(icon.group(2)),
                    int(ground.group(1)) if ground else -1, int(ground.group(2)) if ground else -1)
    return found


def main():
    if len(sys.argv) != 3:
        sys.exit(__doc__)
    items = read_items(sys.argv[1])
    pack = Path(sys.argv[2])
    ours = our_pictures(pack)
    icons, grounds = {}, {}
    # Base items first, so a picture shared with a unique keeps the base's.
    for item in sorted(items, key=lambda i: i["quality"]):
        mine = ours.get((item["class"], item["code"]))
        if not mine:
            continue
        if item["inv_bank"] >= 0:
            icons.setdefault((item["inv_bank"], item["inv_pattern"]), mine[:2])
        if item["ground_dir"] >= 0 and mine[2] >= 0:
            grounds.setdefault((item["ground_dir"], item["ground_picture"]), mine[2:])
    out = ["# Our pictures for the original's items, by the original's picture numbers:",
           "# made by tools/content/item_skin.py from the player's Item.Ibn.", ""]
    out += [f"icon {k[0]},{k[1]} = {v[0]},{v[1]}" for k, v in sorted(icons.items())]
    out += [""] + [f"ground {k[0]},{k[1]} = {v[0]},{v[1]}" for k, v in sorted(grounds.items())]
    (pack / "item_skin.txt").write_text("\n".join(out) + "\n", encoding="utf-8")
    all_icons = {(i["inv_bank"], i["inv_pattern"]) for i in items if i["inv_bank"] >= 0}
    all_ground = {(i["ground_dir"], i["ground_picture"]) for i in items if i["ground_dir"] >= 0}
    print(f"icons: {len(icons)} of {len(all_icons)} drawn; ground: {len(grounds)} of {len(all_ground)} drawn")


if __name__ == "__main__":
    main()
