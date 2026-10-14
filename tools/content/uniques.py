#!/usr/bin/env python3
"""Writes the open pack's unique items, content/uniques.txt, from the
original's item table. Needs the player's own copy of the game.

    python3 tools/content/uniques.py <game folder> <content pack>

Every unique item (quality 2) named in unique_names.txt takes the
original's numbers: size, price, load, durability, stats, elements, item
and required level, hero layer, shot, modifiers, drop weight and drop
pool, so it plays and drops as the original's does. The names, the
unidentified names and the pictures are ours: unique_names.txt gives each
item our name and a kind, and tools/art/uniques.py draws its pictures
(frame n of a class's sheet is that class's n-th item in the file).
"""

import sys
from pathlib import Path

from item_ibn import CLASSES, ELEMENTS, read_items

HERE = Path(__file__).resolve().parent

# Modifier names, in the original's order (docs/content.md).
MODIFIERS = ["attack", "defense", "hit", "evasion", "magic-attack", "magic-defense", "magic-hit", "magic-evasion",
             "attack-speed", "walk-speed", "max-hp", "max-mp", "strength", "magic-level", "stun-chance",
             "stun-length", "high-damage", "life-recovery", "mental-recovery", "mental-cost", "reflection-chance",
             "reflection-rate", "healing-potions", "mental-potions", "absorption-chance", "absorption-rate", "gold",
             "mine-power", "mine-count", "chant-speed", "companion-hp", "companion-attack", "companion-hit",
             "companion-defense", "companion-evasion", "companion-magic-attack", "companion-magic-hit",
             "companion-magic-defense", "companion-magic-evasion"]

# Each class's pictures: inventory bank (System/Game/Pattern/Item%04d) and
# ground folder (Character/ITEM/%08d/Pattern).
PICTURES = {"accessory": (1, 8), "weapon": (2, 9), "armour": (3, 10)}

# The name shown before an item is identified, by kind.
UNIDENTIFIED = {"ring": "Ring", "darkring": "Ring", "amulet": "Amulet", "pendant": "Pendant",
                "bracelet": "Bracelet", "beads": "Beads", "crystal": "Crystal", "locket": "Locket", "gem": "Gem",
                "waystone": "Stone", "shortsword": "Short Sword", "longsword": "Long Sword",
                "bastard": "Bastard Sword", "curved": "Curved Sword", "twohand": "Two-handed Sword",
                "greatsword": "Great Sword", "axe": "Axe", "mace": "Mace", "bowgun": "Bowgun", "sling": "Slingshot",
                "staff": "Staff", "rod": "Rod", "orbrod": "Orb Rod", "cloth": "Cloth Armour", "plate": "Plate Mail",
                "hunter": "Hunting Coat", "robe": "Robe", "chain": "Chain Mail", "roundshield": "Round Shield",
                "towershield": "Tower Shield", "boardshield": "Kite Shield", "book": "Book",
                "knuckle": "Knuckle Guard", "helm": "Helm", "hat": "Hat", "hood": "Hood", "mask": "Mask",
                "circlet": "Circlet", "horned": "Horned Helm", "boots": "Boots", "greaves": "Greaves"}

# Hero layers the open pack's hero does not have yet, drawn as the nearest
# one it has: bastard sword as long sword, rod and staff, great sword and
# axe as two-handed sword, hunter's cloth and robe as cloth, tower and
# board shields as the round shield. The glow layers (3, 4) are left out.
OPEN_LAYERS = {5, 6, 9, 12, 16, 17, 18, 19}
LAYER_STAND_IN = {14: 17, 15: 18, 20: 19, 21: 19, 7: 5, 8: 5, 10: 9, 11: 9}


def read_names():
    names = []
    for line in (HERE / "unique_names.txt").read_text(encoding="utf-8").splitlines():
        if not line.strip() or line.startswith("#"):
            continue
        cls, code, kind, name = line.split(None, 3)
        if kind not in UNIDENTIFIED:
            raise SystemExit(f"unique_names.txt: unknown kind {kind}")
        names.append((cls, int(code), kind, name.strip()))
    return names


def numbers(values):
    return ",".join(str(v) for v in values)


def section(r, kind, name, frame):
    cls = CLASSES[r["class"]]
    bank, folder = PICTURES[cls]
    out = [f"[item {cls} {r['code']}]", f"name={name}", f"unidentified={UNIDENTIFIED[kind]}", "quality=2",
           f"subtype={r['subtype']}", f"size={r['w']},{r['h']}", f"price={r['price']}"]
    if r["fixed_price"]:
        out.append("fixed-price=1")
    if "durability" in r and r["durability"]:
        out.append(f"durability={r['durability']}")
    out += [f"icon={bank},{frame}", f"ground={folder},{frame}", f"level={r['item_level']},{r['req_level']}"]
    if any(r.get("stats", [])):
        out.append("stats=" + numbers(trim(r["stats"])))
    if any(r.get("elements", [])):
        out.append("elements=" + numbers(trim(r["elements"])))
    if "layer" in r:
        layer = r["layer"]
        if layer >= 0 and layer not in OPEN_LAYERS:
            layer = LAYER_STAND_IN[layer]
        out.append(f"layer={layer}")
    if r["class"] == 0:
        if r["shot_kind"] != -1:
            shot = [r["shot_kind"], r["shot_spread"], r["shot_speed"], r["shot_turn"], r["shot_pierces"]]
            out.append("shot=" + numbers(shot if shot[3] or shot[4] else shot[:3]))
        if r["two_handed"]:
            out.append("two-handed=1")
    out.append(f"weight={r['drop_weight']}")
    if r["pool"] != 1:
        out.append(f"pool={r['pool']}")
    if r["load"]:
        out.append(f"load={r['load']}")
    for i, mod in enumerate(MODIFIERS):
        least, most, chance = r["mods"][3 * i:3 * i + 3]
        if chance:
            out.append(f"mod={mod},{least},{most}" + (f",{chance}" if chance != 100 else ""))
    for i, element in enumerate(ELEMENTS):
        least, most, chance = r["elem_mods"][3 * i:3 * i + 3]
        if chance:
            out.append(f"element-mod={element},{least},{most}" + (f",{chance}" if chance != 100 else ""))
    return "\n".join(out) + "\n"


def trim(values):
    """Leaves off trailing zeros, which the pack reads as zeros anyway."""
    values = list(values)
    while values and values[-1] == 0:
        values.pop()
    return values


def main():
    game, pack = Path(sys.argv[1]), Path(sys.argv[2])
    items = read_items(game)
    by_id = {(CLASSES[r["class"]], r["code"]): r for r in items if r["quality"] == 2}
    theirs = {r["name"].strip().lower() for r in items} | {r["base"].strip().lower() for r in items}
    names = read_names()
    seen = set()
    for cls, code, kind, name in names:
        if (cls, code) not in by_id:
            raise SystemExit(f"unique_names.txt: {cls} {code} is not one of the original's unique items")
        if (cls, code) in seen:
            raise SystemExit(f"unique_names.txt: {cls} {code} is named twice")
        if name.lower() in theirs:
            raise SystemExit(f"unique_names.txt: {name} is the original's name; give it ours")
        seen.add((cls, code))
    parts = ["# The original's unique items with our names and pictures. Made by\n"
             "# tools/content/uniques.py from the player's own Item.Ibn: the numbers are\n"
             "# the original's, so each plays and drops as it did there.\n"]
    for cls in CLASSES:
        named = [n for n in names if n[0] == cls]
        if not named:
            continue
        missing = sum(1 for (c, _) in by_id if c == cls) - len(named)
        parts.append(f"\n# {cls.capitalize()}: {len(named)} items" + (f" ({missing} still to come)" if missing else "")
                     + "\n\n")
        parts += [section(by_id[(c, code)], kind, name, frame) + "\n" for frame, (c, code, kind, name) in
                  enumerate(named)]
    (pack / "uniques.txt").write_text("".join(parts).rstrip("\n") + "\n", encoding="utf-8", newline="\n")
    print(f"wrote {len(names)} unique items to {pack / 'uniques.txt'}")


if __name__ == "__main__":
    main()
