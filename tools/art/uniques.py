"""Pictures of the open pack's unique items (content/uniques.txt), rendered
with studio.py like tools/art/items.py.

    python tools/art/uniques.py render <folder> [--class accessory] [--only N,M]
    python3 tools/art/uniques.py pack <folder> <content pack folder>

The first step needs a Python with the `bpy` module and renders every
unique item into <folder>; the second needs Pillow and lays the renders
out as the pack's sheets: System/Game/Pattern/Item0001-0003 (accessories,
weapons, armour) in the bag, Character/ITEM/00000008-00000010/Pattern on
the ground. Frame n of a class is that class's n-th item in uniques.txt.

Each item is one of a few models (its kind in tools/content/unique_names.txt:
ring, bracelet, amulet, ...) made its own: the metal follows its level
(copper, silver, gold, then blackened steel for the deepest), the stones
and glow follow what it does (its strongest element, else its main
modifier: red for attack and strength, blue for mental power, green for
hit and evasion, and so on), and the shapes (band, setting, number of
stones, chain) are picked from its code, so no two look alike. Nothing is
taken from the original's pictures.
"""

import math
import random
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
ROOT = HERE.parent.parent

try:
    import bpy

    sys.path.insert(0, str(HERE))
    import creatures  # noqa: E402
    import items  # noqa: E402
    import studio  # noqa: E402
except ImportError:  # packing only
    bpy = None

CELL = 32
# Class -> (inventory bank, ground folder), as tools/content/uniques.py.
PICTURES = {"accessory": (1, 8), "weapon": (2, 9), "armour": (3, 10)}

ELEMENTS = ["fire", "water", "earth", "thunder", "holy", "dark", "gel", "metal"]
ELEMENT_COLOUR = {
    "fire": (0.95, 0.30, 0.08), "water": (0.15, 0.42, 0.95), "earth": (0.62, 0.42, 0.16),
    "thunder": (0.98, 0.86, 0.20), "holy": (0.96, 0.94, 0.80), "dark": (0.45, 0.12, 0.62),
    "gel": (0.30, 0.85, 0.35), "metal": (0.72, 0.76, 0.82),
}
# The colour of what an item mainly does, when no element stands out.
MOD_COLOUR = [
    (("attack", "strength", "high-damage", "stun-chance", "stun-length"), (0.85, 0.12, 0.10)),
    (("defense", "magic-defense", "reflection-chance", "reflection-rate"), (0.62, 0.70, 0.85)),
    (("hit", "evasion", "magic-hit", "magic-evasion"), (0.20, 0.72, 0.30)),
    (("magic-attack", "magic-level", "chant-speed", "mental-cost"), (0.58, 0.22, 0.85)),
    (("max-hp", "life-recovery", "healing-potions", "absorption-chance", "absorption-rate"), (0.80, 0.06, 0.22)),
    (("max-mp", "mental-recovery", "mental-potions"), (0.12, 0.35, 0.95)),
    (("attack-speed", "walk-speed"), (0.20, 0.80, 0.85)),
    (("gold", "mine-power", "mine-count"), (0.98, 0.62, 0.12)),
]
COMPANION_COLOUR = (0.92, 0.58, 0.20)

# Metals by item level: (colour, dark).
METALS = [
    (23, "copper", (0.84, 0.50, 0.30), (0.42, 0.20, 0.10)),
    (40, "silver", (0.86, 0.88, 0.90), (0.40, 0.42, 0.46)),
    (55, "gold", (0.98, 0.78, 0.30), (0.55, 0.36, 0.08)),
    (999, "black steel", (0.30, 0.30, 0.34), (0.08, 0.08, 0.10)),
]

# Kind -> ground scale (relative to the hero, as items.py), and how the
# model is built: "thing" stands up facing -Y; "flat" lies in the XY plane,
# its top towards +Y, and is stood up for the bag picture.
KINDS = {
    "ring": (2.2, "thing"), "darkring": (2.2, "thing"), "bracelet": (1.7, "thing"), "amulet": (1.6, "flat"),
    "pendant": (1.6, "flat"), "beads": (1.6, "flat"), "crystal": (1.7, "thing"), "gem": (2.2, "thing"),
    "locket": (1.8, "thing"), "waystone": (1.1, "flat"),
    # Weapons are built point up along +Z, their flat facing -Y.
    "shortsword": (1.38, "upright"), "longsword": (1.26, "upright"), "bastard": (1.20, "upright"),
    "curved": (1.26, "upright"), "twohand": (1.20, "upright"), "greatsword": (1.20, "upright"),
    "axe": (1.20, "upright"), "mace": (1.20, "upright"), "bowgun": (1.56, "upright"), "sling": (1.68, "upright"),
    "staff": (0.85, "upright"), "rod": (1.38, "upright"), "orbrod": (1.38, "upright"),
}

# Weapon kind -> (length of the blade or head, its width).
BLADES = {"shortsword": (0.55, 0.06), "longsword": (0.8, 0.065), "bastard": (0.9, 0.08), "curved": (0.82, 0.05),
          "twohand": (1.05, 0.09), "greatsword": (1.15, 0.13)}


def read_uniques(pack):
    """The items of uniques.txt in order, each a dict of its keys (mod and
    element-mod lists), with its kind from unique_names.txt."""
    kinds = {}
    for line in (ROOT / "tools/content/unique_names.txt").read_text(encoding="utf-8").splitlines():
        if line.strip() and not line.startswith("#"):
            cls, code, kind, _ = line.split(None, 3)
            kinds[(cls, int(code))] = kind
    out, item = [], None
    for line in (Path(pack) / "uniques.txt").read_text(encoding="utf-8").splitlines():
        if line.startswith("[item "):
            _, cls, code = line.strip("[]").split()
            item = {"class": cls, "code": int(code), "kind": kinds[(cls, int(code))], "mod": [], "element-mod": []}
            out.append(item)
        elif item is not None and "=" in line and not line.startswith("#"):
            key, value = line.split("=", 1)
            if key in ("mod", "element-mod"):
                name, least, most = value.split(",")[:3]
                item[key].append((name, int(least), int(most)))
            else:
                item[key] = value
    return out


def look(item):
    """(metal, stone colour, glow strength, random) for an item."""
    level = int(item["level"].split(",")[0])
    metal = next(m for m in METALS if level <= m[0])
    rng = random.Random(item["code"] * 7919 + 13)
    best = max(item["element-mod"], key=lambda e: e[2], default=None)
    if best and best[2] > 0:
        colour = ELEMENT_COLOUR[best[0]]
    else:
        score = {}
        for name, _, most in item["mod"]:
            if most <= 0:
                continue
            for names, c in MOD_COLOUR:
                if name in names:
                    score[c] = score.get(c, 0) + most
            if name.startswith("companion"):
                score[COMPANION_COLOUR] = score.get(COMPANION_COLOUR, 0) + most
        colour = max(score, key=score.get) if score else (0.8, 0.8, 0.85)
    # Deeper items glow more.
    glow = 0.4 + min(level, 60) / 30
    return metal, colour, glow, rng


def jitter(colour, rng, amount=0.06):
    return tuple(min(1, max(0, c + rng.uniform(-amount, amount))) for c in colour)


def torus(major, minor, material, rotation=(0, 0, 0), location=(0, 0, 0), segments=32):
    bpy.ops.mesh.primitive_torus_add(major_radius=major, minor_radius=minor, major_segments=segments,
                                     minor_segments=10)
    obj = studio.smooth(bpy.context.object)
    studio.assign(obj, material)
    obj.rotation_euler = rotation
    obj.location = location
    return obj


def faceted(size, material, location=(0, 0, 0), rotation=(0, 0, 0)):
    """A cut stone: an icosphere with flat faces."""
    bpy.ops.mesh.primitive_ico_sphere_add(subdivisions=1, radius=1)
    obj = bpy.context.object
    obj.scale = size
    obj.location = location
    obj.rotation_euler = rotation
    return studio.assign(obj, material)


def put(obj, location, rotation=None):
    obj.location = location
    if rotation is not None:
        obj.rotation_euler = rotation
    return obj


def stone_material(colour, glow, rng, name="stone"):
    return items.glass(f"{name} {rng.random():.4f}", jitter(colour, rng, 0.04), glow)


def metal_material(metal, rng):
    _, name, colour, dark = metal
    return studio.material(f"{name} {rng.random():.4f}", colour, dark, scale=30, rough=0.3, bump=0.15)


def build_accessory(item):
    """Builds an accessory; returns its objects."""
    shape = creatures.shape
    metal, colour, glow, rng = look(item)
    kind = item["kind"]
    gold = metal_material(metal, rng)
    gem = stone_material(colour, glow, rng)
    made = []
    add = made.append
    if kind in ("ring", "darkring"):
        major = rng.uniform(0.12, 0.15)
        minor = rng.uniform(0.022, 0.045)
        band = gold
        if kind == "darkring":
            band = studio.material(f"dark band {rng.random()}", (0.22, 0.18, 0.24), (0.05, 0.03, 0.06), scale=30,
                                   rough=0.35)
        add(torus(major, minor, band, (math.radians(90), 0, 0), (0, 0, major + minor)))
        top = 2 * major + minor
        style = rng.choice(("ball", "cut", "three", "crown", "signet")) if kind == "ring" else "orb"
        if style == "ball":
            add(put(shape("ball", (0.05, 0.05, 0.05), gem), (0, 0, top + 0.03)))
        elif style == "cut":
            add(put(faceted((0.06, 0.05, 0.05), gem), (0, 0, top + 0.035), (0, 0, rng.uniform(0, 1))))
            for k in range(4):  # claws
                a = k * math.pi / 2 + math.pi / 4
                add(put(shape("box", (0.012, 0.012, 0.03), gold), (0.045 * math.cos(a), 0.045 * math.sin(a), top + 0.02)))
        elif style == "three":
            for dx in (-0.06, 0, 0.06):
                r = 0.04 if dx == 0 else 0.028
                add(put(shape("ball", (r, r, r), gem), (dx, 0, top + r * 0.7 - abs(dx) * 0.3)))
        elif style == "crown":
            add(put(shape("cylinder", (0.06, 0.06, 0.02), gold), (0, 0, top + 0.01)))
            for k in range(6):
                a = k * math.pi / 3
                add(put(shape("cone", (0.012, 0.012, 0.03), gold), (0.055 * math.cos(a), 0.055 * math.sin(a), top + 0.05)))
            add(put(shape("ball", (0.04, 0.04, 0.04), gem), (0, 0, top + 0.05)))
        else:  # signet or orb: a flat plate with a stone, or a glowing orb in a cup
            if style == "signet":
                add(put(shape("cylinder", (0.07, 0.055, 0.018), gold), (0, 0, top + 0.01)))
                add(put(shape("cylinder", (0.045, 0.035, 0.012), gem), (0, 0, top + 0.03)))
            else:
                add(put(shape("cone", (0.05, 0.05, 0.03), band, rotation=(math.pi, 0, 0)), (0, 0, top + 0.02)))
                add(put(shape("ball", (0.05, 0.05, 0.05), stone_material(colour, glow * 2.5, rng)), (0, 0, top + 0.07)))
    elif kind == "bracelet":
        major = 0.17
        width = rng.uniform(1.4, 2.6)
        bangle = torus(major, 0.028, gold, (math.radians(90), 0, 0), (0, 0, major + 0.03), 40)
        bangle.scale = (1, 1, width)
        add(bangle)
        studs = rng.choice((3, 5, 7))
        for k in range(studs):
            a = math.pi / 2 + (k - (studs - 1) / 2) * 0.42
            r = 0.035 if k == studs // 2 else 0.022
            add(put(shape("ball", (r, r * 0.8, r), gem if k % 2 == studs // 2 % 2 else gold),
                    (major * 1.12 * math.cos(a), -0.0, major + 0.03 + major * 1.12 * math.sin(a) - 0.002)))
        if rng.random() < 0.5:  # a second, thinner band
            add(torus(major * 1.02, 0.01, gold, (math.radians(90), 0, 0), (0, 0.05, major + 0.03), 40))
    elif kind in ("amulet", "pendant"):
        # Lying flat, its top towards +Y: a chain loop and a hanging piece.
        chain = studio.material(f"chain {rng.random()}", metal[2], metal[3], scale=60, rough=0.35, bump=0.6)
        loop = torus(0.2, 0.01, chain, location=(0, 0.12, 0.012), segments=48)
        loop.scale = (0.85, 1.15, 1)
        add(loop)
        y = -0.12
        if kind == "amulet":
            r = rng.uniform(0.09, 0.12)
            add(put(shape("cylinder", (r, r, 0.022), gold), (0, y, 0.022)))
            add(torus(r, 0.012, gold, location=(0, y, 0.044)))
            add(put(faceted((r * 0.5, r * 0.5, 0.03), gem), (0, y, 0.05)))
            for k in range(rng.choice((0, 4, 6, 8))):
                a = k * 2 * math.pi / 8
                add(put(shape("ball", (0.012, 0.012, 0.012), gem), (r * 0.78 * math.cos(a), y + r * 0.78 * math.sin(a), 0.045)))
        else:
            style = rng.choice(("drop", "diamond", "star"))
            if style == "drop":
                add(put(shape("ball", (0.07, 0.1, 0.03), gem), (0, y, 0.03)))
                add(put(shape("cone", (0.05, 0.05, 0.05), gold, rotation=(math.radians(-90), 0, 0)), (0, y + 0.1, 0.03)))
            elif style == "diamond":
                add(put(shape("box", (0.07, 0.07, 0.022), gold, rotation=(0, 0, math.pi / 4)), (0, y, 0.022)))
                add(put(faceted((0.045, 0.045, 0.025), gem), (0, y, 0.045)))
            else:
                for k in range(5):
                    a = k * 2 * math.pi / 5 + math.pi / 2
                    add(put(shape("cone", (0.03, 0.03, 0.06), gold, rotation=(0, math.radians(90), a)),
                            (0.06 * math.cos(a), y + 0.06 * math.sin(a), 0.02)))
                add(put(shape("ball", (0.045, 0.045, 0.03), gem), (0, y, 0.03)))
            add(put(shape("ball", (0.02, 0.02, 0.02), gold), (0, y + 0.13, 0.02)))
    elif kind == "beads":
        wood = studio.material("bead wood", (0.55, 0.30, 0.16), (0.25, 0.12, 0.06), scale=20, rough=0.5)
        n = 22
        for k in range(n):
            a = k * 2 * math.pi / n
            m = gem if k % 4 == 0 else wood
            add(put(shape("ball", (0.025, 0.025, 0.025), m), (0.17 * math.cos(a), 0.05 + 0.2 * math.sin(a), 0.025)))
        add(put(shape("ball", (0.04, 0.04, 0.04), gem), (0, -0.19, 0.04)))
        add(put(shape("cone", (0.035, 0.035, 0.08), studio.material("tassel", (0.80, 0.70, 0.30), scale=60),
                      rotation=(math.radians(90), 0, 0)), (0, -0.29, 0.03)))
    elif kind == "crystal":
        sides = rng.choice((5, 6, 8))
        h = rng.uniform(0.22, 0.3)
        crystal = stone_material(colour, glow * 1.6, rng, "crystal")
        bpy.ops.mesh.primitive_cylinder_add(vertices=sides, radius=0.07, depth=h)
        body = studio.assign(bpy.context.object, crystal)
        add(put(body, (0, 0, 0.06 + h / 2)))
        bpy.ops.mesh.primitive_cone_add(vertices=sides, radius1=0.07, depth=0.09)
        add(put(studio.assign(bpy.context.object, crystal), (0, 0, 0.06 + h + 0.045)))
        add(put(shape("cylinder", (0.1, 0.1, 0.03), gold), (0, 0, 0.03)))
        for k in range(3):  # claws holding it
            a = k * 2 * math.pi / 3
            add(put(shape("box", (0.012, 0.012, 0.05), gold), (0.075 * math.cos(a), 0.075 * math.sin(a), 0.09)))
    elif kind == "gem":
        add(put(faceted((0.14, 0.12, 0.12), gem), (0, 0, 0.12), (rng.uniform(0, 1), rng.uniform(0, 1), 0)))
        if rng.random() < 0.6:
            add(torus(0.13, 0.012, gold, (math.radians(90), 0, 0), (0, 0, 0.12)))
    elif kind == "locket":
        wood = studio.material("case wood", (0.45, 0.24, 0.14), (0.18, 0.08, 0.04), scale=20, rough=0.6)
        add(put(shape("box", (0.16, 0.11, 0.07), wood), (0, 0, 0.07)))
        add(put(shape("box", (0.165, 0.115, 0.03), gold), (0, 0, 0.16)))
        add(put(shape("box", (0.03, 0.01, 0.04), gold), (0, -0.115, 0.12)))
        for x in (-0.09, 0.09):
            add(put(shape("ball", (0.03, 0.03, 0.03), gem if x < 0 else stone_material((0.15, 0.40, 0.95), glow, rng)),
                    (x, 0, 0.21)))
    elif kind == "waystone":
        # A rough slab lying on its back, a ring of glowing runes cut in it.
        rock = studio.material(f"waystone {rng.random()}", (0.50, 0.50, 0.52), (0.22, 0.22, 0.25), scale=8,
                               rough=0.9, bump=0.6)
        wide = item["size"] == "5,3"
        w, l = (0.75, 0.42) if wide else (0.42, 0.6)
        slab = shape("box", (w, l, 0.14), rock)
        slab.modifiers["soft"].levels = 2
        studio.displace(slab, 0.05, 0.35, seed=item["code"] % 97 + 1)
        add(put(slab, (0, 0, 0.14)))
        rune = stone_material(colour if item["element-mod"] else (0.55, 0.85, 1.0), glow * 2, rng, "rune")
        n = 7
        for k in range(n):
            a = k * 2 * math.pi / n
            add(put(shape("box", (0.03, 0.012, 0.012), rune, rotation=(0, 0, a + rng.uniform(-0.4, 0.4))),
                    (0.22 * math.cos(a) * (1.4 if wide else 0.9), 0.22 * math.sin(a), 0.3)))
        add(put(shape("cylinder", (0.07, 0.07, 0.01), rune), (0, 0, 0.3)))
    else:
        raise SystemExit(f"no model for kind {kind}")
    return made


def hard(obj):
    """Keeps a box's edges sharp (blades, axe heads)."""
    obj.modifiers["soft"].levels = 0
    obj.modifiers["soft"].render_levels = 0
    return obj


def steel_material(item, colour, rng):
    """Blade steel, a little tinted by the item's element; deep items
    darker or brighter."""
    level = int(item["level"].split(",")[0])
    base = (0.78, 0.80, 0.84)
    best = max(item["element-mod"], key=lambda e: e[2], default=None)
    mix = 0.25 if best and best[2] > 0 else 0.08
    if best and best[0] == "dark" and best[2] > 0:
        base, mix = (0.25, 0.22, 0.28), 0.2
    c = tuple(b * (1 - mix) + k * mix for b, k in zip(base, colour))
    m = studio.material(f"steel {rng.random():.4f}", c, tuple(x * 0.45 for x in c), scale=40, rough=0.25, bump=0.1)
    if level >= 42:  # deep blades shine faintly in their colour
        bsdf = m.node_tree.nodes["Principled BSDF"]
        bsdf.inputs["Emission Color"].default_value = (*studio.linear(colour), 1)
        bsdf.inputs["Emission Strength"].default_value = 0.15 + (level - 42) / 60
    return m


def wood_material(rng, dark=False):
    c = (0.38, 0.22, 0.12) if dark else rng.choice(((0.55, 0.34, 0.18), (0.48, 0.30, 0.16), (0.62, 0.42, 0.24)))
    return studio.material(f"wood {rng.random():.4f}", c, tuple(x * 0.45 for x in c), scale=8, rough=0.6,
                           stretch=(1, 1, 0.1))


def leather_material(rng):
    c = rng.choice(((0.30, 0.18, 0.10), (0.22, 0.12, 0.08), (0.18, 0.18, 0.22), (0.35, 0.10, 0.08)))
    return studio.material(f"grip {rng.random():.4f}", c, tuple(x * 0.4 for x in c), scale=60, rough=0.7, bump=0.6)


def hilt(z, metal, gem, rng, two_hands=False, style=None):
    """Grip, guard and pommel below z (the blade starts at z); returns
    the objects."""
    shape = creatures.shape
    grip_len = 0.32 if two_hands else 0.2
    made = [put(shape("cylinder", (0.025, 0.025, grip_len / 2), leather_material(rng)), (0, 0, z - grip_len / 2))]
    style = style or rng.choice(("bar", "bar", "swept", "disc", "winged"))
    if style == "bar":
        made.append(put(shape("box", (0.16 + rng.uniform(0, 0.06), 0.03, 0.025), metal), (0, 0, z)))
        for side in (-1, 1):
            made.append(put(shape("ball", (0.035, 0.035, 0.035), metal), (side * (0.19), 0, z)))
    elif style == "swept":
        for side in (-1, 1):
            made.append(put(shape("box", (0.11, 0.025, 0.02), metal, rotation=(0, side * 0.45, 0)),
                            (side * 0.1, 0, z + 0.04)))
    elif style == "disc":
        made.append(put(shape("cylinder", (0.07, 0.07, 0.015), metal), (0, 0, z)))
    else:
        for side in (-1, 1):
            made.append(put(shape("cone", (0.04, 0.03, 0.12), metal, rotation=(0, side * math.radians(70), 0)),
                            (side * 0.11, 0, z + 0.03)))
        made.append(put(shape("ball", (0.035, 0.03, 0.035), gem), (0, -0.01, z)))
    pommel = gem if rng.random() < 0.5 else metal
    made.append(put(shape("ball", (0.04, 0.04, 0.045), pommel), (0, 0, z - grip_len - 0.03)))
    return made


def blade(kind, item, steel, metal, gem, rng):
    shape = creatures.shape
    length, width = BLADES[kind]
    width *= rng.uniform(0.85, 1.2)
    made = []
    if kind == "curved":
        # A single edge curving back to its point: one mesh along an arc.
        bend = rng.uniform(0.25, 0.4)  # radians from hilt to point
        verts, faces, n = [], [], 16
        for k in range(n + 1):
            t = k / n
            a = bend * t * t
            x, z = -0.5 * bend * length * t * t * 0.6, 0.02 + length * t
            w = width * (1 - 0.3 * t) if k < n else 0.002
            back, edge = (x - w * 0.15 * math.cos(a), z), (x + w * math.cos(a), z - w * math.sin(a) * 0.5)
            if k == n:
                edge = (x - 0.01, z + 0.03)
            for px, pz in (back, edge):
                verts += [(px, -0.012, pz), (px, 0.012, pz)]
        for k in range(n):
            i = 4 * k
            faces += [(i, i + 2, i + 6, i + 4), (i + 1, i + 5, i + 7, i + 3), (i, i + 4, i + 5, i + 1),
                      (i + 2, i + 3, i + 7, i + 6)]
        faces += [(0, 1, 3, 2), (4 * n, 4 * n + 2, 4 * n + 3, 4 * n + 1)]
        mesh = bpy.data.meshes.new("curved blade")
        mesh.from_pydata(verts, [], faces)
        mesh.validate()
        obj = bpy.data.objects.new("curved blade", mesh)
        bpy.context.scene.collection.objects.link(obj)
        made.append(studio.assign(obj, steel))
        made.append(put(shape("cylinder", (0.06, 0.07, 0.012), metal), (0, 0, 0.0)))  # its round guard
        return made, False
    style = rng.choice(("straight", "straight", "leaf", "fuller", "flame"))
    made.append(put(hard(shape("box", (width / 2, 0.012, length / 2), steel)), (0, 0, length / 2)))
    if style == "leaf":
        made.append(put(shape("ball", (width * 0.75, 0.014, length * 0.25), steel), (0, 0, length * 0.62)))
    elif style == "fuller":
        made.append(put(shape("box", (width * 0.12, 0.016, length * 0.35), gem), (0, 0, length * 0.38)))
    elif style == "flame":
        for k in range(6):
            side = 1 if k % 2 else -1
            made.append(put(shape("cone", (0.02, 0.01, 0.04), steel, rotation=(0, side * math.radians(80), 0)),
                            (side * width / 2, 0, length * (0.15 + 0.12 * k))))
    made.append(put(shape("cone", (width / 2 * 1.02, 0.012, width * 0.9), steel), (0, 0, length + width * 0.88)))
    hard(made[-1])
    return made, kind in ("twohand", "greatsword", "bastard")


def build_weapon(item):
    """Builds a weapon point up along +Z, its flat towards -Y."""
    shape = creatures.shape
    metal_info, colour, glow, rng = look(item)
    kind = item["kind"]
    metal = metal_material(metal_info, rng)
    gem = stone_material(colour, glow, rng)
    if kind in BLADES:
        steel = steel_material(item, colour, rng)
        made, two = blade(kind, item, steel, metal, gem, rng)
        return made + hilt(0, metal, gem, rng, two_hands=two or kind == "curved",
                           style="disc" if kind == "curved" else None)
    made = []
    if kind in ("axe", "mace"):
        haft = wood_material(rng, dark=rng.random() < 0.4)
        made.append(put(shape("cylinder", (0.03, 0.03, 0.55), haft), (0, 0, 0.55)))
        made.append(put(shape("ball", (0.045, 0.045, 0.04), metal), (0, 0, 0.0)))
        if kind == "mace":
            made.append(put(shape("ball", (0.12, 0.12, 0.14), metal), (0, 0, 1.15)))
            for k in range(6):
                a = k * math.pi / 3
                made.append(put(shape("box", (0.1, 0.012, 0.11), metal, rotation=(0, 0, a)), (0, 0, 1.15)))
            made.append(put(shape("ball", (0.045, 0.045, 0.045), gem), (0, 0, 1.3)))
        else:
            steel = steel_material(item, colour, rng)
            double = rng.random() < 0.5
            size = rng.uniform(0.17, 0.24)
            for side in ((-1, 1) if double else (1,)):
                made.append(put(hard(shape("box", (size * 0.4, 0.02, 0.05), steel)), (side * size * 0.4, 0, 0.98)))
                bit = shape("cylinder", (size * 0.5, size, 0.022), steel, rotation=(math.radians(90), 0, 0))
                made.append(put(bit, (side * size * 0.8, 0, 0.98)))
            made.append(put(shape("cone", (0.03, 0.03, 0.08), metal), (0, 0, 1.15)))
            made.append(put(shape("cylinder", (0.04, 0.04, 0.04), metal), (0, 0, 0.98)))
            made.append(put(shape("ball", (0.03, 0.02, 0.03), gem), (0, -0.03, 0.98)))
        return made
    if kind in ("bowgun", "sling"):
        wood = wood_material(rng)
        made.append(put(shape("box", (0.045, 0.035, 0.38), wood), (0, 0, 0.38)))  # the stock
        made.append(put(shape("box", (0.06, 0.05, 0.08), wood), (0, 0.0, 0.0)))   # its butt
        if kind == "sling":
            for side in (-1, 1):
                made.append(put(shape("cylinder", (0.025, 0.025, 0.14), wood, rotation=(0, side * 0.5, 0)),
                                (side * 0.07, 0, 0.86)))
            band = studio.material("band", (0.35, 0.22, 0.14), scale=40, rough=0.6)
            made.append(put(shape("box", (0.14, 0.008, 0.01), band), (0, 0, 0.92)))
            made.append(put(shape("box", (0.03, 0.03, 0.04), metal), (0, 0, 0.55)))
            made.append(put(shape("ball", (0.03, 0.03, 0.03), gem), (0, -0.03, 0.45)))
            return made
        span = rng.uniform(0.3, 0.42)
        prod = steel_material(item, colour, rng) if rng.random() < 0.6 else wood
        for side in (-1, 1):
            made.append(put(shape("box", (span / 2, 0.02, 0.025), prod, rotation=(0, side * 0.25, 0)),
                            (side * span / 2, 0, 0.66 - span * 0.12)))
            made.append(put(shape("box", (0.005, 0.005, span * 0.5), studio.material("string", (0.85, 0.82, 0.7)),
                                  rotation=(0, side * math.radians(62), 0)), (side * span * 0.46, 0, 0.52)))
        made.append(put(shape("box", (0.035, 0.03, 0.04), metal), (0, 0, 0.7)))
        made.append(put(shape("cylinder", (0.012, 0.012, 0.25), wood), (0, -0.04, 0.6)))  # the bolt
        made.append(put(shape("cone", (0.025, 0.025, 0.05), gem), (0, -0.04, 0.88)))
        made.append(put(shape("box", (0.02, 0.03, 0.05), metal), (0, 0.04, 0.3)))  # trigger
        if rng.random() < 0.5:
            made.append(put(shape("ball", (0.03, 0.03, 0.03), gem), (0, -0.035, 0.18)))
        return made
    # Staves and rods: a pole and a head.
    staff = kind == "staff"
    length = 1.4 if staff else 0.7
    pole = wood_material(rng) if staff or rng.random() < 0.4 else metal
    made.append(put(shape("cylinder", (0.026, 0.026, length / 2), pole), (0, 0, length / 2)))
    for k in range(rng.choice((1, 2, 3))):
        made.append(put(shape("cylinder", (0.034, 0.034, 0.015), metal), (0, 0, length * (0.3 + 0.2 * k))))
    head = stone_material(colour, glow * 1.5, rng, "head")
    if kind == "orbrod":
        r = 0.1
        made.append(put(shape("ball", (r, r, r), head), (0, 0, length + r * 0.9)))
        made.append(put(torus(r * 1.05, 0.012, metal, (math.radians(90), 0, 0)), (0, 0, length + r * 0.9)))
        made.append(put(shape("cone", (0.06, 0.06, 0.05), metal, rotation=(math.pi, 0, 0)), (0, 0, length + 0.02)))
        return made
    style = rng.choice(("crystal", "ring", "crook", "claw"))
    top = length
    if style == "crystal":
        bpy.ops.mesh.primitive_cylinder_add(vertices=6, radius=0.045, depth=0.16)
        made.append(put(studio.assign(bpy.context.object, head), (0, 0, top + 0.1)))
        bpy.ops.mesh.primitive_cone_add(vertices=6, radius1=0.045, depth=0.07)
        made.append(put(studio.assign(bpy.context.object, head), (0, 0, top + 0.215)))
    elif style == "ring":
        made.append(put(torus(0.1, 0.018, metal, (math.radians(90), 0, 0)), (0, 0, top + 0.11)))
        made.append(put(shape("ball", (0.05, 0.05, 0.05), head), (0, 0, top + 0.11)))
    elif style == "crook":
        made.append(put(torus(0.08, 0.022, pole, (math.radians(90), 0, 0)), (0.08, 0, top + 0.02)))
        made.append(put(shape("ball", (0.045, 0.045, 0.045), head), (0.08, 0, top + 0.02)))
    else:
        for k in range(3):
            a = k * 2 * math.pi / 3
            made.append(put(shape("cone", (0.02, 0.02, 0.09), metal, rotation=(0.5 * math.sin(a), 0.5 * math.cos(a), 0)),
                            (0.04 * math.cos(a), 0.04 * math.sin(a), top + 0.08)))
        made.append(put(shape("ball", (0.055, 0.055, 0.055), head), (0, 0, top + 0.09)))
    return made


def render_item(item, out, name):
    from mathutils import Vector

    w, h = (int(v) for v in item["size"].split(","))
    ground_scale, build = KINDS[item["kind"]]
    scene = studio.reset(samples=64)
    catcher = bpy.data.objects["shadow catcher"]
    handle = bpy.data.objects.new("item", None)
    scene.collection.objects.link(handle)
    objects = build_weapon(item) if build == "upright" else build_accessory(item)
    for o in objects:
        o.parent = handle
    studio.camera(scene, w * CELL, h * CELL)
    catcher.hide_render = True
    sun = bpy.data.objects["sun"].data
    sun.energy *= 1.4
    # Upright, facing the bag's camera.
    if build == "flat":
        handle.rotation_euler = (math.radians(90), 0, 0)
    elif build == "upright":
        handle.rotation_euler = (0, 0, 0)
    elif item["kind"] == "bracelet":  # tipped back to show the band's width
        handle.rotation_euler = (math.radians(-40), 0, math.radians(-25))
    else:
        handle.rotation_euler = (0, 0, math.radians(-25))
    items.inventory_camera(scene, objects, w * CELL, h * CELL)
    studio.render(scene, str(out / f"inv_{name}.png"))
    for o in list(scene.objects):
        if o.type == "CAMERA":
            bpy.data.objects.remove(o)
    catcher.hide_render = False
    sun.energy /= 1.4
    studio.camera(scene, 128, 128)
    angle = math.radians(item["code"] % 360)
    # Weapons lie on their flat.
    handle.rotation_euler = (math.radians(-90), 0, angle) if build == "upright" else (0, 0, angle)
    handle.scale = Vector((items.HERO_SCALE * ground_scale,) * 3)
    bpy.context.view_layer.update()
    points = items.bounds(objects)
    low = min(p.z for p in points)
    centre = sum(points, Vector()) / len(points)
    handle.location = (-centre.x, -centre.y, -low)
    studio.render(scene, str(out / f"ground_{name}.png"))
    ax, ay = studio.anchor(scene)
    (out / f"anchor_{name}.txt").write_text(f"{ax:.0f} {ay:.0f}\n")


def write_grid(frames, folder, name, width=1024):
    """Lays frames out in rows no wider than `width`; writes <name>.png
    and <name>.sheet."""
    from PIL import Image

    placed, x, y, row = [], 0, 0, 0
    for im, ax, ay in frames:
        if x + im.width > width:
            x, y, row = 0, y + row, 0
        placed.append((im, x, y, ax, ay))
        x += im.width
        row = max(row, im.height)
    out = Image.new("RGBA", (width, y + row))
    lines = ["# Rendered by tools/art/uniques.py (our own art, see content/README.md).\n", f"image={name}.png\n"]
    for im, px, py, ax, ay in placed:
        out.paste(im, (px, py))
        lines.append(f"frame={px},{py},{im.width},{im.height},{ax},{ay}\n")
    out = out.crop((0, 0, max(px + im.width for im, px, *_ in placed), out.height))
    folder.mkdir(parents=True, exist_ok=True)
    out.save(folder / f"{name}.png", optimize=True)
    with open(folder / f"{name}.sheet", "w", newline="\n") as f:
        f.writelines(lines)


def main():
    args = sys.argv[1:]
    if args[0] == "render":
        cls = "accessory"
        if "--class" in args:
            k = args.index("--class")
            cls = args[k + 1]
            del args[k:k + 2]
        only = None
        if "--only" in args:
            k = args.index("--only")
            only = {int(x) for x in args[k + 1].split(",")}
            del args[k:k + 2]
        out = Path(args[1])
        out.mkdir(parents=True, exist_ok=True)
        todo = [i for i in read_uniques(ROOT / "content") if i["class"] == cls]
        for n, item in enumerate(todo):
            if only is None or n in only:
                render_item(item, out, f"{cls}_{n:03d}")
                print(cls, n, item["name"], "done", flush=True)
        return
    from PIL import Image

    renders, pack = Path(args[1]), Path(args[2])
    for cls, (bank, folder) in PICTURES.items():
        todo = [i for i in read_uniques(pack) if i["class"] == cls]
        if not todo:
            continue
        inventory, ground = [], []
        for n in range(len(todo)):
            name = f"{cls}_{n:03d}"
            inventory.append((Image.open(renders / f"inv_{name}.png").convert("RGBA"), 0, 0))
            im = Image.open(renders / f"ground_{name}.png").convert("RGBA")
            box = im.getbbox() or (0, 0, 1, 1)
            ax, ay = map(int, (renders / f"anchor_{name}.txt").read_text().split())
            ground.append((im.crop(box), ax - box[0], ay - box[1]))
        write_grid(inventory, pack / "System/Game/Pattern", f"Item{bank:04d}")
        write_grid(ground, pack / f"Character/ITEM/{folder:08d}", "Pattern")
        print(f"wrote {len(todo)} {cls} pictures")


if __name__ == "__main__":
    main()
