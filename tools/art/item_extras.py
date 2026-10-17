"""Pictures for the original's items that nothing in our items.txt or
uniques.txt stands in for, so a player's copy drawn with our art (--our-art,
F12) shows ours for those too: crests, gems, medicines, fruit, quest items
and a few plain swords and armours. Rendered with studio.py like
tools/art/items.py.

    python tools/art/item_extras.py render <folder> [--only N,M] [--group G]
    python3 tools/art/item_extras.py pack <folder> <content pack folder>

The first step needs a Python with the `bpy` module and renders into
<folder>; the second needs Pillow and lays the renders out as
System/Game/Pattern/Item0004 (bag) and Character/ITEM/00000011/Pattern
(ground). Bag frame n is EXTRAS[n]; ground frames are numbered in the same
order over the entries that have a ground picture. tools/content/item_skin.py
reads EXTRAS to map the original's picture numbers (its `icons` and
`grounds`) to these frames.

Each entry is drawn from its name and kind only. Nothing is taken from the
original's pictures: their sizes in the bag (cells) are kept, and small
things fill only part of their cell, as there.
"""

import math
import random
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent

try:
    import bpy
    from mathutils import Vector

    sys.path.insert(0, str(HERE))
    import creatures  # noqa: E402
    import items  # noqa: E402
    import studio  # noqa: E402
    import uniques  # noqa: E402
except ImportError:  # packing, or item_skin.py
    bpy = None

CELL = 32
BANK, FOLDER = 4, 11

# Element colours (sRGB), for crests, gems and medicines.
FIRE, WATER, EARTH, THUNDER = (0.92, 0.22, 0.12), (0.18, 0.42, 0.95), (0.72, 0.45, 0.18), (0.98, 0.84, 0.22)
SACRED, DARK, CLEAR, METAL = (0.82, 0.78, 0.98), (0.50, 0.14, 0.62), (0.92, 0.94, 0.96), (0.58, 0.62, 0.68)
GEL = (0.35, 0.82, 0.38)


def E(name, kind, size=(1, 1), fill=0.8, icons=(), grounds=(), ground=2.0, bag=(0, 0, -25), **args):
    """An entry: kind names a builder below (or "unique:<kind>" for one of
    uniques.py's models); size is its bag cells; fill how much of them it
    takes; ground its scale on the ground (relative to the hero, as
    items.py); bag its turn in degrees about X, Y and Z in the bag."""
    return {"name": name, "kind": kind, "size": size, "fill": fill, "icons": list(icons), "grounds": list(grounds),
            "ground": ground, "bag": bag, "args": args}


CRESTS = [("Full", CLEAR, (0.98, 0.80, 0.32)), ("Water", WATER, None), ("Earth", EARTH, None),
          ("Thunder", THUNDER, None), ("Sacred", SACRED, None), ("Dark", DARK, None), ("Clear", CLEAR, None),
          ("Metal", METAL, None), ("Fire", FIRE, None)]
GEMS = [("Fire", FIRE), ("Water", WATER), ("Earth", EARTH), ("Thunder", THUNDER), ("Sacred", SACRED),
        ("Dark", DARK), ("Clear", (0.98, 0.86, 0.88)), ("Metal", (0.48, 0.50, 0.56))]
MEDICINES = [("Gel", GEL), ("Fire", FIRE), ("Water", WATER), ("Earth", EARTH), ("Thunder", THUNDER),
             ("Holy", SACRED), ("Dark", DARK), ("White", CLEAR), ("Metal", METAL)]

EXTRAS = (
    # Consumables.
    [E("High Tablet", "tablet", fill=0.42, bag=(60, 0, 0), icons=[(0, 55)], colour=(0.62, 0.70, 0.76)),
     E("Tablet +", "tablet", fill=0.42, bag=(60, 0, 0), icons=[(0, 56)], colour=(0.42, 0.22, 0.20), rim=True),
     E("High Tablet +", "tablet", fill=0.42, bag=(60, 0, 0), icons=[(0, 57)], colour=(0.36, 0.20, 0.46), rim=True),
     E("Capsule +", "capsule", fill=0.62, bag=(0, -38, 0), icons=[(0, 64)], top=(0.82, 0.12, 0.12), bottom=(0.16, 0.12, 0.14)),
     E("High Capsule", "capsule", fill=0.62, bag=(0, -38, 0), icons=[(0, 65)], top=(0.30, 0.30, 0.34), bottom=(0.08, 0.08, 0.10)),
     E("High Capsule +", "capsule", fill=0.62, bag=(0, -38, 0), icons=[(0, 66)], top=(0.20, 0.32, 0.92), bottom=(0.10, 0.12, 0.30))]
    + [E(f"{q}Meat", "meat", fill=0.86, bag=(45, 0, -20), icons=[(0, 261 + k)], grounds=[(0, 29)] if k == 0 else [], variant=k)
       for k, q in enumerate(("", "Quality ", "High Quality ", "Excellent Quality "))]
    + [E(f"{n} Medicine", "medicine", (1, 2), fill=0.78, bag=(0, 0, 0), icons=[(0, 414 + k)],
         grounds=[(0, 31)] if n == "White" else [], colour=c) for k, (n, c) in enumerate(MEDICINES)]
    + [E("Vitality Source", "source", fill=0.6, icons=[(9, 36)], grounds=[(7, 36)], colour=(0.92, 0.22, 0.62)),
       E("Mental Source", "source", fill=0.6, icons=[(9, 37)], grounds=[(7, 37)], colour=(0.22, 0.62, 0.98)),
       E("Apple", "apple", fill=0.82, icons=[(9, 38)], grounds=[(7, 38)], colour=(0.78, 0.08, 0.06)),
       E("Green Apple", "apple", fill=0.82, icons=[(9, 39)], grounds=[(7, 39)], colour=(0.62, 0.70, 0.18)),
       E("Walnut", "walnut", fill=0.62, icons=[(9, 40)], grounds=[(7, 40)]),
       E("Chinese Matrimony Vine", "sprig", fill=0.9, bag=(0, 0, 0), icons=[(9, 41)], grounds=[(7, 41)]),
       E("Chico Nut", "nuts", fill=0.82, icons=[(9, 42)], grounds=[(7, 42)]),
       E("Smart Orange", "orange", fill=0.7, icons=[(9, 43)], grounds=[(7, 43)]),
       E("Mandragora", "mandragora", (1, 2), fill=0.85, bag=(0, 0, 0), icons=[(9, 44)], grounds=[(7, 44)]),
       E("Akebi Fruit", "akebi", fill=0.85, bag=(55, -30, 0), icons=[(9, 45)], grounds=[(7, 45)])]
    # Crests and gems.
    + [E(f"{n} Crest", "crest", fill=0.82, bag=(0, 0, 0), icons=[(0, 234 + k)],
         grounds=[(0, 26)] if n == "Fire" else [], ground=1.8, colour=c, metal=m) for k, (n, c, m) in enumerate(CRESTS)]
    + [E(f"{n} Gem", "pear", fill=0.66, bag=(62, 0, 18), icons=[(0, 244 + k)], colour=c) for k, (n, c) in enumerate(GEMS)]
    + [E("Garnet", "stone", fill=0.58, icons=[(5, 0)], grounds=[(3, 3)], colour=(0.62, 0.04, 0.10), cut="round"),
       E("Diopside", "stone", fill=0.58, icons=[(5, 1)], colour=(0.10, 0.55, 0.22), cut="square"),
       E("Lapis-Lazuli", "stone", fill=0.6, icons=[(5, 2)], bag=(45, 0, 0), colour=(0.10, 0.20, 0.72), cut="cabochon"),
       E("Morganite", "stone", fill=0.6, icons=[(5, 3)], colour=(0.98, 0.66, 0.62), cut="oval"),
       E("Kyanite", "stone", fill=0.72, bag=(0, 0, 0), icons=[(5, 4)], colour=(0.30, 0.52, 0.92), cut="blade"),
       E("Chiastolite", "stone", fill=0.6, icons=[(5, 5)], bag=(65, 0, 0), colour=(0.58, 0.48, 0.38), cut="cross"),
       E("Hessonite", "stone", fill=0.58, icons=[(5, 6)], colour=(0.88, 0.46, 0.12), cut="cushion"),
       E("Amethyst", "stone", fill=0.72, icons=[(5, 7)], colour=(0.58, 0.28, 0.85), cut="points"),
       E("Rose Quartz", "stone", fill=0.62, icons=[(5, 8)], colour=(0.96, 0.72, 0.80), cut="tumbled"),
       E("Seal Crystal", "seal", fill=0.82, icons=[(6, 1)], grounds=[(7, 49)])]
    # Necklaces, bracelets and rings, on uniques.py's models.
    + [E("Resistance Necklace", "unique:amulet", icons=[(7, 0)], grounds=[(5, 0)], colour=(0.62, 0.72, 0.92), metal=1, code=11),
       E("Magic Necklace", "unique:pendant", icons=[(7, 1)], grounds=[(5, 1)], colour=(0.58, 0.22, 0.85), metal=2, code=5),
       E("Speed Necklace", "unique:pendant", icons=[(7, 2)], colour=(0.20, 0.80, 0.85), metal=1, code=2),
       E("Stamina Necklace", "unique:pendant", icons=[(7, 3)], grounds=[(5, 3)], colour=(0.85, 0.12, 0.18), metal=0, code=9),
       E("Power Bracelet", "unique:bracelet", icons=[(7, 4)], grounds=[(5, 4)], colour=(0.85, 0.12, 0.10), metal=0, code=4),
       E("Animal Bracelet", "unique:bracelet", icons=[(7, 5)], grounds=[(5, 5)], colour=(0.92, 0.58, 0.20), metal=2, code=7),
       E("Destruction Ring", "unique:darkring", icons=[(7, 6)], grounds=[(5, 6)], colour=(0.92, 0.18, 0.10), metal=3, code=3),
       E("Defense Ring", "unique:ring", icons=[(7, 7)], grounds=[(5, 7)], colour=(0.62, 0.70, 0.85), metal=1, code=14),
       E("Blade Ring", "unique:ring", icons=[(7, 8)], grounds=[(5, 8)], colour=(0.20, 0.72, 0.30), metal=1, code=17)]
    # Quest items.
    + [E("Sacred Wing", "feathers", fill=0.92, bag=(0, 0, 0), icons=[(9, 47)], grounds=[(7, 47)]),
       E("Angel's Hair", "hair", fill=0.92, bag=(0, 0, 0), icons=[(9, 48)], grounds=[(7, 48)]),
       E("Angel's Nail", "shards", fill=0.86, icons=[(9, 49)]),
       E("Fruit of Poreno", "poreno", fill=0.82, bag=(0, 0, 30), icons=[(9, 50)]),
       E("Antidote", "antidote", fill=0.92, bag=(0, -35, 0), icons=[(9, 51)], grounds=[(7, 51)]),
       E("White Cloth", "cloth", fill=0.9, bag=(55, 0, -15), icons=[(9, 52)], grounds=[(7, 52)]),
       E("Red Thread", "spool", fill=0.82, bag=(10, 0, -30), icons=[(9, 53)], grounds=[(7, 53)]),
       E("Silver Needle", "needle", fill=0.95, bag=(0, -45, 0), icons=[(9, 54)], grounds=[(7, 54)], ground=2.4),
       E("Tears of Oberon", "drop", fill=0.78, bag=(0, 0, 0), icons=[(9, 55)], grounds=[(7, 55)]),
       E("Ancient Short Sword", "unique:shortsword", icons=[(9, 56)], grounds=[(7, 56)], bag=(0, -45, 0),
         colour=(0.40, 0.75, 0.55), metal=0, code=21, level=10)]
    + [E(f"{n} Orb", "orb", fill=0.9, bag=(0, 0, 0), icons=[(9, 57 + k)], grounds=[(7, 57 + k)], colour=c)
       for k, (n, c) in enumerate((("Red", (0.85, 0.10, 0.10)), ("Blue", (0.12, 0.22, 0.92)),
                                   ("Green", (0.12, 0.72, 0.22)), ("Silver", (0.80, 0.84, 0.90)),
                                   ("Gold", (0.98, 0.76, 0.22))))]
    + [E(n, "card", fill=0.92, bag=(0, 0, 0), icons=[(9, 62 + k)], grounds=[(7, 62 + k)] if k < 3 else [], face=n)
       for k, n in enumerate(("Jack", "Queen", "King", "Joker"))]
    + [E("Evidence of Injustice", "letter", fill=0.95, bag=(0, 0, 0), icons=[(9, 67)]),
       E("Demon Fang", "fang", fill=0.9, bag=(0, 0, 0), icons=[(10, 94)], grounds=[(8, 94)]),
       E("Spirit Stone", "tag", fill=0.88, bag=(0, 0, 0), icons=[(10, 95)], grounds=[(8, 95)],
         colour=(0.82, 0.90, 0.94), rune=(0.55, 0.85, 1.0)),
       E("Companion Stone", "tag", fill=0.88, bag=(0, 0, 0), icons=[(10, 96)], grounds=[(8, 96)],
         colour=(0.62, 0.48, 0.34), rune=(0.98, 0.66, 0.22)),
       E("Angel's Memory", "brooch", fill=0.82, bag=(0, 0, 0), icons=[(10, 97)], grounds=[(8, 97)]),
       E("Jade Pierced Earrings", "earrings", fill=0.86, bag=(0, 0, 0), icons=[(10, 98)], grounds=[(8, 98)]),
       E("Porcelain Cup", "cup", fill=0.84, icons=[(10, 99)], grounds=[(8, 99)]),
       E("Spiritual Heraldry", "heraldry", fill=0.9, bag=(0, 0, 0), icons=[(10, 100)], grounds=[(8, 100)]),
       E("Music Box", "musicbox", fill=0.92, bag=(20, 0, -25), icons=[(10, 101)], grounds=[(8, 101)]),
       E("Coral Ring", "coral_ring", fill=0.8, icons=[(10, 102)], grounds=[(8, 102)])]
    # Plain weapons and armour, on uniques.py's models.
    + [E("Bastard Sword", "unique:bastard", (2, 4), icons=[(0, 81)], colour=(0.8, 0.8, 0.85), metal=1, code=31),
       E("Iron Armor", "unique:plate", (2, 3), icons=[(0, 108)], colour=(0.55, 0.58, 0.62), metal=3, code=4, bag=(0, 0, 0), fill=1.0, dark=0.6),
       E("Long Sword", "unique:longsword", (1, 4), icons=[(0, 181)], colour=(0.8, 0.8, 0.85), metal=1, code=40),
       E("Board Shield", "unique:boardshield", (2, 3), icons=[(0, 198)], colour=(0.62, 0.42, 0.22), metal=0, code=3),
       E("Broad Sword", "unique:bastard", (1, 4), icons=[(0, 324)], colour=(0.8, 0.8, 0.85), metal=0, code=12),
       E("Knight Broad Sword", "unique:bastard", (1, 4), icons=[(0, 329)], colour=(0.2, 0.4, 0.95), metal=2, code=58),
       E("Falchion", "unique:curved", (1, 4), icons=[(0, 333)], colour=(0.8, 0.8, 0.85), metal=0, code=8),
       E("Claymore", "unique:twohand", (1, 4), icons=[(0, 396)], colour=(0.8, 0.8, 0.85), metal=1, code=2)]
)

# Batches, for rendering and contact sheets.
GROUPS = {
    "consumables": [n for n, e in enumerate(EXTRAS) if e["kind"] in (
        "tablet", "capsule", "meat", "medicine", "source", "apple", "walnut", "sprig", "nuts", "orange",
        "mandragora", "akebi")],
    "jewels": [n for n, e in enumerate(EXTRAS) if e["kind"] in ("crest", "pear", "stone", "seal")
               or e["kind"] in ("unique:amulet", "unique:pendant", "unique:bracelet", "unique:ring", "unique:darkring")],
}
GROUPS["gear"] = [n for n, e in enumerate(EXTRAS) if e["size"][1] > 2 and e["kind"].startswith("unique:")]
GROUPS["quest"] = [n for n in range(len(EXTRAS)) if not any(n in g for g in GROUPS.values())]


def ground_frames():
    """Entry index -> its ground frame, for the entries that have one."""
    out = {}
    for n, e in enumerate(EXTRAS):
        if e["grounds"]:
            out[n] = len(out)
    return out


# --- Building (needs bpy) ---------------------------------------------------

def mat(name, colour, dark=None, **kw):
    return studio.material(name, colour, dark, **kw)


def glow(name, colour, strength, rough=0.1):
    m = studio.material(name, colour, tuple(c * 0.4 for c in colour), scale=6, rough=rough, bump=0)
    bsdf = m.node_tree.nodes["Principled BSDF"]
    bsdf.inputs["Emission Color"].default_value = (*studio.linear(colour), 1)
    bsdf.inputs["Emission Strength"].default_value = strength
    return m


def gold_mat(colour=(0.95, 0.74, 0.30)):
    return mat(f"gold {colour}", colour, tuple(c * 0.45 for c in colour), scale=30, rough=0.3, bump=0.15)


def silver_mat(colour=(0.80, 0.82, 0.86)):
    return mat(f"silver {colour}", colour, tuple(c * 0.4 for c in colour), scale=30, rough=0.3, bump=0.15)


def shape(kind, size, m, at=(0, 0, 0), rotation=(0, 0, 0)):
    obj = creatures.shape(kind, size, m, rotation)
    obj.location = at
    return obj


def flat(obj):
    """Flat faces, for cut stones."""
    for p in obj.data.polygons:
        p.use_smooth = False
    return obj


def mesh(name, verts, faces, m, smooth=True):
    data = bpy.data.meshes.new(name)
    data.from_pydata(verts, [], faces)
    data.validate()
    obj = bpy.data.objects.new(name, data)
    bpy.context.scene.collection.objects.link(obj)
    for p in data.polygons:
        p.use_smooth = smooth
    return studio.assign(obj, m)


def ico(radius, m, subdivisions=1, at=(0, 0, 0), scale=(1, 1, 1)):
    bpy.ops.mesh.primitive_ico_sphere_add(subdivisions=subdivisions, radius=radius, location=at)
    obj = bpy.context.object
    obj.scale = scale
    return studio.assign(obj, m)


def tube(points, radius, m, sides=8):
    """A tube through points (a cord, a thread, a stem)."""
    verts, faces = [], []
    for k, p in enumerate(points):
        a = Vector(points[min(k + 1, len(points) - 1)]) - Vector(points[max(k - 1, 0)])
        a.normalize()
        u = a.orthogonal().normalized()
        v = a.cross(u)
        for s in range(sides):
            t = 2 * math.pi * s / sides
            verts.append(tuple(Vector(p) + radius * (math.cos(t) * u + math.sin(t) * v)))
    for k in range(len(points) - 1):
        for s in range(sides):
            i, j = k * sides + s, k * sides + (s + 1) % sides
            faces.append((i, j, j + sides, i + sides))
    return mesh("tube", verts, faces, m)


def arc(radius, start, end, n, centre=(0, 0, 0), plane="xz"):
    out = []
    for k in range(n + 1):
        t = start + (end - start) * k / n
        a, b = radius * math.cos(t), radius * math.sin(t)
        out.append((centre[0] + a, centre[1], centre[2] + b) if plane == "xz" else (centre[0] + a, centre[1] + b, centre[2]))
    return out


def lathe(profile, m, sides=24, smooth=True):
    """A round body turned from (radius, z) points about Z."""
    verts, faces = [], []
    for r, z in profile:
        for s in range(sides):
            t = 2 * math.pi * s / sides
            verts.append((r * math.cos(t), r * math.sin(t), z))
    for k in range(len(profile) - 1):
        for s in range(sides):
            i, j = k * sides + s, k * sides + (s + 1) % sides
            faces.append((i, j, j + sides, i + sides))
    return mesh("lathe", verts, faces, m, smooth)


def painted(name, picture):
    """A material from a picture painted with Pillow (cards, a letter)."""
    path = Path(bpy.app.tempdir or "/tmp") / f"{name}.png"
    picture.save(path)
    m = bpy.data.materials.new(name)
    m.use_nodes = True
    nodes, links = m.node_tree.nodes, m.node_tree.links
    tex = nodes.new("ShaderNodeTexImage")
    tex.image = bpy.data.images.load(str(path))
    tex.interpolation = "Closest"
    bsdf = nodes["Principled BSDF"]
    bsdf.inputs["Roughness"].default_value = 0.6
    links.new(tex.outputs["Color"], bsdf.inputs["Base Color"])
    return m


def card_face(face):
    """Our own card faces: a framed field, a large letter in the middle
    (a jester's cap for the Joker) and a pip in two corners."""
    from PIL import Image, ImageDraw, ImageFont

    w, h = 80, 120
    im = Image.new("RGB", (w, h), (240, 234, 216))
    d = ImageDraw.Draw(im)
    d.rectangle((3, 3, w - 4, h - 4), outline=(60, 50, 40), width=3)
    red, blue, gold = (196, 28, 32), (36, 56, 160), (214, 162, 40)
    ink = {"Jack": blue, "Queen": red, "King": red, "Joker": (120, 40, 150)}[face]
    for x, y in ((14, 16), (w - 15, h - 17)):
        d.polygon([(x, y - 7), (x + 6, y), (x, y + 7), (x - 6, y)], fill=ink)
    cx, cy = w // 2, h // 2
    if face == "Joker":
        d.polygon([(cx - 26, cy + 24), (cx - 20, cy - 26), (cx, cy - 4), (cx + 20, cy - 26), (cx + 26, cy + 24)],
                  fill=ink)
        for x in (cx - 20, cx + 20):
            d.ellipse((x - 6, cy - 32, x + 6, cy - 20), fill=gold)
        d.rectangle((cx - 28, cy + 18, cx + 28, cy + 28), fill=gold)
    else:
        font = ImageFont.load_default(size=64)
        letter = {"Jack": "J", "Queen": "Q", "King": "K"}[face]
        d.text((cx, cy + 2), letter, fill=ink, font=font, anchor="mm", stroke_width=2, stroke_fill=ink)
        crown = [(cx - 14, cy - 30), (cx - 14, cy - 42), (cx - 6, cy - 35), (cx, cy - 46), (cx + 6, cy - 35),
                 (cx + 14, cy - 42), (cx + 14, cy - 30)]
        if face != "Jack":
            d.polygon(crown, fill=gold)
    return im.transpose(Image.FLIP_TOP_BOTTOM)


def letter_page():
    from PIL import Image, ImageDraw

    im = Image.new("RGB", (64, 80), (226, 216, 186))
    d = ImageDraw.Draw(im)
    rng = random.Random(5)
    for y in range(10, 70, 6):
        x = 8
        while x < 56:
            n = rng.randint(4, 12)
            d.line((x, y, min(x + n, 56), y), fill=(70, 55, 45), width=1)
            x += n + 3
    return im.transpose(Image.FLIP_TOP_BOTTOM)


def build(e):
    """Builds an entry; returns its objects, standing on z = 0, front -Y."""
    kind, a = e["kind"], e["args"]
    rng = random.Random(e["name"])
    made = []
    add = made.append

    if kind == "tablet":
        body = mat(f"tablet {e['name']}", a["colour"], tuple(c * 0.6 for c in a["colour"]), scale=40, rough=0.5, bump=0.3)
        add(shape("ball", (0.11, 0.11, 0.04), body, (0, 0, 0.04)))
        add(shape("box", (0.09, 0.006, 0.006), mat("score", tuple(c * 0.45 for c in a["colour"])), (0, 0, 0.078)))
        if a.get("rim"):
            bpy.ops.mesh.primitive_torus_add(major_radius=0.105, minor_radius=0.008, location=(0, 0, 0.04))
            add(studio.assign(studio.smooth(bpy.context.object), gold_mat()))
    elif kind == "capsule":
        for side, colour in ((1, a["top"]), (-1, a["bottom"])):
            m = mat(f"capsule {colour}", colour, tuple(c * 0.5 for c in colour), scale=20, rough=0.2, bump=0.05)
            add(shape("cylinder", (0.045, 0.045, 0.055), m, (side * 0.055, 0, 0.045), (0, math.radians(90), 0)))
            add(shape("ball", (0.045, 0.045, 0.045), m, (side * 0.11, 0, 0.045)))
        add(shape("cylinder", (0.047, 0.047, 0.006), mat("band", (0.85, 0.85, 0.8)), (0, 0, 0.045), (0, math.radians(90), 0)))
    elif kind == "meat":
        v = a["variant"]
        red = [(0.62, 0.10, 0.08), (0.70, 0.14, 0.12), (0.58, 0.06, 0.08), (0.74, 0.20, 0.16)][v]
        flesh = mat(f"meat {v}", red, (0.30, 0.03, 0.03), scale=14, rough=0.45, bump=0.6, spot=0.08 + 0.06 * v,
                    spot_colour=(0.95, 0.85, 0.78))
        fat = mat("fat", (0.95, 0.88, 0.76), (0.75, 0.62, 0.50), scale=20, rough=0.5)
        bone = mat("bone", (0.93, 0.90, 0.80), (0.62, 0.56, 0.44), scale=25, rough=0.6)
        if v in (0, 2):
            chunk = shape("ball", (0.15, 0.11, 0.1 + 0.015 * v), flesh, (-0.03, 0, 0.1))
            chunk.modifiers["soft"].levels = 2
            studio.displace(chunk, 0.03, 0.12, seed=3 + v)
            add(chunk)
            add(shape("ball", (0.13, 0.1, 0.035), fat, (-0.04, 0.02, 0.17 + 0.015 * v), (0.2, 0, 0)))
            add(shape("cylinder", (0.022, 0.022, 0.07), bone, (0.15, 0, 0.1), (0, math.radians(80), 0)))
            for dz in (-0.02, 0.02):
                add(shape("ball", (0.03, 0.03, 0.03), bone, (0.22, 0, 0.11 + dz)))
        else:
            # A cut steak: a thick slab with a rind of fat, marbled.
            slab = shape("cylinder", (0.17, 0.12, 0.05), flesh, (0, 0, 0.06))
            slab.modifiers["soft"].levels = 1
            studio.displace(slab, 0.015, 0.1, seed=7 + v)
            add(slab)
            rind = shape("cylinder", (0.175, 0.125, 0.045), fat, (0.02, 0.01, 0.055))
            add(rind)
            if v == 3:
                add(shape("box", (0.02, 0.1, 0.012), bone, (0.0, 0, 0.115)))
    elif kind == "medicine":
        liquid = glow(f"medicine {e['name']}", a["colour"], 0.6)
        glass = glow("vial glass", (0.80, 0.86, 0.88), 0.05, rough=0.05)
        pewter = silver_mat((0.62, 0.62, 0.64))
        add(shape("cylinder", (0.07, 0.07, 0.15), liquid, (0, 0, 0.19)))
        add(shape("cylinder", (0.074, 0.074, 0.03), glass, (0, 0, 0.36)))
        add(shape("cylinder", (0.085, 0.085, 0.025), pewter, (0, 0, 0.025)))
        add(shape("cylinder", (0.06, 0.06, 0.02), pewter, (0, 0, 0.4)))
        add(shape("cylinder", (0.04, 0.04, 0.03), mat("cork", (0.62, 0.44, 0.26), scale=40), (0, 0, 0.44)))
        for z in (0.1, 0.27):
            bpy.ops.mesh.primitive_torus_add(major_radius=0.076, minor_radius=0.008, location=(0, 0, z))
            add(studio.assign(studio.smooth(bpy.context.object), pewter))
        for k in range(3):
            t = math.radians(-90 + 120 * k + 30)
            add(shape("box", (0.008, 0.008, 0.19), pewter, (0.078 * math.cos(t), 0.078 * math.sin(t), 0.2)))
    elif kind == "source":
        add(flat(ico(0.11, glow(f"source {e['name']}", a["colour"], 0.35, rough=0.15), 1, (0, 0, 0.14))))
        add(shape("cylinder", (0.05, 0.05, 0.02), gold_mat(), (0, 0, 0.02)))
        add(shape("cone", (0.06, 0.06, 0.03), gold_mat(), (0, 0, 0.05), (math.pi, 0, 0)))
    elif kind == "apple":
        skin = mat(f"apple {a['colour']}", a["colour"], tuple(c * 0.55 for c in a["colour"]), scale=10, rough=0.3,
                   bump=0.1, spot=0.15, spot_colour=tuple(min(1, c * 1.3 + 0.1) for c in a["colour"]))
        add(lathe([(0.001, 0.02), (0.08, 0.0), (0.15, 0.06), (0.16, 0.14), (0.12, 0.23), (0.04, 0.245), (0.001, 0.22)],
                  skin))
        add(shape("cylinder", (0.008, 0.008, 0.04), mat("stem", (0.35, 0.22, 0.10)), (0.005, 0, 0.26), (0, 0.3, 0)))
        add(shape("ball", (0.06, 0.02, 0.025), mat("leaf", (0.28, 0.55, 0.15), scale=20), (0.06, 0, 0.27), (0, -0.4, 0.3)))
    elif kind == "walnut":
        shell = mat("walnut", (0.62, 0.44, 0.26), (0.30, 0.18, 0.08), scale=18, rough=0.8, bump=1.0)
        nut = shape("ball", (0.14, 0.12, 0.12), shell, (0, 0, 0.12))
        nut.modifiers["soft"].levels = 2
        studio.displace(nut, 0.025, 0.05, seed=11)
        add(nut)
        add(shape("box", (0.006, 0.125, 0.125), mat("seam", (0.40, 0.26, 0.12)), (0, 0, 0.12)))
    elif kind == "sprig":
        stem = mat("vine", (0.42, 0.30, 0.16), scale=30)
        berry = glow("goji", (0.92, 0.26, 0.08), 0.15, rough=0.3)
        leaf = mat("vine leaf", (0.28, 0.52, 0.18), (0.12, 0.26, 0.08), scale=20)
        add(tube([(-0.2, 0, 0.0), (-0.05, 0, 0.12), (0.08, 0, 0.2), (0.18, 0, 0.3)], 0.008, stem))
        for k, (x, z, r) in enumerate(((-0.12, 0.02, 0), (-0.02, 0.1, 1), (0.05, 0.15, 2), (0.12, 0.22, 3),
                                       (0.0, 0.03, 4), (0.12, 0.12, 5), (0.2, 0.2, 6))):
            t = rng.uniform(-0.6, 0.6)
            add(shape("ball", (0.028, 0.028, 0.045), berry, (x, -0.02, z), (0, t, 0)))
        for x, z, t in ((-0.1, 0.12, 0.8), (0.04, 0.26, -0.5), (0.18, 0.36, 0.3)):
            add(shape("ball", (0.07, 0.01, 0.025), leaf, (x, 0.01, z), (0, t, 0)))
    elif kind == "nuts":
        shell = mat("chico", (0.52, 0.30, 0.18), (0.24, 0.12, 0.06), scale=25, rough=0.7, bump=0.6)
        for x, y, s in ((-0.08, 0.03, 1.0), (0.08, 0.04, 0.9), (0.0, -0.07, 1.05), (0.1, -0.1, 0.8)):
            add(shape("ball", (0.075 * s, 0.07 * s, 0.065 * s), shell, (x, y, 0.065 * s)))
            add(shape("cone", (0.012, 0.012, 0.02), mat("calyx", (0.30, 0.22, 0.12)), (x, y, 0.13 * s)))
    elif kind == "orange":
        peel = mat("orange", (0.98, 0.58, 0.08), (0.70, 0.30, 0.02), scale=60, rough=0.5, bump=0.8)
        add(shape("ball", (0.14, 0.14, 0.13), peel, (0, 0, 0.13)))
        add(shape("ball", (0.07, 0.02, 0.025), mat("leaf", (0.24, 0.50, 0.14), scale=20), (0.04, 0, 0.265), (0, -0.3, 0.5)))
        add(shape("cylinder", (0.012, 0.012, 0.01), mat("stem", (0.35, 0.30, 0.12)), (0, 0, 0.26)))
    elif kind == "mandragora":
        root = mat("mandragora", (0.78, 0.62, 0.38), (0.42, 0.28, 0.14), scale=20, rough=0.8, bump=0.8, stretch=(1, 1, 0.2))
        leaf = mat("mandragora leaf", (0.22, 0.48, 0.16), (0.08, 0.22, 0.06), scale=20)
        add(shape("ball", (0.08, 0.07, 0.15), root, (0, 0, 0.42)))
        add(shape("ball", (0.06, 0.055, 0.06), root, (0, 0, 0.6)))
        for side in (-1, 1):
            add(tube([(side * 0.04, 0, 0.32), (side * 0.07, 0, 0.18), (side * 0.06, 0, 0.04), (side * 0.1, 0, 0.0)],
                     0.022, root))
            add(tube([(side * 0.06, 0, 0.5), (side * 0.13, 0, 0.42), (side * 0.17, 0, 0.32)], 0.016, root))
        for k in range(5):
            t = math.radians(-50 + 25 * k)
            add(shape("ball", (0.035, 0.012, 0.13), leaf, (0.13 * math.sin(t), 0, 0.72 + 0.1 * math.cos(t)), (0, t, 0)))
        for x in (-0.025, 0.025):  # eyes
            add(shape("ball", (0.012, 0.01, 0.012), mat("hollow", (0.15, 0.08, 0.04)), (x, -0.055, 0.62)))
    elif kind == "akebi":
        rind = mat("akebi", (0.55, 0.32, 0.62), (0.26, 0.10, 0.32), scale=25, rough=0.5, bump=0.4)
        pulp = mat("akebi pulp", (0.96, 0.94, 0.88), (0.80, 0.78, 0.72), scale=15, rough=0.4, spot=0.12,
                   spot_colour=(0.08, 0.06, 0.06))
        # A long pod split along its top, the white pulp showing.
        for side in (-1, 1):
            half = shape("ball", (0.18, 0.06, 0.08), rind, (0, side * 0.035, 0.08), (side * 0.35, 0, 0))
            add(half)
        add(shape("ball", (0.15, 0.04, 0.06), pulp, (0, 0, 0.1)))
    elif kind == "crest":
        m = mat(f"crest {a['colour']}", tuple(0.55 * c + 0.4 for c in a["colour"]) if not a["metal"] else a["metal"],
                tuple(0.25 * c for c in a["colour"]), scale=30, rough=0.3, bump=0.15)
        stone = glow(f"crest stone {a['colour']}", a["colour"], 1.0)
        # A three-leafed lily: a tall middle leaf, two leaves curling out
        # and down, bound by a band, with a short tail below it.
        mid = shape("ball", (0.05, 0.022, 0.12), m, (0, 0, 0.22))
        add(mid)
        add(shape("cone", (0.035, 0.018, 0.06), m, (0, 0, 0.36)))
        for side in (-1, 1):
            pts = [(side * (0.03 + 0.1 * math.sin(t)), 0, 0.15 + 0.12 * math.sin(1.3 * t))
                   for t in [k / 7 * 2.4 for k in range(8)]]
            for k, p in enumerate(pts):
                r = 0.03 * (1 - k / 10)
                add(shape("ball", (r, 0.02, r), m, p))
        add(shape("box", (0.1, 0.03, 0.022), m, (0, 0, 0.13)))
        for x in (-0.045, 0, 0.045):
            add(shape("cone", (0.02, 0.016, 0.04), m, (x, 0, 0.07), (math.pi, 0, 0)))
        add(shape("ball", (0.028, 0.02, 0.028), stone, (0, -0.03, 0.13)))
    elif kind == "pear":
        m = glow(f"gem {a['colour']}", a["colour"], 0.5, rough=0.05)
        stone = flat(ico(0.1, m, 2, (0, 0, 0.06), (1.0, 0.8, 0.55)))
        for v in stone.data.vertices:  # draw one end out to a point
            if v.co.x > 0:
                v.co.y *= max(0.1, 1 - v.co.x / 0.11)
                v.co.z *= max(0.25, 1 - 0.6 * v.co.x / 0.1)
                v.co.x *= 1.6
        add(stone)
    elif kind == "stone":
        c, cut = a["colour"], a["cut"]
        clear = glow(f"stone {e['name']}", c, 0.45, rough=0.05)
        if cut == "round":
            add(flat(ico(0.11, clear, 1, (0, 0, 0.1), (1, 1, 0.9))))
        elif cut == "square":
            box = shape("box", (0.09, 0.09, 0.06), clear, (0, 0, 0.06))
            bevel = box.modifiers.new("bevel", "BEVEL")
            bevel.width, bevel.segments = 0.025, 1
            add(flat(box))
            add(shape("box", (0.06, 0.06, 0.01), clear, (0, 0, 0.12)))
        elif cut == "cabochon":
            lapis = mat("lapis", c, (0.04, 0.06, 0.30), scale=12, rough=0.25, bump=0.1, spot=0.06,
                        spot_colour=(0.95, 0.80, 0.30))
            add(shape("ball", (0.12, 0.1, 0.08), lapis, (0, 0, 0.06)))
        elif cut in ("oval", "cushion"):
            add(flat(ico(0.11, clear, 2, (0, 0, 0.07), (1.15 if cut == "oval" else 1.0, 0.85, 0.6))))
        elif cut == "blade":
            for k, (x, t, l) in enumerate(((0, 0.05, 0.13), (0.05, -0.3, 0.1), (-0.05, 0.35, 0.09))):
                b = shape("box", (0.028, 0.01, l), clear, (x + math.sin(t) * l, 0.012 * k, 0.03 + math.cos(t) * l),
                          (0, t, 0))
                b.modifiers["soft"].levels = 0
                add(flat(b))
        elif cut == "cross":
            body = mat("chiastolite", c, tuple(x * 0.5 for x in c), scale=10, rough=0.6, bump=0.3)
            add(shape("cylinder", (0.11, 0.11, 0.05), body, (0, 0, 0.05)))
            ink = mat("cross", (0.12, 0.10, 0.10))
            for t in (math.pi / 4, -math.pi / 4):
                add(shape("box", (0.1, 0.012, 0.004), ink, (0, 0, 0.1), (0, 0, t)))
            add(shape("box", (0.025, 0.025, 0.004), ink, (0, 0, 0.1), (0, 0, math.pi / 4)))
        elif cut == "points":
            for x, y, t, l in ((0, 0, 0, 0.16), (-0.06, 0.02, 0.4, 0.12), (0.06, 0.02, -0.35, 0.13), (0.02, -0.05, 0.1, 0.09)):
                bpy.ops.mesh.primitive_cylinder_add(vertices=6, radius=0.035, depth=l, location=(x, y, l / 2))
                prism = bpy.context.object
                prism.rotation_euler = (0, t, 0)
                add(flat(studio.assign(prism, clear)))
                bpy.ops.mesh.primitive_cone_add(vertices=6, radius1=0.035, depth=0.05)
                tip = bpy.context.object
                tip.rotation_euler = (0, t, 0)
                tip.location = (x + math.sin(t) * (l + 0.025), y, math.cos(t) * (l + 0.025))
                add(flat(studio.assign(tip, clear)))
            add(shape("ball", (0.1, 0.08, 0.03), mat("geode", (0.45, 0.42, 0.40), scale=10, bump=0.6), (0, 0, 0.0)))
        else:  # tumbled
            soft = glow("rose quartz", c, 0.3, rough=0.35)
            pebble = shape("ball", (0.12, 0.09, 0.08), soft, (0, 0, 0.08))
            pebble.modifiers["soft"].levels = 2
            studio.displace(pebble, 0.02, 0.15, seed=13)
            add(pebble)
    elif kind == "seal":
        add(shape("ball", (0.12, 0.12, 0.12), glow("seal crystal", (0.22, 0.85, 0.48), 1.2), (0, 0, 0.16)))
        g = gold_mat()
        bpy.ops.mesh.primitive_torus_add(major_radius=0.125, minor_radius=0.012, location=(0, 0, 0.16))
        band = studio.smooth(bpy.context.object)
        band.rotation_euler = (math.radians(70), 0, math.radians(20))
        add(studio.assign(band, g))
        add(shape("cylinder", (0.07, 0.07, 0.02), g, (0, 0, 0.02)))
    elif kind == "feathers":
        white = glow("feather", (0.94, 0.95, 1.0), 0.25, rough=0.7)
        quill = mat("quill", (0.85, 0.82, 0.70))
        for k, t in enumerate((-0.5, 0.0, 0.45)):
            l = 0.24 - 0.03 * abs(k - 1)
            at = (math.sin(t) * l, -0.01 * k, math.cos(t) * l + 0.02)
            add(shape("ball", (0.045, 0.01, l), white, at, (0, t, 0)))
            add(shape("cylinder", (0.005, 0.005, l), quill, (at[0] * 0.95, at[1] - 0.008, at[2]), (0, t, 0)))
    elif kind == "hair":
        gold_hair = glow("angel hair", (0.98, 0.84, 0.42), 0.35, rough=0.4)
        for k in range(5):
            off = (k - 2) * 0.012
            pts = [(-0.22 + 0.44 * s / 20 + off, 0, 0.25 * s / 20 + 0.03 * math.sin(s / 20 * 9 + k)) for s in range(21)]
            add(tube(pts, 0.008, gold_hair, 6))
        bpy.ops.mesh.primitive_torus_add(major_radius=0.03, minor_radius=0.012, location=(0, 0, 0.125))
        tie = studio.smooth(bpy.context.object)
        tie.rotation_euler = (0, math.radians(30), 0)
        add(studio.assign(tie, mat("ribbon", (0.85, 0.20, 0.30))))
    elif kind == "shards":
        violet = glow("angel nail", (0.62, 0.42, 0.92), 0.7, rough=0.1)
        for x, y, t, l in ((-0.08, 0.02, 0.5, 0.11), (0.0, -0.03, -0.2, 0.14), (0.09, 0.03, 0.8, 0.1)):
            bpy.ops.mesh.primitive_cone_add(vertices=5, radius1=0.03, depth=l * 2, location=(x, y, 0.04))
            o = bpy.context.object
            o.rotation_euler = (0.4, t + math.pi / 2, 0)
            add(flat(studio.assign(o, violet)))
    elif kind == "poreno":
        shell = mat("poreno", (0.60, 0.40, 0.30), (0.30, 0.16, 0.10), scale=20, rough=0.6, bump=0.8, stretch=(1, 0.3, 1))
        seed = shape("ball", (0.17, 0.1, 0.09), shell, (0, 0, 0.09))
        seed.modifiers["soft"].levels = 2
        add(seed)
        add(shape("cone", (0.03, 0.03, 0.04), shell, (0.18, 0, 0.09), (0, math.radians(90), 0)))
    elif kind == "antidote":
        bottle = glow("antidote", (0.30, 0.72, 0.42), 0.4)
        add(lathe([(0.001, 0.0), (0.07, 0.0), (0.09, 0.05), (0.09, 0.16), (0.04, 0.22), (0.025, 0.3), (0.03, 0.31),
                   (0.001, 0.31)], bottle))
        add(shape("cylinder", (0.022, 0.022, 0.03), mat("cork", (0.62, 0.44, 0.26), scale=40), (0, 0, 0.34)))
        add(shape("cylinder", (0.092, 0.092, 0.03), mat("label", (0.90, 0.86, 0.72)), (0, 0, 0.1)))
    elif kind == "cloth":
        linen = mat("white cloth", (0.94, 0.92, 0.86), (0.70, 0.66, 0.58), scale=50, rough=0.9, bump=0.3)
        for k in range(3):
            fold = shape("box", (0.2 - 0.012 * k, 0.16 - 0.01 * k, 0.012), linen,
                         (0.01 * k, -0.005 * k, 0.012 + 0.026 * k), (0, 0, 0.04 * k))
            add(fold)
    elif kind == "spool":
        wood = mat("spool wood", (0.62, 0.42, 0.24), (0.30, 0.18, 0.08), scale=10, stretch=(1, 1, 0.1))
        thread = mat("red thread", (0.78, 0.08, 0.10), (0.40, 0.02, 0.04), scale=4, stretch=(40, 40, 1), rough=0.8)
        add(shape("cylinder", (0.075, 0.075, 0.09), thread, (0, 0, 0.12)))
        for z in (0.02, 0.22):
            add(shape("cylinder", (0.1, 0.1, 0.02), wood, (0, 0, z)))
        add(tube([(0.07, -0.02, 0.1), (0.13, -0.06, 0.06), (0.16, -0.12, 0.01), (0.22, -0.1, 0.0)], 0.007, thread, 6))
    elif kind == "needle":
        steel = silver_mat((0.86, 0.88, 0.92))
        add(shape("cylinder", (0.008, 0.008, 0.24), steel, (0, 0, 0.26)))
        add(shape("cone", (0.008, 0.008, 0.02), steel, (0, 0, 0.0)))
        bpy.ops.mesh.primitive_torus_add(major_radius=0.012, minor_radius=0.004, location=(0, 0, 0.51))
        eye = studio.smooth(bpy.context.object)
        eye.rotation_euler = (math.radians(90), 0, 0)
        add(studio.assign(eye, steel))
    elif kind == "drop":
        tear = glow("tears of oberon", (0.40, 0.62, 1.0), 1.0, rough=0.05)
        add(lathe([(0.001, 0.0), (0.06, 0.012), (0.1, 0.07), (0.09, 0.13), (0.05, 0.2), (0.02, 0.26), (0.001, 0.3)], tear))
    elif kind == "orb":
        g = gold_mat((0.80, 0.62, 0.30))
        add(shape("ball", (0.13, 0.13, 0.13), glow(f"orb {a['colour']}", a["colour"], 0.8, rough=0.05), (0, 0, 0.2)))
        add(shape("cylinder", (0.12, 0.12, 0.025), g, (0, 0, 0.025)))
        add(shape("cone", (0.09, 0.09, 0.04), g, (0, 0, 0.09), (math.pi, 0, 0)))
        for k in range(4):
            t = k * math.pi / 2 + math.pi / 4
            add(tube([(0.07 * math.cos(t), 0.07 * math.sin(t), 0.05), (0.12 * math.cos(t), 0.12 * math.sin(t), 0.12),
                      (0.11 * math.cos(t), 0.11 * math.sin(t), 0.2)], 0.012, g, 6))
    elif kind == "card":
        bpy.ops.mesh.primitive_plane_add(size=1, location=(0, -0.006, 0.18))
        face = bpy.context.object
        face.scale = (0.24, 0.36, 1)
        face.rotation_euler = (math.radians(90), 0, 0)
        add(studio.assign(face, painted(f"card {a['face']}", card_face(a["face"]))))
        add(shape("box", (0.122, 0.005, 0.182), mat("card back", (0.55, 0.12, 0.14), scale=60), (0, 0, 0.18)))
    elif kind == "letter":
        page = painted("letter", letter_page())
        for k, t in enumerate((0.25, -0.05)):
            bpy.ops.mesh.primitive_plane_add(size=1, location=(0.03 * k, -0.01 * k, 0.17))
            sheet = bpy.context.object
            sheet.scale = (0.26, 0.32, 1)
            sheet.rotation_euler = (math.radians(90), t, 0)
            add(studio.assign(sheet, page if k else mat("paper", (0.86, 0.82, 0.70), scale=40)))
        add(shape("cylinder", (0.04, 0.04, 0.01), mat("wax", (0.70, 0.08, 0.08), rough=0.3), (0.06, -0.03, 0.06),
                  (math.radians(90), 0, 0)))
    elif kind == "fang":
        ivory = mat("fang", (0.93, 0.88, 0.74), (0.62, 0.52, 0.36), scale=20, rough=0.4)
        verts, faces, n, s = [], [], 12, 8
        for k in range(n + 1):
            t = k / n
            r = 0.04 * (1 - t) + 0.002
            cx, cz = 0.1 * math.sin(t * 1.6), 0.28 - 0.26 * t
            for j in range(s):
                u = 2 * math.pi * j / s
                verts.append((cx + r * math.cos(u), r * 0.7 * math.sin(u), cz))
        for k in range(n):
            for j in range(s):
                i, i2 = k * s + j, k * s + (j + 1) % s
                faces.append((i, i2, i2 + s, i + s))
        add(mesh("fang", verts, faces, ivory))
        add(tube(arc(0.13, 0.2, math.pi + 0.6, 14, (-0.06, 0, 0.3)), 0.008, mat("cord", (0.30, 0.20, 0.12)), 6))
        add(shape("cylinder", (0.042, 0.042, 0.02), gold_mat((0.7, 0.55, 0.3)), (0, 0, 0.29)))
    elif kind == "tag":
        stone = glow(f"tag {e['name']}", a["colour"], 0.15, rough=0.6)
        slab = shape("box", (0.1, 0.03, 0.13), stone, (0, 0, 0.14))
        slab.modifiers["soft"].levels = 2
        add(slab)
        rune = glow(f"rune {a['rune']}", a["rune"], 2.5)
        for z, w in ((0.19, 0.05), (0.14, 0.03), (0.09, 0.05)):
            add(shape("box", (w, 0.006, 0.008), rune, (0, -0.032, z)))
        add(shape("box", (0.008, 0.006, 0.06), rune, (0, -0.033, 0.14)))
        add(tube(arc(0.1, 0.3, math.pi - 0.3, 12, (0, 0, 0.24)), 0.007, mat("cord", (0.30, 0.20, 0.12)), 6))
    elif kind == "brooch":
        g = gold_mat()
        add(shape("box", (0.11, 0.025, 0.11), g, (0, 0, 0.16), (0, math.pi / 4, 0)))
        add(flat(ico(0.07, glow("angel memory", (0.25, 0.45, 0.98), 0.9), 1, (0, -0.03, 0.16), (1, 0.6, 1.25))))
        for x, z in ((0.15, 0.16), (-0.15, 0.16), (0, 0.31), (0, 0.01)):
            add(shape("ball", (0.02, 0.02, 0.02), glow("pearl", (0.95, 0.95, 0.92), 0.2, 0.3), (x, -0.02, z)))
    elif kind == "earrings":
        jade = mat("jade", (0.28, 0.66, 0.42), (0.10, 0.36, 0.20), scale=8, rough=0.2, bump=0.05)
        g = gold_mat()
        for x in (-0.08, 0.08):
            add(shape("ball", (0.045, 0.03, 0.07), jade, (x, 0, 0.08)))
            add(shape("ball", (0.015, 0.015, 0.015), g, (x, 0, 0.16)))
            add(tube(arc(0.04, -0.3, math.pi, 10, (x + 0.03, 0, 0.18)), 0.005, g, 6))
    elif kind == "cup":
        china = mat("porcelain", (0.95, 0.95, 0.93), (0.78, 0.80, 0.82), scale=10, rough=0.25, bump=0.02)
        blue = mat("glaze", (0.15, 0.25, 0.70), rough=0.25)
        add(lathe([(0.001, 0.0), (0.06, 0.0), (0.065, 0.02), (0.1, 0.12), (0.115, 0.2), (0.105, 0.2), (0.09, 0.12),
                   (0.05, 0.03), (0.001, 0.03)], china))
        bpy.ops.mesh.primitive_torus_add(major_radius=0.107, minor_radius=0.006, location=(0, 0, 0.17))
        add(studio.assign(studio.smooth(bpy.context.object), blue))
        add(tube(arc(0.045, -math.pi / 2, math.pi / 2, 10, (0.11, 0, 0.11)), 0.012, china, 6))
    elif kind == "heraldry":
        iron = mat("heraldry", (0.28, 0.30, 0.36), (0.10, 0.10, 0.14), scale=30, rough=0.35)
        g = gold_mat()
        add(shape("box", (0.13, 0.02, 0.09), iron, (0, 0, 0.24)))
        add(shape("cone", (0.13, 0.02, 0.15), iron, (0, 0, 0.0), (math.pi, 0, 0)))
        made[-1].location = (0, 0, 0.06)
        add(tube([(-0.14, -0.022, 0.33), (0.14, -0.022, 0.33)], 0.012, g, 6))
        add(tube([(-0.14, -0.022, 0.33), (-0.13, -0.022, 0.15), (0, -0.022, -0.09), (0.13, -0.022, 0.15),
                  (0.14, -0.022, 0.33)], 0.012, g, 6))
        add(shape("ball", (0.04, 0.02, 0.05), glow("heraldry stone", (0.30, 0.70, 0.98), 1.0), (0, -0.03, 0.18)))
        for t in (0.6, -0.6):
            add(shape("ball", (0.07, 0.01, 0.02), g, (0.06 * math.copysign(1, t), -0.025, 0.24), (0, t, 0)))
    elif kind == "musicbox":
        wood = mat("music box", (0.50, 0.26, 0.14), (0.20, 0.08, 0.04), scale=12, rough=0.4, stretch=(1, 0.1, 1))
        g = gold_mat()
        add(shape("box", (0.15, 0.11, 0.07), wood, (0, 0, 0.07)))
        lid = shape("box", (0.155, 0.115, 0.02), wood, (0, 0.1, 0.22), (math.radians(-60), 0, 0))
        add(lid)
        add(shape("cylinder", (0.03, 0.03, 0.09), g, (0, -0.02, 0.15), (0, math.radians(90), 0)))
        for x in (-0.15, 0.15):
            for y in (-0.11, 0.11):
                add(shape("box", (0.012, 0.012, 0.072), g, (x, y, 0.07)))
        add(shape("cylinder", (0.008, 0.008, 0.04), g, (0.19, 0, 0.08), (0, math.radians(90), 0)))
        add(shape("box", (0.008, 0.008, 0.04), g, (0.23, 0, 0.11)))
        add(shape("ball", (0.02, 0.02, 0.02), g, (0.06, -0.115, 0.08)))
    elif kind == "coral_ring":
        coral = mat("coral", (0.95, 0.42, 0.38), (0.62, 0.18, 0.16), scale=40, rough=0.5, bump=0.6)
        bpy.ops.mesh.primitive_torus_add(major_radius=0.13, minor_radius=0.03, location=(0, 0, 0.16))
        band = studio.smooth(bpy.context.object)
        band.rotation_euler = (math.radians(90), 0, 0)
        add(studio.assign(band, coral))
        for t in (-0.4, 0.0, 0.4):
            add(shape("ball", (0.035, 0.03, 0.045), coral, (0.05 * math.sin(t) * 3, 0, 0.31 + 0.01 * math.cos(t)), (0, t, 0)))
        add(shape("ball", (0.03, 0.03, 0.03), glow("pearl", (0.95, 0.95, 0.92), 0.2, 0.3), (0, -0.03, 0.31)))
    else:
        raise SystemExit(f"no model for kind {kind}")
    return made


def fake_unique(e, n):
    """An item for uniques.py's models: its kind, cells and look."""
    a = e["args"]
    level = a.get("level", (10, 30, 50, 70)[a["metal"]])
    return {"class": "weapon" if e["kind"][7:] in uniques.BLADES or e["kind"][7:] in uniques.MODELS else
            ("armour" if e["kind"][7:] in ("plate", "boardshield") else "accessory"),
            "code": a["code"], "kind": e["kind"][7:], "level": f"{level},1", "mod": [], "element-mod": [],
            "size": f"{e['size'][0]},{e['size'][1]}", "name": e["name"], "colour": a["colour"]}


def render_unique(e, n, out):
    item = fake_unique(e, n)
    original = uniques.look

    def look(i):
        metal, _, glow_, rng = original(i)
        return metal, i["colour"], 0.6, rng
    uniques.look = look
    try:
        uniques.render_item(item, out, f"{n:03d}")
        builder = uniques.build_armour if item["class"] == "armour" else uniques.build_weapon
        if e["bag"] not in ((0, 0, -25), (0, 0, 0)) or "dark" in e["args"]:
            # Turned in the bag (a short sword lying across its cell), or darker.
            render_extra(e, n, out, bag_only=True, objects_from=lambda: builder(item))
    finally:
        uniques.look = original


def render_extra(e, n, out, bag_only=False, objects_from=None):
    w, h = e["size"]
    scene = studio.reset(samples=64)
    catcher = bpy.data.objects["shadow catcher"]
    handle = bpy.data.objects.new("item", None)
    scene.collection.objects.link(handle)
    objects = objects_from() if objects_from else build(e)
    for o in objects:
        if o.parent is None:
            o.parent = handle
    if "dark" in e["args"]:  # plain iron: darker, and without the deep items' glow
        for m in {s.material for o in objects for s in o.material_slots if s.material}:
            for node in m.node_tree.nodes:
                if node.type == "VALTORGB":
                    for el in node.color_ramp.elements:
                        el.color = (*(c * e["args"]["dark"] for c in el.color[:3]), 1)
            bsdf = m.node_tree.nodes["Principled BSDF"]
            bsdf.inputs["Roughness"].default_value = 0.45
            bsdf.inputs["Emission Strength"].default_value = 0
    studio.camera(scene, w * CELL, h * CELL)
    catcher.hide_render = True
    sun = bpy.data.objects["sun"].data
    sun.energy *= 1.4
    handle.rotation_euler = tuple(math.radians(d) for d in e["bag"])
    items.inventory_camera(scene, objects, w * CELL, h * CELL)
    scene.camera.data.ortho_scale /= e["fill"]
    studio.render(scene, str(out / f"inv_{n:03d}.png"))
    if bag_only or not e["grounds"]:
        return
    for o in list(scene.objects):
        if o.type == "CAMERA":
            bpy.data.objects.remove(o)
    catcher.hide_render = False
    sun.energy /= 1.4
    studio.camera(scene, 128, 128)
    handle.rotation_euler = (0, 0, math.radians(20 + 37 * n % 140))
    if e["kind"] in ("card", "letter", "feathers", "hair", "crest", "tag", "fang", "brooch", "heraldry", "needle",
                     "medicine", "mandragora", "antidote"):
        # Flat or tall things lie on their backs.
        handle.rotation_euler = (math.radians(-90), 0, math.radians(20 + 37 * n % 140))
    handle.scale = Vector((e["ground"],) * 3)
    bpy.context.view_layer.update()
    points = items.bounds(objects)
    low = min(p.z for p in points)
    centre = sum(points, Vector()) / len(points)
    handle.location = (-centre.x, -centre.y, -low)
    studio.render(scene, str(out / f"ground_{n:03d}.png"))
    ax, ay = studio.anchor(scene)
    (out / f"anchor_{n:03d}.txt").write_text(f"{ax:.0f} {ay:.0f}\n")


def main():
    args = sys.argv[1:]
    if args[0] == "render":
        todo = list(range(len(EXTRAS)))
        if "--only" in args:
            k = args.index("--only")
            todo = [int(x) for x in args[k + 1].split(",")]
            del args[k:k + 2]
        if "--group" in args:
            k = args.index("--group")
            todo = GROUPS[args[k + 1]]
            del args[k:k + 2]
        out = Path(args[1])
        out.mkdir(parents=True, exist_ok=True)
        for n in todo:
            e = EXTRAS[n]
            if e["kind"].startswith("unique:"):
                render_unique(e, n, out)
            else:
                render_extra(e, n, out)
            print(n, e["name"], "done", flush=True)
        return
    from PIL import Image

    sys.path.insert(0, str(HERE))
    from uniques import write_grid

    renders, pack = Path(args[1]), Path(args[2])
    inventory, ground = [], []
    grounds = ground_frames()
    for n, e in enumerate(EXTRAS):
        inventory.append((Image.open(renders / f"inv_{n:03d}.png").convert("RGBA"), 0, 0))
        if n in grounds:
            im = Image.open(renders / f"ground_{n:03d}.png").convert("RGBA")
            box = im.getbbox() or (0, 0, 1, 1)
            ax, ay = map(int, (renders / f"anchor_{n:03d}.txt").read_text().split())
            ground.append((im.crop(box), ax - box[0], ay - box[1]))
    write_grid(inventory, pack / "System/Game/Pattern", f"Item{BANK:04d}")
    write_grid(ground, pack / f"Character/ITEM/{FOLDER:08d}", "Pattern")
    for path in (pack / "System/Game/Pattern" / f"Item{BANK:04d}.sheet",
                 pack / f"Character/ITEM/{FOLDER:08d}/Pattern.sheet"):
        text = path.read_text().replace("tools/art/uniques.py", "tools/art/item_extras.py")
        path.write_text(text)
    print(f"wrote {len(inventory)} bag and {len(ground)} ground pictures")


if __name__ == "__main__":
    main()
