"""Castle pieces in ShadowFlare's style, rendered with studio.py: dark
ashlar walls under rounded blue-steel copings, arched gateways, buttressed
towers with flared steel caps, carved pillars, statues on plinths, little
roofed shrines, a marble hall with a fire inside, pews and a magic circle.

    python tools/art/castle.py [--blend] <output folder> [piece ...]

(run with a Python that has the `bpy` module). Writes one PNG per piece plus
castle.txt listing each file's anchor point, like deep.py. Walls are two
tiles long (6.4 Blender units) and about 4.6 units high.
"""

import math
import random
import sys
from pathlib import Path

import bpy

sys.path.insert(0, str(Path(__file__).resolve().parent))
import studio  # noqa: E402
from dungeon import solid, turn  # noqa: E402
from town import _mats, ball, box, cyl, mat  # noqa: E402

ASHLAR = dict(colour=(0.22, 0.21, 0.20), dark=(0.04, 0.04, 0.04), scale=10, detail=12, bump=1.0, grime=0.6,
              bricks=dict(width=1.1, height=0.55, mortar=0.035, vary=0.6, joint=0.25))
CARVED = dict(colour=(0.26, 0.25, 0.23), dark=(0.06, 0.06, 0.06), scale=12, bump=0.9, grime=0.5)
STEEL = dict(colour=(0.30, 0.33, 0.42), dark=(0.06, 0.07, 0.10), scale=14, rough=0.28, bump=0.3)
MARBLE = dict(colour=(0.78, 0.76, 0.71), dark=(0.42, 0.40, 0.36), scale=8, bump=0.4, grime=0.4)
STATUE = dict(colour=(0.40, 0.40, 0.39), dark=(0.10, 0.10, 0.10), scale=10, bump=0.6, grime=0.6)
ROOF = dict(colour=(0.12, 0.12, 0.13), dark=(0.03, 0.03, 0.04), scale=16, rough=0.6, bump=0.5)
WOOD = dict(colour=(0.36, 0.14, 0.08), dark=(0.10, 0.04, 0.02), scale=10, bump=0.5, stretch=(6, 1, 1))
GOLD = dict(colour=(0.85, 0.62, 0.22), dark=(0.36, 0.22, 0.06), scale=12, rough=0.3, bump=0.3)
PALE = dict(colour=(0.40, 0.39, 0.36), dark=(0.10, 0.10, 0.09), scale=12, bump=0.9, grime=0.6)
# Smooth dark dressed stone, its joints too fine to see, for plain walls.
DRESSED = dict(colour=(0.20, 0.19, 0.18), dark=(0.05, 0.05, 0.05), scale=6, detail=10, bump=0.5, grime=0.7,
               band=(0.3, 0.8))
EARTHTOP = dict(colour=(0.24, 0.17, 0.10), dark=(0.07, 0.05, 0.03), scale=18, rough=1.0, bump=1.0)
BLACK = dict(colour=(0.015, 0.015, 0.02), dark=(0.005, 0.005, 0.01), scale=4, bump=0)

WALL_L, WALL_T, WALL_H = 6.4, 0.9, 4.6


def glow(name, colour, dark, strength, **kw):
    """A noisy material that lights itself."""
    m = studio.material(name, colour, dark, **kw)
    p = m.node_tree.nodes["Principled BSDF"]
    p.inputs["Emission Strength"].default_value = strength
    m.node_tree.links.new(p.inputs["Base Color"].links[0].from_socket, p.inputs["Emission Color"])
    return m


def shrink(f):
    """Scale everything built so far about the origin."""
    for o in bpy.context.scene.objects:
        if o.type == "MESH" and not o.is_shadow_catcher and o.parent is None:
            o.location = (o.location.x * f, o.location.y * f, o.location.z * f)
            o.scale = (o.scale.x * f, o.scale.y * f, o.scale.z * f)


def cut(o, cutter):
    """Carve one shape out of another and throw the cutter away."""
    mod = o.modifiers.new("cut", "BOOLEAN")
    mod.operation, mod.object = "DIFFERENCE", cutter
    bpy.ops.object.select_all(action="DESELECT")
    o.select_set(True)
    bpy.context.view_layer.objects.active = o
    bpy.ops.object.modifier_apply(modifier="cut")
    bpy.data.objects.remove(cutter)
    return o


def coping(length, z, y=0.0, roll=True):
    """Packed earth along the top of a wall, edged in front by a rounded
    blue-steel roll (roll=False: earth only)."""
    steel = mat("steel", STEEL)
    box((length, WALL_T, 0.2), (0, y, z), mat("earthtop", EARTHTOP))
    if roll:
        cyl(0.28, length + 0.1, (0, y - WALL_T / 2, z), steel, rot=(0, math.pi / 2, 0), verts=20)


def castle_wall(along_y=False, seed=1):
    """A plain wall of smooth dark stone with packed earth on top, like
    the original's town walls."""
    solid(box((WALL_L, WALL_T, WALL_H), (0, 0, WALL_H / 2), mat("dressed", DRESSED)))
    coping(WALL_L, WALL_H, roll=False)
    if along_y:
        turn(math.pi / 2)


def arcade(along_y=False):
    """The canal side of the town's thick walls: dark ashlar with two tall
    pointed arches barred with steel grilles, a rose window between their
    heads, a row of little corbels and a steel coping on the front edge."""
    stone = mat("arcade", dict(ASHLAR, colour=(0.34, 0.33, 0.31)))
    carved, steel = mat("moulding", dict(PALE, colour=(0.58, 0.56, 0.52))), mat("steel", STEEL)
    black = mat("black", BLACK)
    L, H, T = 5.6, WALL_H, 1.6  # the original sets these bays 5.5 units apart
    face = -T / 2
    w = solid(box((L, T, H), (0, 0, H / 2), stone))
    aw, spring = 1.8, 2.5  # arch width, height where the pointed head starts
    for x0 in (-1.35, 1.35):
        cut(w, solid(box((aw, 0.8, spring), (x0, face, spring / 2), stone)))
        head = solid(box((aw / math.sqrt(2), 0.8, aw / math.sqrt(2)), (x0, face, spring), stone))
        head.rotation_euler = (0, math.pi / 4, 0)
        cut(w, head)
        box((aw, 0.05, spring + aw / 2), (x0, face + 0.38, (spring + aw / 2) / 2), black)
        # The grille: bars both ways.
        for k in range(5):
            box((0.07, 0.07, spring + 0.5), (x0 - aw / 2 + 0.15 + k * (aw - 0.3) / 4, face + 0.2,
                                            (spring + 0.5) / 2), steel)
        for z in (0.6, 1.25, 1.9, 2.55):
            box((aw, 0.07, 0.07), (x0, face + 0.2, z), steel)
        # A carved moulding round the arch.
        for sx in (-1, 1):
            box((0.16, 0.2, spring), (x0 + sx * (aw / 2 + 0.08), face - 0.05, spring / 2), carved)
            box((0.16, 0.2, aw * 0.75), (x0 + sx * aw / 4, face - 0.05, spring + aw / 4), carved,
                rot=(0, -sx * math.pi / 4, 0))
    # The rose window between the arch heads.
    rx, rz = 0.0, 3.55
    cyl(0.45, 0.1, (rx, face - 0.01, rz), black, rot=(math.pi / 2, 0, 0), verts=24)
    bpy.ops.mesh.primitive_torus_add(major_radius=0.5, minor_radius=0.1, location=(rx, face - 0.05, rz),
                                     rotation=(math.pi / 2, 0, 0))
    studio.assign(bpy.context.object, carved)
    for k in range(4):
        box((0.9, 0.06, 0.06), (rx, face - 0.04, rz), steel, rot=(0, k * math.pi / 4, 0))
    # Corbels under the coping, and the coping.
    for k in range(7):
        box((0.18, 0.2, 0.25), (-L / 2 + 0.35 + k * (L - 0.7) / 6, face - 0.1, H - 0.45), carved)
    box((L, T, 0.2), (0, 0, H + 0.1), mat("earthtop", EARTHTOP))
    cyl(0.32, L + 0.1, (0, face, H + 0.1), steel, rot=(0, math.pi / 2, 0), verts=20)
    if along_y:
        turn(math.pi / 2)


def castle_gate(along_y=False):
    """A wall pierced by a pointed arch with a carved frame, a dark
    passage and a raised steel portcullis."""
    stone, carved = mat("ashlar", ASHLAR), mat("carved", CARVED)
    w = solid(box((WALL_L, WALL_T, WALL_H), (0, 0, WALL_H / 2), stone))
    aw, ah = 2.6, 3.8
    c1 = solid(box((aw, 3, ah - aw / 2), (0, 0, (ah - aw / 2) / 2 - 0.05), stone))
    cut(w, c1)
    # A pointed arch: two overlapping circles meeting at the top.
    for s in (-1, 1):
        c = cyl(aw * 0.8, 3, (s * aw * 0.3, 0, ah - aw / 2), stone, rot=(math.pi / 2, 0, 0), verts=40)
        keep = solid(box((aw / 2 + 0.01, 3.2, aw), (-s * aw / 4, 0, ah - aw / 2 + aw / 2), stone))
        c.modifiers.new("i", "BOOLEAN").operation = "INTERSECT"
        c.modifiers["i"].object = keep
        bpy.context.view_layer.objects.active = c
        bpy.ops.object.modifier_apply(modifier="i")
        bpy.data.objects.remove(keep)
        cut(w, c)
    box((aw + 0.1, 0.2, ah + 0.4), (0, WALL_T / 2 - 0.05, ah / 2), mat("black", BLACK))
    # Carved frame: jambs, a keystone and a panel over the arch.
    for s in (-1, 1):
        box((0.45, 0.35, ah - aw / 2), (s * (aw / 2 + 0.22), -WALL_T / 2 - 0.12, (ah - aw / 2) / 2), carved)
        box((0.7, 0.4, 0.3), (s * (aw / 2 + 0.22), -WALL_T / 2 - 0.15, ah - aw / 2), carved)
    box((0.5, 0.4, 0.7), (0, -WALL_T / 2 - 0.15, ah + 0.05), carved)
    box((1.6, 0.25, 0.7), (0, -WALL_T / 2 - 0.1, ah + 0.85), carved)
    ball(0.22, (0, -WALL_T / 2 - 0.3, ah + 0.85), mat("steel", STEEL), scale=(1, 0.6, 1))
    # The portcullis, drawn up so only its spiked foot shows.
    steel = mat("steel", STEEL)
    for k in range(5):
        x = -aw / 2 + 0.35 + k * (aw - 0.7) / 4
        box((0.12, 0.12, 1.3), (x, 0.05, ah - 0.7), steel)
        cyl(0.08, 0.3, (x, 0.05, ah - 1.5), steel, r2=0.0, rot=(math.pi, 0, 0), verts=6)
    box((aw, 0.14, 0.14), (0, 0.05, ah - 0.9), steel)
    plinth = solid(box((WALL_L + 0.05, WALL_T + 0.3, 0.45), (0, 0, 0.22), carved))
    cut(plinth, solid(box((aw, 3, 1.0), (0, 0, 0.3), stone)))
    coping(WALL_L, WALL_H + 0.1)
    if along_y:
        turn(math.pi / 2)


def tower(round_=False, seed=3):
    """A slim buttressed tower under a flared steel cap."""
    stone, carved, steel = mat("ashlar", ASHLAR), mat("carved", CARVED), mat("steel", STEEL)
    h = 6.4
    if round_:
        cyl(1.15, h, (0, 0, h / 2), stone, verts=24)
        for k in range(4):
            a = math.pi / 4 + k * math.pi / 2
            box((0.5, 0.5, 2.4), (1.15 * math.cos(a), 1.15 * math.sin(a), 1.2), carved, rot=(0, 0, a))
            box((0.35, 0.35, 1.0), (1.3 * math.cos(a), 1.3 * math.sin(a), 0.5), carved, rot=(0, 0, a))
    else:
        solid(box((2.2, 2.2, h), (0, 0, h / 2), stone))
        for sx in (-1, 1):
            for sy in (-1, 1):
                box((0.55, 0.55, 2.6), (sx * 1.1, sy * 1.1, 1.3), carved)
        # A narrow slit window and a carved panel on each face.
        for rot, (x, y) in ((0, (0, -1.11)), (math.pi / 2, (1.11, 0))):
            box((0.25, 0.1, 1.2), (x, y, 4.4), mat("black", BLACK), rot=(0, 0, rot))
            box((0.9, 0.12, 0.9), (x * 1.01, y * 1.01, 2.6), carved, rot=(0, 0, rot))
    cyl(1.35, 0.5, (0, 0, h + 0.1), carved, verts=24)
    cyl(1.4, 1.1, (0, 0, h + 0.85), steel, r2=1.95, verts=32)
    cyl(1.95, 0.25, (0, 0, h + 1.5), steel, verts=32)
    cyl(1.7, 0.2, (0, 0, h + 1.65), mat("earthtop", EARTHTOP), verts=32)
    shrink(0.62)


def carved_pillar(seed=4):
    """A tall square pillar of carved panels under a stepped head with a
    steel boss."""
    stone, carved, steel = mat("ashlar", ASHLAR), mat("carved", CARVED), mat("steel", STEEL)
    h = 5.8
    solid(box((2.2, 2.2, h), (0, 0, h / 2), mat("pale", PALE)))
    box((2.6, 2.6, 0.6), (0, 0, 0.3), stone)
    box((2.6, 2.6, 0.5), (0, 0, h + 0.2), stone)
    box((2.2, 2.2, 0.4), (0, 0, h + 0.6), stone)
    for rot, (x, y) in ((0, (0, -1.1)), (math.pi / 2, (1.1, 0))):
        for z, zh in ((1.7, 2.0), (4.0, 2.0)):
            box((1.5, 0.12, zh), (x * 1.005, y * 1.005, z), mat("black", BLACK), rot=(0, 0, rot))
            box((1.2, 0.2, zh - 0.3), (x * 1.02, y * 1.02, z), stone, rot=(0, 0, rot))
        ball(0.32, (x * 1.12, y * 1.12, h - 0.4), steel, scale=(1, 1, 1))


def keep(seed=5):
    """A great block of the keep, its top hollowed into a round bay."""
    stone = mat("ashlar", ASHLAR)
    h = 7.0
    o = solid(box((5.2, 3.0, h), (0, 0, h / 2), stone))
    cut(o, cyl(1.6, 3.4, (0, -1.5, h), stone, rot=(0, 0, 0), verts=40))
    top = solid(box((5.2, 3.0, 0.2), (0, 0, h), mat("earthtop", EARTHTOP)))
    cut(top, cyl(1.6, 3.4, (0, -1.5, h), stone, verts=40))
    steel = mat("steel", STEEL)
    for x in (-2.6, 2.6):
        cyl(0.25, 3.0, (x, 0, h), steel, rot=(math.pi / 2, 0, 0), verts=16)
    solid(box((5.3, 3.1, 0.5), (0, 0, 0.25), mat("carved", CARVED)))
    for x in (-1.6, 1.6):
        box((0.7, 0.2, 1.4), (x, -1.52, 4.0), mat("black", BLACK))
        box((0.9, 0.3, 0.25), (x, -1.55, 4.8), mat("carved", CARVED))


def figure(m, sword=True, scale=1.0, at=(0, 0, 0)):
    """A stocky armoured figure, sword raised when asked."""
    x0, y0, z0 = at
    s = scale
    for side in (-1, 1):
        box((0.38 * s, 0.4 * s, 1.5 * s), (x0 + side * 0.28 * s, y0, z0 + 0.75 * s), m)
    box((1.0 * s, 0.6 * s, 1.5 * s), (x0, y0, z0 + 2.2 * s), m)
    cyl(0.62 * s, 0.9 * s, (x0, y0, z0 + 1.55 * s), m, r2=0.45 * s, verts=12)
    ball(0.32 * s, (x0, y0, z0 + 3.25 * s), m)
    ball(0.33 * s, (x0, y0, z0 + 3.35 * s), m, scale=(1, 1, 0.6))
    for side in (-1, 1):
        ball(0.3 * s, (x0 + side * 0.62 * s, y0, z0 + 2.8 * s), m)
    box((0.26 * s, 0.26 * s, 1.3 * s), (x0 - 0.7 * s, y0, z0 + 2.1 * s), m)
    if sword:
        box((0.26 * s, 1.3 * s, 0.26 * s), (x0 + 0.75 * s, y0 - 0.5 * s, z0 + 2.75 * s), m, rot=(0.5, 0, 0))
        box((0.16 * s, 2.6 * s, 0.07 * s), (x0 + 0.75 * s, y0 - 2.1 * s, z0 + 3.7 * s), m, rot=(0.5, 0, 0))
        box((0.7 * s, 0.14 * s, 0.14 * s), (x0 + 0.75 * s, y0 - 1.0 * s, z0 + 3.1 * s), m, rot=(0.5, 0, 0))
    else:
        box((0.26 * s, 0.26 * s, 1.3 * s), (x0 + 0.7 * s, y0, z0 + 2.1 * s), m)


def statue_knight():
    """A stone knight on a round plinth, thrusting his sword forward."""
    st = mat("statue", STATUE)
    cyl(1.0, 0.9, (0, 0, 0.45), mat("carved", CARVED), verts=20)
    cyl(1.1, 0.2, (0, 0, 0.95), mat("carved", CARVED), verts=20)
    figure(st, sword=True, scale=0.75, at=(0, 0, 1.05))


def statue_saint():
    """A slim robed figure, hands together, on a tall round plinth."""
    st = mat("statue", STATUE)
    cyl(0.55, 1.3, (0, 0, 0.65), mat("carved", CARVED), verts=16)
    cyl(0.65, 0.2, (0, 0, 1.4), mat("carved", CARVED), verts=16)
    cyl(0.5, 2.6, (0, 0, 2.8), st, r2=0.32, verts=14)
    ball(0.3, (0, 0, 4.35), st)
    ball(0.38, (0, -0.12, 4.4), st, scale=(1, 1, 1.2))  # hood
    box((0.25, 0.4, 0.5), (0, -0.4, 3.6), st)


def statue_gargoyle():
    """A crouching winged beast on a square plinth with pale corner posts."""
    st, marble = mat("statue", STATUE), mat("marble", MARBLE)
    box((3.0, 3.0, 1.6), (0, 0, 0.8), mat("carved", CARVED))
    box((3.3, 3.3, 0.25), (0, 0, 1.7), mat("carved", CARVED))
    for sx in (-1, 1):
        for sy in (-1, 1):
            box((0.4, 0.4, 1.9), (sx * 1.55, sy * 1.55, 0.95), marble)
            ball(0.25, (sx * 1.55, sy * 1.55, 2.0), marble)
    ball(0.9, (0, 0.2, 2.8), st, scale=(0.8, 1.2, 1.0))
    ball(0.55, (0, -0.85, 3.5), st, scale=(0.9, 1.1, 0.9))
    cyl(0.18, 0.7, (0, -1.4, 3.4), st, r2=0.05, rot=(math.pi / 2, 0, 0), verts=8)
    for side in (-1, 1):
        cyl(0.12, 0.8, (side * 0.4, -0.75, 4.05), st, r2=0.0, rot=(-0.4, side * 0.4, 0), verts=6)  # horns
        box((0.28, 0.35, 1.0), (side * 0.55, -0.6, 2.3), st)
        box((0.28, 0.6, 0.9), (side * 0.6, 0.6, 2.25), st)
        for k in range(3):
            box((0.1, 0.4 + k * 0.1, 2.4 - k * 0.4), (side * (1.1 + k * 0.45), 0.6 + k * 0.1, 3.8 - k * 0.15), st,
                rot=(0, side * (0.5 + k * 0.35), 0))
        box((1.6, 0.12, 0.25), (side * 1.4, 0.6, 4.7), st, rot=(0, -side * 0.45, 0))


def shrine(seed=6, cross=True):
    """A small dark roofed shrine, a cross or a spike on its gable."""
    stone, roof, steel = mat("ashlar", ASHLAR), mat("roof", ROOF), mat("steel", STEEL)
    solid(box((1.4, 1.4, 1.5), (0, 0, 0.75), stone))
    cyl(1.05, 1.6, (0, 0, 1.5), roof, rot=(0, math.pi / 2, 0), verts=4)
    box((0.6, 0.06, 0.8), (0, -0.71, 0.75), mat("black", BLACK))
    if cross:
        box((0.1, 0.1, 1.2), (0, 0, 3.0), steel)
        box((0.6, 0.1, 0.1), (0, 0, 3.25), steel)
    else:
        cyl(0.12, 1.0, (0, 0, 3.0), steel, r2=0.0, verts=6)
    if random.Random(seed).random() < 0.5:
        turn(math.pi / 2)


def hall(seed=7):
    """A pale marble hall with a dark roof, pillars at its corners and a
    fire glowing red through its open arch."""
    marble, roof = mat("marble", MARBLE), mat("roof", ROOF)
    fire = glow("fire", (1.0, 0.30, 0.06), (0.40, 0.03, 0.0), 1.6, scale=10, band=(0.4, 0.85))
    w, d, h = 4.6, 4.6, 5.6
    o = solid(box((w, d, h), (0, 0, h / 2), marble))
    cut(o, solid(box((2.2, 2.0, 3.8), (0, -d / 2, 1.9), marble)))
    cut(o, solid(box((2.0, 2.2, 3.8), (w / 2, 0, 1.9), marble)))
    box((2.1, 0.2, 3.7), (0, -d / 2 + 1.0, 1.85), fire)
    box((0.2, 2.1, 3.7), (w / 2 - 1.0, 0, 1.85), fire)
    for x, y in ((-w / 2, -d / 2), (w / 2, -d / 2), (w / 2, d / 2)):
        cyl(0.45, h, (x, y, h / 2), marble, verts=16)
        box((1.1, 1.1, 0.4), (x, y, 0.2), marble)
        box((1.1, 1.1, 0.4), (x, y, h - 0.2), marble)
    box((w + 0.8, d + 0.8, 0.5), (0, 0, h + 0.25), marble)
    cyl((w + 0.6) * 0.71, 1.4, (0, 0, h + 1.2), mat("steel", STEEL), rot=(0, 0, math.pi / 4), verts=4, r2=1.2)
    for k in range(3):
        box((0.9, 0.35, 0.35), (0, -d / 2 - 0.4 - k * 0.35, 0.18 + k * 0.0), marble)


def pew(seed=8):
    """A short dark red wooden pew."""
    wood = mat("wood", WOOD)
    box((2.2, 0.7, 0.15), (0, 0, 0.85), wood)
    box((2.2, 0.12, 1.1), (0, 0.35, 1.3), wood)
    for x in (-1.05, 1.05):
        box((0.12, 0.8, 1.1), (x, 0.0, 0.55), wood)
    turn(math.pi / 2 if seed % 2 else 0)


def magic_circle():
    """A square stone slab inlaid with a black disc, a ring of gold and
    golden lines, gold studs at its corners."""
    carved, gold = mat("carved", CARVED), glow("gold", (0.95, 0.70, 0.25), (0.45, 0.28, 0.06), 0.6, scale=12)
    s = 3.6
    box((s, s, 0.3), (0, 0, 0.15), carved)
    box((s - 0.4, s - 0.4, 0.05), (0, 0, 0.3), mat("ashlar", ASHLAR))
    cyl(1.5, 0.08, (0, 0, 0.33), gold, verts=48)
    cyl(1.38, 0.1, (0, 0, 0.34), mat("black", BLACK), verts=48)
    rng = random.Random(9)
    for k in range(7):
        a = rng.uniform(0, math.pi)
        box((rng.uniform(1.2, 2.5), 0.06, 0.03), (rng.uniform(-0.3, 0.3), rng.uniform(-0.3, 0.3), 0.4), gold,
            rot=(0, 0, a))
    for sx in (-1, 1):
        for sy in (-1, 1):
            ball(0.22, (sx * (s / 2 - 0.3), sy * (s / 2 - 0.3), 0.4), gold)


PROPS = {
    "castle_x": (castle_wall, 256),
    "castle_y": (lambda: castle_wall(along_y=True, seed=2), 256),
    "castle_gate_x": (castle_gate, 256),
    "arcade_x": (arcade, 256),
    "arcade_y": (lambda: arcade(along_y=True), 256),
    "castle_gate_y": (lambda: castle_gate(along_y=True), 256),
    "tower": (tower, 192),
    "tower_round": (lambda: tower(round_=True), 192),
    "carved_pillar": (carved_pillar, 192),
    "keep": (keep, 320),
    "statue_knight": (statue_knight, 160),
    "statue_saint": (statue_saint, 128),
    "statue_gargoyle": (statue_gargoyle, 224),
    "shrine": (shrine, 96),
    "shrine_spike": (lambda: shrine(seed=3, cross=False), 96),
    "fire_hall": (hall, 352),
    "pew": (pew, 96),
    "pew_y": (lambda: pew(seed=1), 96),
    "magic_circle": (magic_circle, 256),
}


def main():
    args = [a for a in sys.argv[sys.argv.index("--") + 1:]] if "--" in sys.argv else sys.argv[1:]
    blend = "--blend" in args
    args = [a for a in args if a != "--blend"]
    out = Path(args[0])
    names = args[1:] or list(PROPS)
    out.mkdir(parents=True, exist_ok=True)
    listing = out / "castle.txt"
    anchors = dict(l.split(" ", 1) for l in listing.read_text().split("\n") if l) if listing.exists() else {}
    for name in names:
        build, size = PROPS[name]
        _mats.clear()
        scene = studio.reset(samples=32)
        build()
        if name.startswith(("castle_", "arcade")):  # the original's walls cast no shadows
            bpy.data.objects["shadow catcher"].hide_render = True
        for m in bpy.data.materials:
            # No glossy sheen on stone; steel and gold keep theirs.
            if m.node_tree and "Principled BSDF" in m.node_tree.nodes and m.name not in ("steel", "gold"):
                m.node_tree.nodes["Principled BSDF"].inputs["Specular IOR Level"].default_value = 0.0
        studio.camera(scene, size, size, target=(0, 0, size / studio.PIXELS_PER_UNIT * 0.3))
        ax, ay = studio.anchor(scene)
        if blend:
            studio.save_blend(out / f"{name}.blend")
        studio.render(scene, str(out / f"{name}.png"))
        anchors[name] = f"{ax:.0f} {ay:.0f}"
        print(name, "done", flush=True)
    listing.write_text("".join(f"{k} {v}\n" for k, v in anchors.items()))


if __name__ == "__main__":
    main()
