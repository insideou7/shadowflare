"""Scenario objects in ShadowFlare's style, rendered with studio.py: the
chests, crates, dungeon doors, floor buttons, levers and pads a scenario
places (Character/OBJECT/<kind>/Pattern.njp in the original), each in the
states the game switches between (closed and open, up and down), and the
enemy holes (goblin burrows that bring nearby monsters back until the hero
breaks them: hole, hole_open while one comes back, hole_broken).

    python tools/art/objects.py [--blend] <output folder> [piece ...]

(run with a Python that has the `bpy` module). Writes one PNG per piece plus
objects.txt listing each file's anchor point. A scenario object is drawn
with its anchor on the object's own position, so the anchor written here is
not the model's origin: it is moved so the model's outline (without its
shadow) sits where the original picture's box sits about its anchor, the
middle of its bottom edge on the middle of the original's. The boxes below
are the original pictures' sizes and offsets (x, y, width, height) only.
Props without a box (the holes) are anchored on the model's origin, the
middle of the object's rectangle.
"""

import math
import random
import sys
from pathlib import Path

import bpy
from bpy_extras.object_utils import world_to_camera_view

sys.path.insert(0, str(Path(__file__).resolve().parent))
import studio  # noqa: E402
from dungeon import BLOCKS, CAP, DARK, solid, turn  # noqa: E402
from fort import BLACKIRON, DOORIRON, RIVET  # noqa: E402
from town import IRON, _mats, ball, box, cyl, mat  # noqa: E402

GILT = dict(colour=(0.74, 0.60, 0.28), dark=(0.24, 0.18, 0.06), scale=22, rough=0.3, bump=0.5)
CHESTWOOD = dict(colour=(0.30, 0.17, 0.08), dark=(0.10, 0.05, 0.02), scale=10, bump=0.5, stretch=(1, 6, 1))
SILVER = dict(colour=(0.86, 0.86, 0.84), dark=(0.48, 0.48, 0.50), scale=18, rough=0.25, bump=0.3)
LINING = dict(colour=(0.80, 0.24, 0.28), dark=(0.36, 0.07, 0.10), scale=14, bump=0.3)
STAVES = dict(colour=(0.42, 0.40, 0.38), dark=(0.14, 0.13, 0.12), scale=6, stretch=(1, 1, 8), bump=0.5)
PLANKS = dict(colour=(0.50, 0.34, 0.16), dark=(0.20, 0.12, 0.05), scale=7, stretch=(8, 1, 1), bump=0.6, grime=0.3)
DOORWOOD = dict(colour=(0.30, 0.13, 0.07), dark=(0.08, 0.03, 0.02), scale=9, stretch=(1, 1, 6), bump=0.6, grime=0.3)
REDSTONE = dict(colour=(0.86, 0.18, 0.14), dark=(0.42, 0.05, 0.04), scale=12, rough=0.3, bump=0.4)
DULLRED = dict(colour=(0.30, 0.10, 0.08), dark=(0.10, 0.03, 0.02), scale=12, rough=0.6, bump=0.4)
FRAME = dict(colour=(0.40, 0.26, 0.15), dark=(0.14, 0.08, 0.04), scale=14, bump=0.5)
SEALIRON = dict(colour=(0.06, 0.05, 0.05), dark=(0.01, 0.01, 0.01), scale=20, rough=0.5, bump=0.5)
STEEL = dict(colour=(0.70, 0.70, 0.72), dark=(0.20, 0.20, 0.22), scale=24, rough=0.3, bump=0.4)


def glow(name, colour, strength=1.5):
    """A glassy material that shines with its own colour."""
    m = studio.material(name, colour, tuple(c * 0.35 for c in colour), scale=6, rough=0.15)
    p = m.node_tree.nodes["Principled BSDF"]
    p.inputs["Emission Strength"].default_value = strength
    m.node_tree.links.new(p.inputs["Base Color"].links[0].from_socket, p.inputs["Emission Color"])
    return m


def hinged(size, hinge, rel, angle, m, axis="x"):
    """A box `size` whose centre is `rel` from a hinge line through `hinge`,
    turned `angle` about it (axis "x": the line runs along X and rel is
    (y, z); "z": the line is upright and rel is (x, y))."""
    a, b = rel
    c, s = math.cos(angle), math.sin(angle)
    if axis == "x":
        at = (hinge[0], hinge[1] + a * c - b * s, hinge[2] + a * s + b * c)
        return box(size, at, m, rot=(angle, 0, 0))
    at = (hinge[0] + a * c - b * s, hinge[1] + a * s + b * c, hinge[2])
    return box(size, at, m, rot=(0, 0, angle))


# Chests and crates are built long along Blender X (the game's x), their
# fronts toward the camera (Blender -Y), and turned for the y pieces.

def chest(open_=False, silver=False, along_y=False):
    """A gilt chest bound in dark wood with studded corners (silver: a
    pale silver one with gilt bands); open, the lid stands back and the
    red lining shows."""
    if silver:
        body, band = mat("silver", SILVER), mat("gilt", GILT)
        length, depth, height, lid_t = 2.2, 1.15, 0.62, 0.32
    else:
        body, band = mat("gilt", GILT), mat("blackiron", BLACKIRON)
        length, depth, height, lid_t = 2.2, 1.0, 0.78, 0.40
    stud, lining = mat("rivet", RIVET), mat("lining", LINING)
    if open_:
        # Hollow, so the lining shows.
        t = 0.1
        box((length, depth, 0.2), (0, 0, 0.1), body)
        for y in (-1, 1):
            box((length, t, height), (0, y * (depth / 2 - t / 2), height / 2), body)
        for x in (-1, 1):
            box((t, depth, height), (x * (length / 2 - t / 2), 0, height / 2), body)
    else:
        box((length, depth, height), (0, 0, height / 2), body)
    # Corner posts and a band round the foot.
    for x in (-1, 1):
        for y in (-1, 1):
            box((0.16, 0.16, height + 0.02), (x * (length / 2 - 0.06), y * (depth / 2 - 0.06), height / 2), band)
    box((length + 0.04, depth + 0.04, 0.12), (0, 0, 0.06), band)
    for x in (-0.55, 0.55):
        box((0.12, depth + 0.03, height), (x * length / 2 * 1.4, 0, height / 2), band)
    for x in (-length / 2 + 0.25, length / 2 - 0.25):
        for z in (0.25, height - 0.15):
            ball(0.04, (x, -depth / 2 - 0.02, z), stud)
    hinge = (0, depth / 2, height)
    if open_:
        box((length - 0.2, depth - 0.2, 0.04), (0, 0, 0.22), lining)
        for x in (-1, 1):
            box((0.03, depth - 0.2, height - 0.25), (x * (length / 2 - 0.115), 0, 0.2 + (height - 0.25) / 2), lining)
        for y in (-1, 1):
            box((length - 0.2, 0.03, height - 0.25), (0, y * (depth / 2 - 0.115), 0.2 + (height - 0.25) / 2), lining)
        angle = math.radians(-108)
    else:
        angle = 0.0
        box((0.22, 0.06, 0.3), (0, -depth / 2 - 0.03, height - 0.12), band)  # the hasp
        ball(0.07, (0, -depth / 2 - 0.07, height - 0.2), stud)
    hinged((length + 0.04, depth + 0.04, lid_t * 0.55), hinge, (-depth / 2, lid_t * 0.27), angle, body)
    hinged((length - 0.3, depth - 0.3, lid_t * 0.45), hinge, (-depth / 2, lid_t * 0.77), angle, body)
    hinged((length + 0.06, 0.12, lid_t * 0.6), hinge, (-depth + 0.06, lid_t * 0.3), angle, band)
    for x in (-0.55, 0.55):
        hinged((0.12, depth + 0.06, lid_t + 0.02), hinge, (-depth / 2, lid_t / 2), angle, band) \
            .location.x = x * length / 2 * 1.4
    if open_:
        hinged((length - 0.2, 0.03, lid_t * 0.8), hinge, (-0.04, lid_t * 0.45), angle, lining)
    if along_y:
        turn(math.pi / 2)


def keg(open_=False):
    """A grey keg lying on its side with iron hoops; open, its end is off and
    lies beside it."""
    staves, iron = mat("staves", STAVES), mat("iron", IRON)
    r, length = 0.52, 1.4
    side = (0, math.pi / 2, 0)
    cyl(r, length, (0, 0, r), staves, rot=side, verts=20)
    for x in (-0.58, -0.2, 0.2, 0.58):
        cyl(r + 0.03, 0.08, (x, 0, r), iron, rot=side, verts=20)
    if open_:
        cyl(r - 0.06, 0.04, (length / 2 + 0.01, 0, r), mat("hole", DARK), rot=side, verts=20)
        cyl(r - 0.04, 0.08, (length / 2 + 0.5, -0.6, 0.04), staves, verts=20)
    else:
        cyl(r - 0.04, 0.04, (length / 2 + 0.01, 0, r), staves, rot=side, verts=20)


def crate(open_=False, along_y=False):
    """A long plank crate on runners; open, its lid boards are off and two
    of them lean on its side."""
    wood, timber = mat("planks", PLANKS), mat("chestwood", CHESTWOOD)
    length, depth, height = 2.4, 0.95, 0.82
    if open_:
        t = 0.08
        box((length, depth, 0.1), (0, 0, 0.05), wood)
        for y in (-1, 1):
            box((length, t, height), (0, y * (depth / 2 - t / 2), height / 2), wood)
        for x in (-1, 1):
            box((t, depth, height), (x * (length / 2 - t / 2), 0, height / 2), wood)
        box((length - 0.2, depth - 0.2, 0.05), (0, 0, 0.12), mat("hole", DARK))
        for k in range(2):
            box((length * 0.9, 0.3, 0.05), (0.1 * k, -depth / 2 - 0.3 - 0.32 * k, 0.025 + 0.05 * k), wood,
                rot=(0, 0, 0.04 * (k - 0.5)))
    else:
        box((length, depth, height), (0, 0, height / 2), wood)
        for k in range(1, 4):
            box((length + 0.01, 0.02, 0.02), (0, -depth / 2 - 0.005, height * k / 4), timber)
    for x in (-1, 1) if open_ else (-1, 0, 1):
        box((0.12, depth + 0.06, height + 0.04), (x * (length / 2 - 0.06), 0, height / 2), timber)
    if along_y:
        turn(math.pi / 2)


# A dungeon door: two block posts with a double door of studded boards
# between them, open above like the original's (sealed: black iron with a red gem). The game swaps the
# closed piece for its two open halves, each a post with its leaf swung
# back, so all three are built in the same place.
DOOR_L, DOOR_H, DOOR_T, POST = 5.0, 5.6, 0.85, 0.95


def door(along_y=False, sealed=False, part="closed"):
    blocks, cap = mat("blocks", BLOCKS), mat("cap", CAP)
    stud = mat("rivet", RIVET)
    leafm = mat("sealiron", SEALIRON) if sealed else mat("doorwood", DOORWOOD)
    band = mat("dooriron", DOORIRON)
    sides = {"closed": (-1, 1), "left": (-1,), "right": (1,)}[part]
    for side in sides:
        x = side * (DOOR_L / 2 - POST / 2)
        solid(box((POST, DOOR_T + 0.15, DOOR_H), (x, 0, DOOR_H / 2), blocks))
        box((POST + 0.15, DOOR_T + 0.3, 0.3), (x, 0, DOOR_H + 0.1), cap)
        box((POST + 0.15, DOOR_T + 0.3, 0.25), (x, 0, 0.12), cap)
    gap = DOOR_L - 2 * POST
    w, h, t = gap / 2 - 0.03, DOOR_H - 0.5, 0.22
    for side in sides:
        # Each leaf turns on the inside of its post, swinging back (+Y).
        hinge = (side * gap / 2, t / 2, 0)
        angle = 0.0 if part == "closed" else side * math.radians(-80)

        def leaf(size, x, y, z, m):
            # x: across the leaf from its hinge; y: off its face.
            o = hinged(size, hinge, (-side * x, y - t / 2), angle, m, axis="z")
            o.location.z = z
            return o

        leaf((w, t, h), w / 2, 0, h / 2, leafm)
        for z in (0.6, h / 2, h - 0.6):
            leaf((w, t + 0.06, 0.18), w / 2, 0, z, band)
        leaf((0.16, t + 0.06, h), w - 0.1, 0, h / 2, band)
        if sealed:
            o = leaf((0.45, 0.45, 0.45), w - 0.12, -t / 2 - 0.15, h * 0.62, glow("gem", (0.95, 0.08, 0.10), 1.5))
            bpy.ops.object.select_all(action="DESELECT")
            o.select_set(True)
            bpy.context.view_layer.objects.active = o
            bpy.ops.object.modifier_add(type="SUBSURF")
            o.modifiers[-1].levels = o.modifiers[-1].render_levels = 2
            leaf((0.06, 0.06, h * 0.7), w * 0.5, -t / 2 - 0.03, h * 0.45, band)
        else:
            for row in range(7):
                for col in range(2):
                    leaf((0.09, 0.09, 0.09), 0.3 + col * (w - 0.6), -t / 2 - 0.04, 0.4 + row * (h - 0.8) / 6, stud)
    if along_y:
        turn(math.pi / 2)


def button(down=False):
    """A square red stone set in a dark frame in the floor, raised until it
    is stepped on."""
    box((1.1, 1.1, 0.1), (0, 0, 0.05), mat("frame", FRAME))
    if down:
        box((0.86, 0.86, 0.06), (0, 0, 0.1), mat("dullred", DULLRED))
    else:
        box((0.86, 0.86, 0.14), (0, 0, 0.14), mat("redstone", REDSTONE))
        box((0.4, 0.4, 0.03), (0, 0, 0.225), glow("gem", (1.0, 0.25, 0.2), 0.6))


def lever(down=False, along_y=False):
    """A steel lever in a squat iron box, its red-knobbed handle thrown
    back (up) or forward (down)."""
    iron, steel = mat("blackiron", BLACKIRON), mat("steel", STEEL)
    knob = glow("knob", (0.85, 0.10, 0.08), 0.3)
    box((1.3, 0.9, 0.45), (0, 0, 0.22), mat("iron", IRON))
    box((1.36, 0.96, 0.1), (0, 0, 0.05), iron)
    box((0.9, 0.2, 0.12), (0, 0, 0.5), iron)  # the slot
    angle = math.radians(60 if down else -30)
    length = 1.5
    tip = (math.sin(angle) * length, 0, 0.5 + math.cos(angle) * length)
    rod = cyl(0.06, length, (tip[0] / 2, 0, (0.5 + tip[2]) / 2), steel, verts=8)
    rod.rotation_euler = (0, angle, 0)
    cyl(0.11, 0.3, tip, knob, rot=(0, angle, 0), verts=12)
    if not along_y:
        turn(-math.pi / 2)


def pad(colour):
    """A round glowing pad in a gilt ring."""
    gilt, stone = mat("gilt", GILT), mat("cap", CAP)
    cyl(1.4, 0.12, (0, 0, 0.06), stone, verts=40)
    cyl(1.25, 0.1, (0, 0, 0.14), gilt, verts=40)
    cyl(1.05, 0.06, (0, 0, 0.19), stone, verts=40)
    cyl(0.85, 0.06, (0, 0, 0.22), glow("pad", colour, 1.2), verts=40)
    for k in range(8):
        a = math.pi * k / 4
        ball(0.06, (1.15 * math.cos(a), 1.15 * math.sin(a), 0.2), mat("rivet", RIVET))


EARTH = dict(colour=(0.42, 0.31, 0.20), dark=(0.17, 0.11, 0.07), scale=9, bump=0.7, grime=0.3)
PIT = dict(colour=(0.10, 0.07, 0.05), dark=(0.02, 0.015, 0.01), scale=10, bump=0.5)
ROCK = dict(colour=(0.52, 0.49, 0.44), dark=(0.20, 0.18, 0.16), scale=7, bump=0.7, grime=0.25)
STAKE = dict(colour=(0.46, 0.33, 0.19), dark=(0.18, 0.11, 0.05), scale=8, stretch=(1, 1, 8), bump=0.6, grime=0.3)
HOLE_R = 1.25  # the opening; the ring of stones round it makes the burrow about 450 world units across


def rock(r, at, m, rng, squash=0.6):
    """A rough, flat-faced stone."""
    bpy.ops.mesh.primitive_ico_sphere_add(subdivisions=1, radius=r, location=at)
    o = bpy.context.object
    for v in o.data.vertices:
        v.co *= rng.uniform(0.75, 1.15)
    o.scale = (rng.uniform(0.9, 1.3), rng.uniform(0.8, 1.1), squash)
    o.rotation_euler = (rng.uniform(-0.3, 0.3), rng.uniform(-0.3, 0.3), rng.uniform(0, math.pi))
    return studio.assign(o, m)


def stake(at, lean, height, m, broken=False):
    """A sharpened wooden stake driven into the ground, leaning outward
    (lean: the direction in radians it leans towards)."""
    from mathutils import Vector

    tilt = math.radians(20)
    up = Vector((math.sin(tilt) * math.cos(lean), math.sin(tilt) * math.sin(lean), math.cos(tilt)))
    rot = up.to_track_quat("Z", "Y").to_euler()
    h = height * (0.45 if broken else 1)
    base = Vector((at[0], at[1], 0))
    cyl(0.09, h, base + up * (h / 2), m, rot=rot, verts=7)
    if not broken:
        cyl(0.09, 0.25, base + up * (h + 0.125), m, rot=rot, verts=7, r2=0.0)


def hole(state="shut"):
    """A goblin burrow: a dark pit in a low ring of dug earth, rough stones
    piled round its lip and a few sharpened stakes; open, it glows red-orange
    from deep inside; broken, it has caved in under a heap of earth and
    tumbled stones."""
    rng = random.Random(7)
    earth, pit, stone, wood = mat("earth", EARTH), mat("pit", PIT), mat("rock", ROCK), mat("stake", STAKE)
    broken = state == "broken"
    # The dug-up earth round the hole: a low, lumpy ring.
    bpy.ops.mesh.primitive_torus_add(major_radius=HOLE_R + 0.35, minor_radius=0.5, major_segments=28,
                                     minor_segments=10, location=(0, 0, 0))
    ring = bpy.context.object
    ring.scale = (1, 1, 0.55)
    for v in ring.data.vertices:
        v.co.z *= rng.uniform(0.8, 1.25)
    studio.smooth(studio.assign(ring, earth))
    if broken:
        # Caved in: a heap of loose earth fills the opening.
        ball(HOLE_R + 0.1, (0, 0, 0.02), earth, scale=(1, 0.95, 0.22))
        for k in range(7):
            a = rng.uniform(0, 2 * math.pi)
            d = rng.uniform(0, 0.8)
            rock(rng.uniform(0.18, 0.3), (d * math.cos(a), d * math.sin(a), 0.22), stone, rng)
    else:
        # The throat of the burrow, sloping down into the dark.
        bpy.ops.mesh.primitive_cone_add(vertices=28, radius1=HOLE_R * 0.55, radius2=HOLE_R + 0.15, depth=0.24,
                                        location=(0, 0, 0.13), end_fill_type="NOTHING")
        studio.smooth(studio.assign(bpy.context.object, pit))
        if state == "open":
            core = glow("burrow fire", (1.0, 0.36, 0.08), 6.0)
            cyl(HOLE_R * 0.6, 0.02, (0, 0, 0.02), core, verts=28)
            cyl(HOLE_R * 0.95, 0.02, (0, 0, 0.012), glow("burrow glow", (0.85, 0.20, 0.05), 1.5), verts=28)
            light = bpy.data.lights.new("burrow light", "POINT")
            light.energy = 220
            light.color = (1.0, 0.45, 0.15)
            light.shadow_soft_size = 0.5
            lo = bpy.data.objects.new("burrow light", light)
            bpy.context.scene.collection.objects.link(lo)
            lo.location = (0, 0, 0.12)
        else:
            cyl(HOLE_R * 0.7, 0.02, (0, 0, 0.02), mat("deep", dict(colour=(0.02, 0.015, 0.01), scale=4, bump=0)),
                verts=28)
    # Stones piled round the lip.
    n = 16
    for k in range(n):
        a = 2 * math.pi * k / n + rng.uniform(-0.12, 0.12)
        d = HOLE_R + 0.35 + rng.uniform(-0.12, 0.2)
        if broken and k % 3 == 0:
            d -= 0.5  # tumbled in
        rock(rng.uniform(0.2, 0.32), (d * math.cos(a), d * math.sin(a), 0.22 + rng.uniform(0, 0.08)), stone, rng)
    # Sharpened stakes round the back and sides, none in front of the hole.
    for k, a in enumerate((45, 95, 140, 185, 230)):  # the camera looks from 315 degrees
        a = math.radians(a)
        d = HOLE_R + 0.85
        stake((d * math.cos(a), d * math.sin(a)), a, rng.uniform(0.85, 1.15), wood, broken=broken and k % 2 == 0)
    if broken:
        # A snapped stake lying across the heap.
        cyl(0.06, 1.0, (0.2, -0.3, 0.32), wood, rot=(math.radians(84), 0, math.radians(30)), verts=7)


# name: (builder, image size, the original picture's box or None, wall pitch)
PROPS = {
    "obj_chest_x": (chest, 96, (-23, -24, 49, 52), False),
    "obj_chest_open_x": (lambda: chest(open_=True), 112, (-23, -38, 59, 66), False),
    "obj_chest_y": (lambda: chest(along_y=True), 96, (-25, -25, 50, 52), False),
    "obj_chest_open_y": (lambda: chest(open_=True, along_y=True), 112, (-34, -38, 59, 65), False),
    "obj_silver_x": (lambda: chest(silver=True), 96, (-25, -22, 51, 50), False),
    "obj_silver_open_x": (lambda: chest(open_=True, silver=True), 112, (-39, -22, 65, 54), False),
    "obj_keg": (keg, 80, (-20, -16, 39, 34), False),
    "obj_keg_open": (lambda: keg(open_=True), 96, (-23, -20, 47, 40), False),
    "obj_crate_y": (lambda: crate(along_y=True), 96, (-27, -25, 53, 48), False),
    "obj_crate_open_y": (lambda: crate(open_=True, along_y=True), 112, (-27, -25, 53, 56), False),
    "obj_crate_x": (crate, 96, (-27, -23, 53, 48), False),
    "obj_crate_open_x": (lambda: crate(open_=True), 112, (-39, -23, 65, 48), False),
    "obj_door_x": (door, 224, (-47, -118, 95, 156), True),
    "obj_door_left_x": (lambda: door(part="left"), 224, (-47, -121, 51, 111), True),
    "obj_door_right_x": (lambda: door(part="right"), 224, (21, -87, 32, 125), True),
    "obj_door_y": (lambda: door(along_y=True), 224, (-47, -118, 96, 156), True),
    "obj_door_left_y": (lambda: door(along_y=True, part="left"), 224, (-51, -86, 31, 124), True),
    "obj_door_right_y": (lambda: door(along_y=True, part="right"), 224, (-4, -121, 53, 111), True),
    "obj_sealed_x": (lambda: door(sealed=True), 224, (-47, -118, 95, 156), True),
    "obj_sealed_left_x": (lambda: door(sealed=True, part="left"), 224, (-47, -118, 38, 111), True),
    "obj_sealed_right_x": (lambda: door(sealed=True, part="right"), 224, (15, -76, 33, 114), True),
    "obj_sealed_y": (lambda: door(along_y=True, sealed=True), 224, (-47, -118, 96, 156), True),
    "obj_sealed_left_y": (lambda: door(along_y=True, sealed=True, part="left"), 224, (-47, -76, 34, 114), True),
    "obj_sealed_right_y": (lambda: door(along_y=True, sealed=True, part="right"), 224, (11, -118, 38, 111), True),
    "obj_button": (button, 64, (-16, -10, 33, 24), False),
    "obj_button_down": (lambda: button(down=True), 64, (-16, -10, 33, 24), False),
    "obj_lever_x": (lever, 96, (-16, -34, 36, 50), False),
    "obj_lever_down_x": (lambda: lever(down=True), 96, (-19, -13, 36, 29), False),
    "obj_lever_y": (lambda: lever(along_y=True), 96, (-19, -35, 36, 50), False),
    "obj_lever_down_y": (lambda: lever(down=True, along_y=True), 96, (-16, -14, 36, 29), False),
    "obj_pad_blue": (lambda: pad((0.25, 0.55, 1.0)), 96, (-29, -41, 63, 46), False),
    "obj_pad_red": (lambda: pad((1.0, 0.18, 0.22)), 96, (-29, -41, 63, 46), False),
    "hole": (hole, 160, None, False),
    "hole_open": (lambda: hole("open"), 160, None, False),
    "hole_broken": (lambda: hole("broken"), 160, None, False),
}


def outline(scene):
    """The model's box in the image, in pixels, from its vertices."""
    bpy.context.view_layer.update()
    xs, ys = [], []
    w, h = scene.render.resolution_x, scene.render.resolution_y
    deps = bpy.context.evaluated_depsgraph_get()
    for o in scene.objects:
        if o.type != "MESH" or o.name == "shadow catcher":
            continue
        ev = o.evaluated_get(deps)
        for v in ev.data.vertices:
            co = world_to_camera_view(scene, scene.camera, ev.matrix_world @ v.co)
            xs.append(co.x * w)
            ys.append((1 - co.y) * h)
    return min(xs), min(ys), max(xs), max(ys)


def main():
    args = [a for a in sys.argv[sys.argv.index("--") + 1:]] if "--" in sys.argv else sys.argv[1:]
    blend = "--blend" in args
    args = [a for a in args if a != "--blend"]
    out = Path(args[0])
    names = args[1:] or list(PROPS)
    out.mkdir(parents=True, exist_ok=True)
    listing = out / "objects.txt"
    anchors = dict(l.split(" ", 1) for l in listing.read_text().split("\n") if l) if listing.exists() else {}
    for name in names:
        build, size, original, wall = PROPS[name]
        _mats.clear()
        scene = studio.reset(samples=32)
        build()
        if wall:  # the original's doors, like its walls, cast no shadows
            bpy.data.objects["shadow catcher"].hide_render = True
        studio.camera(scene, size, size, target=(0, 0, size / studio.PIXELS_PER_UNIT * 0.3 if original else 0.4),
                      elevation=studio.WALL_ELEVATION if wall else None)
        left, top, right, bottom = outline(scene)
        if original:
            # The image point the object's position goes on: the outline's
            # bottom middle where the original's is.
            x0, y0, w, h = original
            ax, ay = (left + right) / 2 - (x0 + w / 2), bottom - (y0 + h)
        else:
            ax, ay = studio.anchor(scene)
            w, h = "-", "-"
        if blend:
            studio.save_blend(out / f"{name}.blend")
        studio.render(scene, str(out / f"{name}.png"))
        anchors[name] = f"{ax:.0f} {ay:.0f}"
        print(f"{name} done: {right - left:.0f}x{bottom - top:.0f} (original {w}x{h})", flush=True)
    listing.write_text("".join(f"{k} {v}\n" for k, v in anchors.items()))


if __name__ == "__main__":
    main()
