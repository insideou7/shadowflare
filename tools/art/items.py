"""Item pictures for the open content pack, rendered with studio.py.

    python tools/art/items.py render <folder> [--only N,M]
    python3 tools/art/items.py pack <folder> <content pack folder>
    python tools/art/items.py placed <content pack folder>

The first step needs a Python with the `bpy` module (like creatures.py)
and renders every item into <folder>; the second needs Pillow and lays
the renders out as the pack's sprite sheets. Every item in ITEMS gets
two pictures, in the pack's layout (docs/content.md):

- an inventory picture, System/Game/Pattern/Item0000.png + .sheet: the
  item seen from the front, filling its size in 32-pixel inventory cells,
  as the original's Item%04d pictures do,
- a ground picture, Character/ITEM/00000007/Pattern.png + .sheet: the
  item lying on the ground, seen through the game's camera at the same
  scale as the characters (small things a little larger, so they can be
  seen and clicked), anchored where it touches the ground.

Frames are numbered in the order of ITEMS; content/items.txt refers to
them with icon=0,<n> and ground=7,<n>. Packing keeps the pack's current
picture for any item not rendered this time, so one new item can be
rendered with --only and packed without redrawing the rest.

`placed` draws a laid mine for the engine's mines, as the original's
Character/OPTION/00001000/Pattern: picture 0 just laid, picture 1 armed
(its prongs out and its lamp lit). Weapons, shields and body armour
are the same models the hero wears (creatures.GEAR), so an item looks the
same in the bag, on the ground and on the hero.
"""

import math
import sys
from pathlib import Path

try:
    import bpy
    from mathutils import Matrix, Vector

    sys.path.insert(0, str(Path(__file__).resolve().parent))
    import creatures  # noqa: E402
    import studio  # noqa: E402
except ImportError:  # packing only
    bpy = None

CELL = 32
HERO_SCALE = 1.22  # the hero rig's scale (creatures.build_hero_shapes)

# (name, builder, width and height in cells, ground scale relative to the
# hero, ground angle in degrees). Builders return (objects, kind) where
# kind says how to stand the model up: "grip" (a weapon along +X of its
# grip), "shield" (a disc facing +X), "armour" (worn on the hero's rig),
# "thing" (a model standing at the origin, its front towards -Y) or
# "pendant" (a flat charm standing at the origin, its face towards -Y,
# that lies on its back on the ground). A ("blade", name) builder is a
# weapon the hero does not wear, built by blade() along +X of its grip;
# ("pendant", name) is built by pendant().
ITEMS = [
    ("short_sword", ("gear", 12), 1, 4, 1.15, 30),
    ("great_sword", ("gear", 19), 2, 4, 1.0, 35),
    ("cloth_armour", ("gear", 5), 2, 3, 0.85, 20),
    ("round_shield", ("gear", 9), 2, 2, 1.0, 0),
    ("leather_hood", ("thing", "hood"), 2, 2, 1.4, 20),
    ("leather_boots", ("thing", "boots"), 2, 2, 1.3, 20),
    ("copper_ring", ("thing", "ring"), 1, 1, 2.2, 0),
    ("healing_potion", ("thing", "red_potion"), 1, 1, 2.0, 0),
    ("mana_potion", ("thing", "blue_potion"), 1, 1, 2.0, 0),
    ("gold", ("thing", "gold"), 1, 1, 2.2, 0),
    ("long_sword", ("gear", 17), 1, 4, 1.05, 40),
    ("magic_staff", ("gear", 18), 1, 3, 1.0, 25),
    ("plate_mail", ("gear", 6), 2, 3, 0.85, 20),
    ("mine", ("thing", "mine"), 2, 2, 1.6, 0),
    ("wood_bowgun", ("gear", 16), 2, 3, 1.4, 30),
    ("merchant_sapphire", ("thing", "sapphire"), 1, 1, 2.2, 15),
    ("shrine_stone", ("thing", "shrine_stone"), 1, 1, 2.0, 0),
    ("keepsake_ruby", ("thing", "ruby"), 1, 1, 2.2, 20),
    ("might_charm", ("pendant", "might_charm"), 1, 1, 2.0, 15),
    ("shrine_charm", ("pendant", "shrine_charm"), 1, 1, 2.0, -15),
    ("blast_charm", ("pendant", "blast_charm"), 1, 1, 2.0, 10),
    ("warding_cross", ("pendant", "warding_cross"), 1, 1, 2.0, 20),
    ("scarlet_dirk", ("blade", "scarlet_dirk"), 1, 3, 1.3, 30),
    ("old_knight_sword", ("blade", "old_knight_sword"), 1, 4, 1.0, 40),
]

# A laid mine is drawn larger than the mine item, as the original's is, so
# it can be seen where it lies.
MINE_PLACED_SCALE = 2.3

HEADER = "# Rendered by tools/art/items.py (our own art, see content/README.md).\n"


def mat(name, colour, dark=None, **kw):
    return studio.material(name, colour, dark, **kw)


def glass(name, colour, glow):
    m = studio.material(name, colour, scale=6, rough=0.1, bump=0)
    bsdf = m.node_tree.nodes["Principled BSDF"]
    bsdf.inputs["Emission Color"].default_value = (*studio.linear(colour), 1)
    bsdf.inputs["Emission Strength"].default_value = glow
    return m


def thing(kind):
    """Small items the hero does not wear visibly; built at the origin,
    standing on z = 0 with their front towards -Y."""
    shape = creatures.shape
    leather = mat("hood leather", (0.42, 0.27, 0.15), (0.18, 0.10, 0.05), scale=20, rough=0.6, bump=0.4)
    dark = mat("dark leather", (0.24, 0.16, 0.10), (0.09, 0.06, 0.04), scale=20, rough=0.6, bump=0.4)
    brass = mat("brass", (0.80, 0.62, 0.30), (0.40, 0.28, 0.10), scale=30, rough=0.35)
    made = []
    add = made.append
    if kind == "hood":
        add(shape("ball", (0.22, 0.24, 0.21), leather))
        made[-1].location = (0, 0, 0.26)
        add(shape("box", (0.03, 0.2, 0.16), dark))  # a flap over the neck, at the back
        made[-1].location = (0, 0.2, 0.12)
        made[-1].rotation_euler = (math.radians(-20), 0, math.radians(90))
        add(shape("flare", (0.26, 0.25, 0.13), dark))
        made[-1].location = (0, 0, 0.13)
        add(shape("cylinder", (0.225, 0.245, 0.03), dark))
        made[-1].location = (0, 0, 0.27)
        for side in (1, -1):  # cheek flaps
            add(shape("box", (0.09, 0.03, 0.12), leather))
            made[-1].location = (0.22 * side, -0.03, 0.12)
            made[-1].rotation_euler = (0, 0, math.radians(90))
        add(shape("ball", (0.035, 0.035, 0.035), brass))
        made[-1].location = (0.02, -0.235, 0.27)
    elif kind == "boots":
        for side in (1, -1):
            x = 0.16 * side
            add(shape("cylinder", (0.12, 0.12, 0.22), dark))
            made[-1].location = (x, 0, 0.30)
            add(shape("cylinder", (0.14, 0.14, 0.05), leather))
            made[-1].location = (x, 0, 0.52)
            add(shape("box", (0.11, 0.24, 0.07), dark))  # the foot points towards -Y, the front
            made[-1].location = (x, -0.1, 0.07)
            add(shape("cylinder", (0.125, 0.125, 0.025), brass))
            made[-1].location = (x, 0, 0.40)
    elif kind == "ring":
        copper = mat("copper", (0.84, 0.50, 0.30), (0.42, 0.20, 0.10), scale=30, rough=0.3)
        bpy.ops.mesh.primitive_torus_add(major_radius=0.14, minor_radius=0.035, major_segments=32, minor_segments=10)
        ring = studio.smooth(bpy.context.object)
        studio.assign(ring, copper)
        ring.rotation_euler = (math.radians(90), 0, 0)
        ring.location = (0, 0, 0.175)
        add(ring)
        add(shape("ball", (0.05, 0.05, 0.05), glass("ring stone", (0.75, 0.10, 0.12), 0.6)))
        made[-1].location = (0, 0, 0.33)
    elif kind in ("red_potion", "blue_potion"):
        colour = (0.80, 0.08, 0.06) if kind == "red_potion" else (0.10, 0.30, 0.90)
        add(shape("ball", (0.15, 0.15, 0.15), glass(kind, colour, 0.5)))
        made[-1].location = (0, 0, 0.15)
        add(shape("cylinder", (0.05, 0.05, 0.08), glass("flask neck", (0.70, 0.80, 0.78), 0.1)))
        made[-1].location = (0, 0, 0.34)
        add(shape("cylinder", (0.045, 0.045, 0.04), mat("cork", (0.62, 0.44, 0.26), scale=40)))
        made[-1].location = (0, 0, 0.44)
    elif kind in ("mine", "mine_armed"):
        # A squat iron pot on the ground with a brass band, a plunger on
        # top and a little red lamp; armed, three prongs fold out and the
        # lamp lights.
        armed = kind == "mine_armed"
        iron = mat("mine iron", (0.30, 0.30, 0.31), (0.12, 0.12, 0.13), scale=25, rough=0.45, bump=0.3)
        add(shape("cylinder", (0.5, 0.5, 0.08), iron))
        made[-1].location = (0, 0, 0.08)
        add(shape("ball", (0.43, 0.43, 0.15), iron))
        made[-1].location = (0, 0, 0.15)
        add(shape("cylinder", (0.52, 0.52, 0.03), brass))
        made[-1].location = (0, 0, 0.11)
        for k in range(8):
            a = k * math.pi / 4 + math.pi / 8
            add(shape("ball", (0.035, 0.035, 0.035), brass))
            made[-1].location = (0.52 * math.cos(a), 0.52 * math.sin(a), 0.11)
        add(shape("cylinder", (0.08, 0.08, 0.05 if not armed else 0.08), iron))
        made[-1].location = (0, 0, 0.32 if not armed else 0.35)
        add(shape("cylinder", (0.1, 0.1, 0.02), brass))
        made[-1].location = (0, 0, 0.38 if not armed else 0.44)
        add(shape("ball", (0.055, 0.055, 0.045), glass("mine lamp", (0.85, 0.12, 0.06), 6.0 if armed else 0.15)))
        made[-1].location = (-0.2, 0.12, 0.28)
        if armed:
            for k in range(3):
                a = math.radians(90 + 120 * k)
                add(shape("box", (0.2, 0.03, 0.018), iron))
                made[-1].location = (0.66 * math.cos(a), 0.66 * math.sin(a), 0.03)
                made[-1].rotation_euler = (0, 0, a)
                add(shape("cone", (0.04, 0.04, 0.05), brass))
                made[-1].location = (0.86 * math.cos(a), 0.86 * math.sin(a), 0.06)
    elif kind == "gold":
        # A tied leather purse with coins spilled in front of it, so a
        # stack of gold reads clearly in one bag cell.
        gold = mat("gold", (0.98, 0.78, 0.25), (0.55, 0.36, 0.06), scale=30, rough=0.25, bump=0.2)
        purse = mat("purse leather", (0.78, 0.60, 0.38), (0.36, 0.22, 0.11), scale=24, rough=0.6, bump=0.5)
        add(shape("ball", (0.13, 0.12, 0.15), purse))
        made[-1].location = (0.04, 0.08, 0.15)
        add(shape("cone", (0.07, 0.07, 0.08), purse))
        made[-1].location = (0.04, 0.08, 0.32)
        add(shape("cylinder", (0.055, 0.055, 0.015), dark))
        made[-1].location = (0.04, 0.08, 0.28)
        coins = [(-0.11, -0.09, 0), (-0.11, -0.09, 1), (-0.10, -0.08, 2), (-0.11, -0.10, 3), (-0.10, -0.09, 4),
                 (0.07, -0.13, 0), (0.08, -0.12, 1), (0.07, -0.13, 2), (-0.20, 0.02, 0), (0.17, -0.02, 0),
                 (-0.02, -0.18, 0), (0.17, -0.02, 1)]
        for x, y, k in coins:
            add(shape("cylinder", (0.075, 0.075, 0.014), gold))
            made[-1].location = (x, y, 0.012 + 0.025 * k)
            made[-1].rotation_euler = (0.12 * ((k + int(x * 100)) % 3 - 1), 0.1 * (k % 2), 0)
    return made


def gem(colour, radius, glow=0.4, facets=8):
    """A brilliant-cut stone with flat facets, its table up (+Z), sitting on
    its point at the origin."""
    m = studio.material(f"gem {colour}", colour, tuple(c * 0.35 for c in colour), scale=8, rough=0.05, bump=0)
    bsdf = m.node_tree.nodes["Principled BSDF"]
    bsdf.inputs["Emission Color"].default_value = (*studio.linear(colour), 1)
    bsdf.inputs["Emission Strength"].default_value = glow
    bpy.ops.mesh.primitive_cone_add(vertices=facets, radius1=0, radius2=radius, depth=radius * 0.85,
                                    location=(0, 0, radius * 0.425))
    pavilion = studio.assign(bpy.context.object, m)
    bpy.ops.mesh.primitive_cone_add(vertices=facets, radius1=radius, radius2=radius * 0.58, depth=radius * 0.38,
                                    location=(0, 0, radius * 0.85 + radius * 0.19))
    crown = studio.assign(bpy.context.object, m)
    crown.rotation_euler = (0, 0, math.pi / facets)
    return [pavilion, crown]


def tilt(objects, angle, at):
    """Turns objects about X by `angle` round the point `at` (a positive
    angle tips their top towards the viewer, -Y)."""
    pivot = bpy.data.objects.new("pivot", None)
    bpy.context.scene.collection.objects.link(pivot)
    for o in objects:
        o.parent = pivot
    pivot.location = at
    pivot.rotation_euler = (angle, 0, 0)
    bpy.context.view_layer.update()
    for o in objects:
        w = o.matrix_world.copy()
        o.parent = None
        o.matrix_world = w
    bpy.data.objects.remove(pivot)
    return objects


def loop(radius, thickness, at, m):
    """A small ring a charm hangs from, upright facing -Y."""
    bpy.ops.mesh.primitive_torus_add(major_radius=radius, minor_radius=thickness, major_segments=20,
                                     minor_segments=8, location=at, rotation=(math.radians(90), 0, 0))
    return studio.assign(studio.smooth(bpy.context.object), m)


def jewel(kind):
    """The quest stones, standing at the origin, front towards -Y."""
    shape = creatures.shape
    made = []
    if kind == "sapphire":
        # A large blue stone tipped towards the viewer so its table shows.
        made += tilt(gem((0.12, 0.30, 0.95), 0.17, glow=0.5), math.radians(55), (0, 0, 0.06))
    elif kind == "ruby":
        # A red stone held by four gold claws on a little gold cup.
        gold = mat("ruby gold", (0.92, 0.72, 0.28), (0.45, 0.28, 0.06), scale=30, rough=0.25)
        parts = gem((0.88, 0.08, 0.10), 0.14, glow=0.5)
        bpy.ops.mesh.primitive_cone_add(vertices=16, radius1=0.05, radius2=0.12, depth=0.1, location=(0, 0, 0.06))
        parts.append(studio.assign(studio.smooth(bpy.context.object), gold))
        for k in range(4):
            a = math.pi / 4 + k * math.pi / 2
            parts.append(shape("cone", (0.022, 0.022, 0.07), gold))
            parts[-1].location = (0.125 * math.cos(a), 0.125 * math.sin(a), 0.14)
            parts[-1].rotation_euler = (-0.5 * math.sin(a), 0.5 * math.cos(a), 0)
        bpy.ops.mesh.primitive_torus_add(major_radius=0.13, minor_radius=0.015, major_segments=24, minor_segments=6,
                                         location=(0, 0, 0.11))
        parts.append(studio.assign(studio.smooth(bpy.context.object), gold))
        made += tilt(parts, math.radians(45), (0, 0, 0.04))
    elif kind == "shrine_stone":
        # A smooth pale stone, a little egg-shaped, glowing softly, with
        # three carved marks down its face.
        pale = mat("shrine stone", (0.90, 0.92, 0.84), (0.62, 0.66, 0.64), scale=7, rough=0.4, bump=0.2)
        bsdf = pale.node_tree.nodes["Principled BSDF"]
        bsdf.inputs["Emission Color"].default_value = (*studio.linear((0.85, 0.95, 1.0)), 1)
        bsdf.inputs["Emission Strength"].default_value = 0.6
        add = made.append
        add(shape("ball", (0.15, 0.12, 0.19), pale))
        made[-1].location = (0, 0, 0.19)
        rune = glass("shrine rune", (0.55, 0.85, 1.0), 3.0)
        for z, w in ((0.27, 0.07), (0.19, 0.045), (0.11, 0.07)):
            add(shape("box", (w, 0.012, 0.012), rune))
            made[-1].location = (0, -0.118 + 0.02 * abs(z - 0.19) / 0.08, z)
        add(shape("box", (0.012, 0.012, 0.09), rune))
        made[-1].location = (0, -0.121, 0.19)
    return made


def pendant(kind):
    """Charms on a loop, built upright at the origin with their face towards
    -Y; they lie on their backs on the ground."""
    shape = creatures.shape
    made = []
    add = made.append
    if kind == "might_charm":
        # A rounded red-brown stone in a bronze rim on a leather thong.
        bronze = mat("bronze", (0.70, 0.46, 0.24), (0.30, 0.17, 0.07), scale=30, rough=0.35, bump=0.3)
        stone = mat("might stone", (0.62, 0.22, 0.14), (0.28, 0.08, 0.05), scale=9, rough=0.3, bump=0.3)
        bsdf = stone.node_tree.nodes["Principled BSDF"]
        bsdf.inputs["Emission Color"].default_value = (*studio.linear((0.7, 0.2, 0.1)), 1)
        bsdf.inputs["Emission Strength"].default_value = 0.25
        add(shape("cylinder", (0.15, 0.15, 0.03), bronze, rotation=(math.radians(90), 0, 0)))
        made[-1].location = (0, 0, 0.16)
        add(shape("ball", (0.11, 0.05, 0.13), stone))
        made[-1].location = (0, -0.03, 0.16)
        for k in range(6):
            a = k * math.pi / 3
            add(shape("ball", (0.02, 0.02, 0.02), bronze))
            made[-1].location = (0.135 * math.cos(a), -0.03, 0.16 + 0.135 * math.sin(a))
        rune = mat("might rune", (0.95, 0.78, 0.36), (0.55, 0.38, 0.10), scale=30, rough=0.3)
        for k in range(3):  # a gold triangle inlaid in the stone
            a = math.radians(90 + 120 * k)
            add(shape("box", (0.05, 0.008, 0.008), rune, rotation=(0, -a - math.radians(90), 0)))
            made[-1].location = (0.035 * math.cos(a + math.pi), -0.08, 0.16 + 0.035 * math.sin(a + math.pi))
        add(loop(0.045, 0.014, (0, 0, 0.33), bronze))
    elif kind == "shrine_charm":
        # A violet stone in a silver teardrop with pale pearls round it.
        silver = mat("silver", (0.86, 0.86, 0.90), (0.45, 0.45, 0.52), scale=24, rough=0.22, bump=0.2)
        pearl = mat("pearl", (0.96, 0.94, 0.98), (0.75, 0.72, 0.80), scale=20, rough=0.2, bump=0)
        add(shape("ball", (0.12, 0.025, 0.16), silver))
        made[-1].location = (0, 0, 0.16)
        add(shape("cone", (0.06, 0.025, 0.06), silver))
        made[-1].location = (0, 0, 0.32)
        for k in range(10):
            a = k * 2 * math.pi / 10
            add(shape("ball", (0.022, 0.022, 0.022), pearl))
            made[-1].location = (0.13 * math.sin(a), -0.02, 0.16 + 0.17 * math.cos(a))
        stone = gem((0.72, 0.42, 0.98), 0.085, glow=1.2)
        made += tilt(stone, math.radians(90), (0, -0.03, 0.15))
        add(loop(0.04, 0.012, (0, 0, 0.39), silver))
    elif kind == "blast_charm":
        # A black iron sun with a fiery orange core and eight short rays.
        iron = mat("charm iron", (0.20, 0.19, 0.20), (0.06, 0.05, 0.06), scale=26, rough=0.4, bump=0.5)
        core = glass("blast core", (1.0, 0.45, 0.08), 3.5)
        add(shape("cylinder", (0.12, 0.12, 0.035), iron, rotation=(math.radians(90), 0, 0)))
        made[-1].location = (0, 0, 0.18)
        for k in range(8):
            a = k * math.pi / 4
            add(shape("cone", (0.03, 0.02, 0.05), iron, rotation=(0, math.pi / 2 - a, 0)))
            made[-1].location = (0.15 * math.cos(a), 0, 0.18 + 0.15 * math.sin(a))
        add(shape("ball", (0.07, 0.04, 0.07), core))
        made[-1].location = (0, -0.035, 0.18)
        bpy.ops.mesh.primitive_torus_add(major_radius=0.085, minor_radius=0.014, major_segments=24, minor_segments=6,
                                         location=(0, -0.035, 0.18), rotation=(math.radians(90), 0, 0))
        add(studio.assign(studio.smooth(bpy.context.object), iron))
        add(loop(0.04, 0.012, (0, 0, 0.37), iron))
    elif kind == "warding_cross":
        # A silver cross with flared arms and a small white stone at the
        # crossing.
        silver = mat("cross silver", (0.88, 0.88, 0.92), (0.40, 0.42, 0.48), scale=24, rough=0.2, bump=0.2)
        add(shape("box", (0.035, 0.022, 0.17), silver))
        made[-1].location = (0, 0, 0.17)
        add(shape("box", (0.12, 0.022, 0.035), silver))
        made[-1].location = (0, 0, 0.22)
        for x, z, r in ((0, 0.0, 0), (0, 0.34, 180), (0.12, 0.22, -90), (-0.12, 0.22, 90)):
            add(shape("cone", (0.05, 0.022, 0.025), silver, rotation=(0, math.radians(r), 0)))
            made[-1].location = (x, 0, z + (0.02 if r == 0 else 0))
        made += tilt(gem((0.92, 0.95, 1.0), 0.035, glow=1.0), math.radians(90), (0, -0.02, 0.22))
        add(loop(0.03, 0.01, (0, 0, 0.39), silver))
    return made


def blade(kind):
    """Swords the hero does not wear, built along +X from the middle of
    the grip like creatures.blade_weapon, the blade's flat facing +Z."""
    shape = creatures.shape
    along = (0, math.radians(90), 0)
    made = []
    add = made.append
    if kind == "scarlet_dirk":
        # A short dagger: a red-tinted steel blade, a gold guard curling
        # forward, a grip wound in red and a ruby for a pommel.
        steel = mat("scarlet steel", (0.80, 0.30, 0.28), (0.34, 0.06, 0.06), scale=24, rough=0.22, bump=0.2)
        gold = mat("dirk gold", (0.90, 0.70, 0.28), (0.42, 0.26, 0.06), scale=30, rough=0.25)
        wrap = mat("dirk grip", (0.60, 0.08, 0.08), (0.25, 0.03, 0.03), scale=40, rough=0.6, bump=0.5)
        add(shape("cylinder", (0.03, 0.03, 0.08), wrap, rotation=along))
        add(shape("ball", (0.045, 0.045, 0.045), glass("dirk ruby", (0.85, 0.05, 0.08), 0.6)))
        made[-1].location = (-0.115, 0, 0)
        add(shape("box", (0.025, 0.11, 0.025), gold))
        made[-1].location = (0.095, 0, 0)
        for side in (1, -1):
            add(shape("ball", (0.025, 0.025, 0.025), gold))
            made[-1].location = (0.115, 0.11 * side, 0)
        length, width = 0.42, 0.045
        b = shape("box", (length / 2, width, 0.015), steel)
        b.modifiers.clear()
        b.location = (0.12 + length / 2, 0, 0)
        add(b)
        add(shape("cone", (0.015, width * 1.02, width * 2.4), steel, rotation=along))
        made[-1].location = (0.12 + length + width * 2.4, 0, 0)
        r = shape("box", (length / 2, width * 0.28, width * 0.28), steel, rotation=(math.radians(45), 0, 0))
        r.modifiers.clear()
        r.location = (0.12 + length / 2, 0, 0)
        add(r)
    elif kind == "old_knight_sword":
        # A broad old blade with a dark fuller, a straight steel cross-guard
        # with knobbed ends, a wire-bound grip and a wheel pommel.
        steel = mat("knight steel", (0.86, 0.87, 0.90), (0.40, 0.41, 0.45), scale=20, rough=0.28, bump=0.2,
                    grime=0.3)
        fuller = mat("knight fuller", (0.36, 0.37, 0.40), (0.14, 0.14, 0.16), scale=30, rough=0.4)
        guard = mat("knight guard", (0.55, 0.56, 0.60), (0.20, 0.20, 0.22), scale=24, rough=0.3, bump=0.3)
        grip = mat("knight grip", (0.24, 0.16, 0.10), (0.09, 0.06, 0.04), scale=40, rough=0.6, bump=0.5)
        brass = mat("knight brass", (0.80, 0.62, 0.30), (0.40, 0.28, 0.10), scale=30, rough=0.35)
        add(shape("cylinder", (0.036, 0.036, 0.15), grip, rotation=along))
        for x in (-0.1, -0.03, 0.04, 0.11):
            add(shape("cylinder", (0.039, 0.039, 0.008), brass, rotation=along))
            made[-1].location = (x, 0, 0)
        add(shape("cylinder", (0.075, 0.075, 0.03), guard))  # the wheel pommel, its faces with the blade's
        made[-1].location = (-0.19, 0, 0)
        for z in (1, -1):
            add(shape("ball", (0.03, 0.03, 0.02), brass))
            made[-1].location = (-0.19, 0, 0.03 * z)
        add(shape("box", (0.04, 0.27, 0.035), guard))
        made[-1].location = (0.19, 0, 0)
        for side in (1, -1):
            add(shape("ball", (0.045, 0.045, 0.045), guard))
            made[-1].location = (0.19, 0.29 * side, 0)
        add(shape("box", (0.06, 0.085, 0.045), guard))  # the langet over the blade's root
        made[-1].location = (0.25, 0, 0)
        length, width = 1.2, 0.11
        b = shape("box", (length / 2, width, 0.022), steel)
        b.modifiers.clear()
        b.location = (0.23 + length / 2, 0, 0)
        add(b)
        add(shape("cone", (0.022, width * 1.02, width * 1.8), steel, rotation=along))
        made[-1].location = (0.23 + length + width * 1.8, 0, 0)
        for z in (1, -1):
            f = shape("box", (length * 0.36, width * 0.22, 0.004), fuller)
            f.modifiers.clear()
            f.location = (0.27 + length * 0.36, 0, 0.022 * z)
            add(f)
    return made


def build(spec):
    """Builds an item; returns (handle, objects, kind). The handle is an
    empty that moves the whole model."""
    source, what = spec
    if source in ("thing", "pendant", "blade"):
        handle = bpy.data.objects.new("item", None)
        bpy.context.scene.collection.objects.link(handle)
        if source == "blade":
            objects = blade(what)
            handle.scale = (HERO_SCALE,) * 3
        elif source == "pendant":
            objects = pendant(what)
        else:
            objects = thing(what) or jewel(what)
        for o in objects:
            o.parent = handle
        return handle, objects, {"thing": "thing", "pendant": "pendant", "blade": "grip"}[source]
    rig = creatures.build_hero_shapes()
    for o in [o for o in bpy.data.objects if o.type == "MESH" and o.name != "shadow catcher"]:
        bpy.data.objects.remove(o)
    objects = creatures.build_gear(rig, what)
    if creatures.GEAR[what][2]:
        # Body armour: the jacket or cuirass only, without the legs.
        legs = {rig.joints["leg_l"], rig.joints["leg_r"]}
        for o in [o for o in objects if o.parent in legs]:
            objects.remove(o)
            bpy.data.objects.remove(o)
        rig.pose({"arm_l": (0.3, 0, 0), "arm_r": (-0.3, 0, 0)})
        return rig.joints["root"], objects, "armour"
    handle = bpy.data.objects.new("item", None)
    bpy.context.scene.collection.objects.link(handle)
    handle.scale = (HERO_SCALE,) * 3
    kind = "grip" if any(o.parent.name.startswith("grip") for o in objects) else "shield"
    tops = {o.parent if o.parent.name.startswith("grip") else o for o in objects}
    for top in tops:
        if top.name.startswith("grip"):
            top.parent = handle
            top.location = (0, 0, 0)
            top.rotation_euler = (0, 0, 0)
        else:
            top.parent = handle
    return handle, objects, kind


def orient(handle, kind, lying, angle=0.0):
    """Turns the model to face the inventory camera (-Y), or lays it down
    on the ground turned by `angle` degrees."""
    a = math.radians(angle)
    if kind == "grip":
        if lying:
            # Blade along the ground, its flat facing up.
            x, z = Vector((math.cos(a), math.sin(a), 0)), Vector((0, 0, 1))
        else:
            # Point up, flat towards the camera.
            x, z = Vector((0, 0, 1)), Vector((0, -1, 0))
        m = Matrix((x, z.cross(x), z)).transposed()
        handle.rotation_euler = m.to_euler()
    elif kind == "shield":
        handle.rotation_euler = (0, math.radians(-90), a) if lying else (0, 0, math.radians(-90))
    elif kind == "armour":
        # Worn, facing the camera; lying on its back.
        handle.rotation_euler = (0, math.radians(-90), a) if lying else (0, 0, math.radians(-90))
    elif kind == "pendant":
        # Upright in the bag, its face a little turned; on its back on the ground.
        handle.rotation_euler = (math.radians(-90), 0, a) if lying else (0, 0, math.radians(-12))
    else:
        # Small things stand on the ground either way; in the bag they are
        # turned a little to show their sides.
        handle.rotation_euler = (0, 0, a if lying else math.radians(-25))


def bounds(objects):
    bpy.context.view_layer.update()
    return [o.matrix_world @ Vector(c) for o in objects for c in o.bound_box]


def inventory_camera(scene, objects, w, h):
    """An orthographic camera from the front, a little above, framing the
    model in w x h pixels with a pixel of margin."""
    cam = scene.camera
    cam.data.type = "ORTHO"
    elevation = math.radians(12)
    forward = Vector((0, math.cos(elevation), -math.sin(elevation)))
    cam.rotation_euler = (math.pi / 2 - elevation, 0, 0)
    points = bounds(objects)
    centre = sum(points, Vector()) / len(points)
    cam.location = centre - forward * 30
    bpy.context.view_layer.update()
    inv = cam.matrix_world.inverted()
    local = [inv @ p for p in points]
    xs, ys = [p.x for p in local], [p.y for p in local]
    per_pixel = max((max(xs) - min(xs)) / (w - 2), (max(ys) - min(ys)) / (h - 2))
    cam.data.ortho_scale = per_pixel * max(w, h)
    right, up = cam.matrix_world.to_3x3() @ Vector((1, 0, 0)), cam.matrix_world.to_3x3() @ Vector((0, 1, 0))
    cam.location += right * (max(xs) + min(xs)) / 2 + up * (max(ys) + min(ys)) / 2
    scene.render.resolution_x, scene.render.resolution_y = w, h


def render_item(index, out):
    name, spec, w, h, ground_scale, angle = ITEMS[index]
    scene = studio.reset(samples=64)
    catcher = bpy.data.objects["shadow catcher"]
    handle, objects, kind = build(spec)
    studio.camera(scene, w * CELL, h * CELL)
    # Inventory picture: no ground, the model upright and lit a little
    # brighter, like the original's pictures.
    catcher.hide_render = True
    sun = bpy.data.objects["sun"].data
    sun.energy *= 1.4
    orient(handle, kind, lying=False)
    inventory_camera(scene, objects, w * CELL, h * CELL)
    studio.render(scene, str(out / f"inv_{index:02d}.png"))
    # Ground picture: lying in the world, seen through the game's camera.
    for o in list(scene.objects):
        if o.type == "CAMERA":
            bpy.data.objects.remove(o)
    catcher.hide_render = False
    sun.energy /= 1.4
    size = 128
    studio.camera(scene, size, size)
    orient(handle, kind, lying=True, angle=angle)
    handle.scale = Vector(handle.scale) * ground_scale
    bpy.context.view_layer.update()
    low = min(p.z for p in bounds(objects))
    centre = sum(bounds(objects), Vector()) / len(bounds(objects))
    handle.location = (handle.location.x - centre.x, handle.location.y - centre.y, handle.location.z - low)
    studio.render(scene, str(out / f"ground_{index:02d}.png"))
    ax, ay = studio.anchor(scene)
    return ax, ay


def render_placed(out):
    """The laid mine, just laid and armed, lying where the game's camera
    sees it; returns the anchor of each."""
    anchors = []
    for k, kind in enumerate(("mine", "mine_armed")):
        scene = studio.reset(samples=64)
        handle = bpy.data.objects.new("item", None)
        bpy.context.scene.collection.objects.link(handle)
        for o in thing(kind):
            o.parent = handle
        handle.scale = (MINE_PLACED_SCALE,) * 3
        handle.rotation_euler = (0, 0, math.radians(15))
        studio.camera(scene, 128, 128)
        studio.render(scene, str(out / f"placed_{k}.png"))
        anchors.append(studio.anchor(scene))
    return anchors


def existing_frames(folder, name):
    """The frames of a pack sheet already written, to keep those not
    rendered again."""
    from PIL import Image

    sheet = folder / f"{name}.sheet"
    if not sheet.exists():
        return []
    image = Image.open(folder / f"{name}.png").convert("RGBA")
    frames = []
    for line in sheet.read_text().splitlines():
        if line.startswith("frame="):
            x, y, w, h, ax, ay = map(int, line[6:].split(","))
            frames.append((image.crop((x, y, x + w, y + h)), ax, ay))
    return frames


def write_strip(frames, folder, name):
    """Lays frames out left to right and writes <name>.png and <name>.sheet."""
    from PIL import Image

    width = sum(im.width for im, _, _ in frames)
    height = max(im.height for im, _, _ in frames)
    out = Image.new("RGBA", (width, height))
    lines = [HEADER, f"image={name}.png\n"]
    x = 0
    for im, ax, ay in frames:
        out.paste(im, (x, 0))
        lines.append(f"frame={x},0,{im.width},{im.height},{ax},{ay}\n")
        x += im.width
    folder.mkdir(parents=True, exist_ok=True)
    out.save(folder / f"{name}.png", optimize=True)
    with open(folder / f"{name}.sheet", "w", newline="\n") as f:
        f.writelines(lines)


def main():
    args = sys.argv[1:]
    if args[0] == "placed":
        from PIL import Image

        pack = Path(args[1])
        renders = pack / "placed-renders"
        renders.mkdir(parents=True, exist_ok=True)
        anchors = render_placed(renders)
        frames = []
        for k, (ax, ay) in enumerate(anchors):
            im = Image.open(renders / f"placed_{k}.png").convert("RGBA")
            box = im.getbbox() or (0, 0, 1, 1)
            frames.append((im.crop(box), round(ax) - box[0], round(ay) - box[1]))
        for f in renders.iterdir():
            f.unlink()
        renders.rmdir()
        write_strip(frames, pack / "Character/OPTION/00001000", "Pattern")
        print("wrote the laid mine")
        return
    if args[0] == "render":
        only = list(range(len(ITEMS)))
        if "--only" in args:
            k = args.index("--only")
            only = [int(x) for x in args[k + 1].split(",")]
            del args[k:k + 2]
        renders = Path(args[1])
        renders.mkdir(parents=True, exist_ok=True)
        for index in only:
            ax, ay = render_item(index, renders)
            (renders / f"anchor_{index:02d}.txt").write_text(f"{ax:.0f} {ay:.0f}\n")
            print(ITEMS[index][0], "done", flush=True)
        return
    from PIL import Image

    renders, pack = Path(args[1]), Path(args[2])
    old_inventory = existing_frames(pack / "System/Game/Pattern", "Item0000")
    old_ground = existing_frames(pack / "Character/ITEM/00000007", "Pattern")
    inventory, ground = [], []
    for index in range(len(ITEMS)):
        if not (renders / f"inv_{index:02d}.png").exists():
            # Not rendered this time: keep the pack's own.
            inventory.append(old_inventory[index])
            ground.append(old_ground[index])
            continue
        inventory.append((Image.open(renders / f"inv_{index:02d}.png").convert("RGBA"), 0, 0))
        im = Image.open(renders / f"ground_{index:02d}.png").convert("RGBA")
        box = im.getbbox() or (0, 0, 1, 1)
        ax, ay = map(int, (renders / f"anchor_{index:02d}.txt").read_text().split())
        ground.append((im.crop(box), ax - box[0], ay - box[1]))
    write_strip(inventory, pack / "System/Game/Pattern", "Item0000")
    write_strip(ground, pack / "Character/ITEM/00000007", "Pattern")
    print(f"wrote {len(ITEMS)} item pictures")


if __name__ == "__main__":
    main()
