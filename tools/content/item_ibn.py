"""Reads the original's item table, System/Game/Parameter/Item.Ibn, from the
player's own copy of the game (docs/formats.md, Items).

The file is RCLIB-packed after every byte went through a 256-byte table
that ShadowFlare.exe builds on its stack (0x463031..0x46376e), so the
table is read out of the player's exe rather than carried here. Records
come back as dicts with the fields src/formats/item_data.cpp reads.

    python3 tools/content/item_ibn.py <game folder>     # a summary
"""

import collections
import struct
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent.parent / "re"))
from rclib import lzss  # noqa: E402

CLASSES = ["weapon", "armour", "accessory", "consumable", "special"]
ELEMENTS = ["fire", "water", "earth", "thunder", "holy", "dark", "gel", "metal"]

# The part every class shares, in file order.
COMMON = ["subtype", "code", "quality", "pool", "drop_weight", "price", "fixed_price", "w", "h", "load",
          "inv_bank", "inv_pattern", "ground_dir", "ground_picture", "inv_param", "ground_r", "ground_g",
          "ground_b", "glow", "glow_r", "glow_g", "glow_b", "page", "page_x", "page_y"]
# Each class's tail, in file order: (field, number of words).
TAIL = {
    0: [("durability", 1), ("stats", 10), ("item_level", 1), ("req_level", 1), ("unk_ec", 1), ("fx_tint", 3),
        ("layer", 1), ("layer_tint", 3), ("shot_kind", 1), ("shot_spread", 1), ("shot_speed", 1),
        ("shot_turn", 1), ("shot_pierces", 1), ("two_handed", 1), ("elements", 8), ("mods", 117),
        ("elem_mods", 24)],
    1: [("durability", 1), ("stats", 10), ("item_level", 1), ("req_level", 1), ("layer", 1), ("layer_tint", 3),
        ("elements", 8), ("mods", 117), ("elem_mods", 24)],
    2: [("item_level", 1), ("req_level", 1), ("mods", 117), ("elem_mods", 24)],
    3: [("item_level", 1), ("effect", 9)],
    4: [],
}


def substitution_table(exe):
    """The 256-byte table, from the mov-byte run at 0x463031 in the exe."""
    va, end, base = 0x463031, 0x46376e, 0x400000
    o, shift, t = va - base, 0, {}
    while va < end:
        b = exe[o:o + 8]
        if b[:3] == b"\xc6\x44\x24":
            disp, val, n = b[3], b[4], 5
        elif b[:3] == b"\xc6\x84\x24":
            disp, val, n = struct.unpack_from("<I", b, 3)[0], b[7], 8
        elif b[:3] == b"\x88\x5c\x24":  # mov [esp+d8], bl (bl is 0)
            disp, val, n = b[3], 0, 4
        elif b[:3] == b"\x88\x9c\x24":
            disp, val, n = struct.unpack_from("<I", b, 3)[0], 0, 7
        elif b[0] == 0x53:  # push ebx
            shift, va, o = shift + 4, va + 1, o + 1
            continue
        elif b[0] == 0x68:  # push imm32
            shift, va, o = shift + 4, va + 5, o + 5
            continue
        elif b[:3] == b"\x8d\x4c\x24":  # lea ecx, [esp+d8]
            va, o = va + 4, o + 4
            continue
        else:
            raise SystemExit(f"ShadowFlare.exe: unexpected bytes {b.hex()} at {va:#x}; not the expected version?")
        t[disp - shift] = val
        va, o = va + n, o + n
    table = [t[0x3c + i] for i in range(256)]
    if len(set(table)) != 256:
        raise SystemExit("ShadowFlare.exe: the item byte table is not a permutation")
    return table


def unpack(game):
    game = Path(game)
    raw = (game / "System/Game/Parameter/Item.Ibn").read_bytes()
    if raw[:15] != b"SFItemDataV0000" or raw[24:32] != b"RCLIB-L\x1a":
        raise SystemExit("Item.Ibn: not an item table")
    checksum = struct.unpack_from("<i", raw, 16)[0]
    size, packed = struct.unpack_from("<II", raw, 32)
    table = substitution_table((game / "ShadowFlare.exe").read_bytes())
    out, _ = lzss(bytes(table[x] for x in raw[40:40 + packed]), size)
    if len(out) != size or sum(x - 256 if x > 127 else x for x in out) != checksum:
        raise SystemExit("Item.Ibn: checksum does not match")
    return out


def read_items(game):
    """Every item record, in file order."""
    data = unpack(game)
    o = 0

    def i32():
        nonlocal o
        o += 4
        return struct.unpack_from("<i", data, o - 4)[0]

    def text():
        nonlocal o
        n = i32()
        o += n
        return bytes(~x & 255 for x in data[o - n:o]).decode("cp932", "replace")

    items = []
    for cls in range(5):
        for _ in range(i32()):
            r = {"class": cls, "name": text(), "base": text()}
            for field in COMMON:
                r[field] = i32()
            for field, n in TAIL[cls]:
                r[field] = i32() if n == 1 else [i32() for _ in range(n)]
            items.append(r)
    if o != len(data):
        raise SystemExit("Item.Ibn: records do not fill the file")
    return items


def main():
    items = read_items(sys.argv[1])
    print(f"{len(items)} items")
    for cls, name in enumerate(CLASSES):
        q = collections.Counter(r["quality"] for r in items if r["class"] == cls)
        print(f"  {name:10} " + ", ".join(f"quality {k}: {v}" for k, v in sorted(q.items())))


if __name__ == "__main__":
    main()
