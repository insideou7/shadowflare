"""Town and forest props in ShadowFlare's style, rendered with studio.py.

    python tools/art/town.py <output folder> [prop ...]

(run with a Python that has the `bpy` module). Writes one PNG per prop plus
town.txt listing each file's anchor point; with prop names, renders only
those. Sizes follow the original's scale: the hero is about 3.5 Blender
units tall and a map tile is 3.2 units square.
"""

import math
import random
import sys
from pathlib import Path

import bpy
import bmesh
from mathutils import Vector

sys.path.insert(0, str(Path(__file__).resolve().parent))
import studio  # noqa: E402

STONE = dict(colour=(0.50, 0.43, 0.32), dark=(0.24, 0.20, 0.14), scale=7, bump=0.8, grime=0.5,
             bricks=dict(width=0.55, height=0.3, mortar=0.025, vary=0.82, joint=0.6))
MOSSY = dict(colour=(0.40, 0.40, 0.30), dark=(0.14, 0.15, 0.10), scale=6, bump=0.9, grime=0.5,
             bricks=dict(width=0.5, height=0.25, mortar=0.04, vary=0.7, joint=0.3),
             spot=0.35, spot_colour=(0.30, 0.36, 0.16))
PLASTER = dict(colour=(0.70, 0.64, 0.52), dark=(0.45, 0.40, 0.31), scale=5, bump=0.4, grime=0.5)
TIMBER = dict(colour=(0.30, 0.20, 0.12), dark=(0.10, 0.07, 0.04), scale=12, bump=0.5, stretch=(4, 4, 0.2), grime=0.3)
PLANK = dict(colour=(0.55, 0.40, 0.24), dark=(0.22, 0.15, 0.08), scale=10, bump=0.6, stretch=(1, 6, 0.3))
TILES = dict(colour=(0.66, 0.34, 0.24), dark=(0.36, 0.16, 0.10), scale=18, bump=1.2, grime=0.4,
             bricks=dict(width=0.42, height=0.3, mortar=0.07, vary=0.7, joint=0.25))
CLOTH = dict(colour=(0.86, 0.82, 0.70), dark=(0.55, 0.50, 0.40), scale=6, bump=0.3)
IRON = dict(colour=(0.40, 0.40, 0.42), dark=(0.14, 0.14, 0.16), scale=20, rough=0.4, bump=0.3)
COPPER = dict(colour=(0.62, 0.38, 0.24), dark=(0.25, 0.13, 0.08), scale=10, rough=0.35, bump=0.3,
              spot=0.2, spot_colour=(0.30, 0.42, 0.34))
SACKING = dict(colour=(0.52, 0.45, 0.30), dark=(0.26, 0.21, 0.13), scale=25, bump=0.6)
LEAVES = dict(colour=(0.30, 0.46, 0.12), dark=(0.06, 0.13, 0.03), scale=30, detail=8, bump=1.0,
              spot=0.25, spot_colour=(0.48, 0.62, 0.16))
BARK = dict(colour=(0.30, 0.25, 0.20), dark=(0.10, 0.08, 0.06), scale=14, bump=0.7, stretch=(4, 4, 0.3))

_mats = {}


def mat(name, spec=None, **kw):
    """One material per name and scene."""
    key = name
    if key not in _mats or _mats[key].name not in bpy.data.materials:
        _mats[key] = studio.material(name, **(spec or kw))
    return _mats[key]


def box(size, at, m, rot=(0, 0, 0)):
    bpy.ops.mesh.primitive_cube_add(size=1, location=at, rotation=rot)
    o = bpy.context.object
    o.scale = size
    return studio.assign(o, m)


def cyl(r, h, at, m, rot=(0, 0, 0), verts=16, r2=None):
    if r2 is None:
        bpy.ops.mesh.primitive_cylinder_add(vertices=verts, radius=r, depth=h, location=at, rotation=rot)
    else:
        bpy.ops.mesh.primitive_cone_add(vertices=verts, radius1=r, radius2=r2, depth=h, location=at, rotation=rot)
    return studio.smooth(studio.assign(bpy.context.object, m))


def ball(r, at, m, scale=(1, 1, 1)):
    bpy.ops.mesh.primitive_uv_sphere_add(segments=16, ring_count=10, radius=r, location=at)
    o = bpy.context.object
    o.scale = scale
    return studio.smooth(studio.assign(o, m))


def mesh(verts, faces, m, name="mesh"):
    me = bpy.data.meshes.new(name)
    me.from_pydata(verts, [], faces)
    me.update()
    o = bpy.data.objects.new(name, me)
    bpy.context.scene.collection.objects.link(o)
    me.materials.append(m)
    return o


def gable_roof(w, d, h, z, m, overhang=0.35, axis="x"):
    """A pitched roof over a w x d footprint, ridge along x (or y)."""
    ox, oy = w / 2 + overhang, d / 2 + overhang
    if axis == "x":
        v = [(-ox, -oy, z), (ox, -oy, z), (ox, oy, z), (-ox, oy, z), (-ox, 0, z + h), (ox, 0, z + h)]
        f = [(0, 1, 5, 4), (2, 3, 4, 5), (0, 4, 3), (1, 2, 5)]
    else:
        v = [(-ox, -oy, z), (ox, -oy, z), (ox, oy, z), (-ox, oy, z), (0, -oy, z + h), (0, oy, z + h)]
        f = [(1, 2, 5, 4), (3, 0, 4, 5), (0, 1, 4), (2, 3, 5)]
    o = mesh(v, f, m, "roof")
    sol = o.modifiers.new("thick", "SOLIDIFY")
    sol.thickness = 0.12
    return o


def timber_frame(w, d, z0, z1, m):
    """Dark posts at the corners and a beam round the top of a wall block."""
    for sx in (-1, 1):
        for sy in (-1, 1):
            box((0.22, 0.22, z1 - z0), (sx * w / 2, sy * d / 2, (z0 + z1) / 2), m)
    for z in (z0 + 0.1, z1 - 0.1):
        box((w + 0.25, 0.2, 0.2), (0, -d / 2, z), m)
        box((w + 0.25, 0.2, 0.2), (0, d / 2, z), m)
        box((0.2, d + 0.25, 0.2), (-w / 2, 0, z), m)
        box((0.2, d + 0.25, 0.2), (w / 2, 0, z), m)


def window(at, facing_x, m_frame, m_glass):
    sx, sy = (0.06, 0.7) if facing_x else (0.7, 0.06)
    box((sx + 0.06, sy + 0.12, 0.95), at, m_frame)
    off = Vector((0.04, 0, 0) if facing_x else (0, -0.04, 0))
    box((sx, sy, 0.8), Vector(at) + off, m_glass)


# --- props -------------------------------------------------------------------


def cottage(seed=1):
    """A weathered stone house with tiled roofs: a tall main block, a lower
    wing with its own roof, a chimney, a plank door and small windows."""
    stone, timber, tiles = mat("stone", STONE), mat("timber", TIMBER), mat("tiles", TILES)
    plank = mat("plank", PLANK)
    glass = mat("glass", colour=(0.10, 0.10, 0.12), dark=(0.04, 0.04, 0.05), scale=3, rough=0.2, bump=0)
    w, d, h = 9.5, 8.0, 6.0
    box((w, d, h), (0, 0, h / 2), stone)
    gable_roof(w, d, 4.2, h, tiles, overhang=0.5)
    for sx in (-1, 1):
        mesh([(sx * w / 2, -d / 2, h), (sx * w / 2, d / 2, h), (sx * w / 2, 0, h + 4.2)], [(0, 1, 2)], stone, "gable")
    # The wing, towards the camera on the right.
    ww, wd, wh = 5.5, 5.0, 4.2
    box((ww, wd, wh), (w / 2 - ww / 2 + 1.0, -d / 2 - wd / 2 + 0.5, wh / 2), stone)
    roof = gable_roof(ww, wd, 2.8, wh, tiles, overhang=0.4, axis="y")
    roof.location = (w / 2 - ww / 2 + 1.0, -d / 2 - wd / 2 + 0.5, 0)
    mesh([(w / 2 + 1.0 - ww, -d / 2 - wd + 0.5, wh), (w / 2 + 1.0, -d / 2 - wd + 0.5, wh),
          (w / 2 + 1.0 - ww / 2, -d / 2 - wd + 0.5, wh + 2.8)], [(0, 1, 2)], stone, "gable")
    # A small lean-to shed on the far right.
    box((2.4, 3.0, 2.6), (w / 2 + 2.2, 1.0, 1.3), stone)
    shed = mesh([(w / 2 + 0.9, -0.7, 2.6), (w / 2 + 3.6, -0.7, 2.0), (w / 2 + 3.6, 2.7, 2.0), (w / 2 + 0.9, 2.7, 2.6)],
                [(0, 1, 2, 3)], tiles, "shed")
    shed.modifiers.new("thick", "SOLIDIFY").thickness = 0.12
    box((1.1, 1.1, 3.0), (-2.5, 1.5, h + 3.6), stone)  # chimney
    box((1.4, 0.14, 2.4), (-1.5, -d / 2 - 0.07, 1.2), plank)
    window((-3.6, -d / 2 - 0.05, 3.6), False, timber, glass)
    window((w / 2 + 1.0 - ww / 2, -d / 2 - wd + 0.45, 2.4), False, timber, glass)
    window((w / 2 + 1.05, -d / 2 - wd / 2 + 0.5, 2.4), True, timber, glass)
    window((w / 2 + 0.05, 2.5, 4.4), True, timber, glass)
    box((0.9, 0.1, 2.0), (w / 2 + 3.65, 1.0, 1.0), plank, rot=(0, 0, math.radians(90)))


def hall(seed=2):
    """A bigger two-storey house: stone ground floor, jettied timber upper
    floor, a steep roof with a cross gable."""
    stone, plaster, timber, tiles = mat("stone", STONE), mat("plaster", PLASTER), mat("timber", TIMBER), mat("tiles", TILES)
    glass = mat("glass", colour=(0.10, 0.10, 0.12), dark=(0.04, 0.04, 0.05), scale=3, rough=0.2, bump=0)
    w, d = 11.0, 8.0
    box((w, d, 3.4), (0, 0, 1.7), stone)
    box((w + 0.6, d + 0.6, 3.0), (0, 0, 4.9), plaster)
    timber_frame(w + 0.6, d + 0.6, 3.4, 6.4, timber)
    # Close-set studs and a middle rail on the two walls facing the camera.
    for i in range(12):
        x = -(w + 0.6) / 2 + (i + 0.5) * (w + 0.6) / 12
        box((0.16, 0.12, 3.0), (x, -(d + 0.6) / 2 - 0.04, 4.9), timber)
    for i in range(9):
        y = -(d + 0.6) / 2 + (i + 0.5) * (d + 0.6) / 9
        box((0.12, 0.16, 3.0), ((w + 0.6) / 2 + 0.04, y, 4.9), timber)
    box((w + 0.7, 0.14, 0.16), (0, -(d + 0.6) / 2 - 0.06, 4.9), timber)
    box((0.14, d + 0.7, 0.16), ((w + 0.6) / 2 + 0.06, 0, 4.9), timber)
    gable_roof(w + 0.6, d + 0.6, 4.6, 6.4, tiles)
    for sx in (-1, 1):
        mesh([(sx * (w + 0.6) / 2, -(d + 0.6) / 2, 6.4), (sx * (w + 0.6) / 2, (d + 0.6) / 2, 6.4),
              (sx * (w + 0.6) / 2, 0, 11.0)], [(0, 1, 2)], plaster, "gable")
    # Cross gable over the front door.
    box((3.6, 2.0, 3.0), (1.5, -(d + 0.6) / 2 - 0.6, 4.9), plaster)
    roof = gable_roof(3.6, 2.4, 2.4, 6.4, tiles, overhang=0.3, axis="y")
    roof.location = (1.5, -(d + 0.6) / 2 - 0.4, 0)
    box((1.6, 0.14, 2.6), (1.5, -d / 2 - 0.07, 1.3), timber)
    for x in (-3.8, -1.6, 4.4):
        window((x, -d / 2 - 0.05, 1.9), False, timber, glass)
        window((x, -(d + 0.6) / 2 - 0.05, 5.0), False, timber, glass)
    for y in (-2.0, 1.5):
        window((w / 2 + 0.05, y, 1.9), True, timber, glass)
        window((w / 2 + 0.35, y, 5.0), True, timber, glass)
    box((1.0, 1.0, 3.5), (-3.5, 1.5, 10.0), stone)
    # A tall stone chimney stack up the end wall.
    box((1.4, 1.2, 12.0), (w / 2 + 0.9, 1.2, 6.0), stone)


def stall(seed=3, fruit=None):
    """A market stall: a plank counter on legs under a sagging cloth awning."""
    rng = random.Random(seed)
    plank, cloth, timber = mat("plank", PLANK), mat("cloth", CLOTH), mat("timber", TIMBER)
    w, d = 3.6, 2.2
    box((w, d, 0.15), (0, 0, 1.1), plank)
    box((w, 0.1, 1.0), (0, -d / 2, 0.55), plank)
    for sx in (-1, 1):
        for sy in (-1, 1):
            box((0.14, 0.14, 2.9 if sy > 0 else 2.5), (sx * w / 2, sy * d / 2, 1.45 if sy > 0 else 1.25), timber)
    # Awning sloping towards the front, with a sag and a hanging valance.
    bpy.ops.mesh.primitive_grid_add(x_subdivisions=12, y_subdivisions=8, size=1, location=(0, 0, 2.7))
    aw = bpy.context.object
    aw.scale = (w + 0.6, d + 0.8, 1)
    aw.rotation_euler = (math.radians(-14), 0, 0)
    studio.displace(aw, 0.25, 0.6, seed)
    studio.assign(aw, cloth)
    sol = aw.modifiers.new("thick", "SOLIDIFY")
    sol.thickness = 0.05
    box((w + 0.6, 0.04, 0.35), (0, -(d + 0.8) / 2 * math.cos(math.radians(14)), 2.35), cloth)
    # Goods on the counter.
    for i in range(6):
        g = mat("goods", colour=rng.choice([(0.70, 0.55, 0.30), (0.45, 0.50, 0.25), (0.62, 0.30, 0.20)]), scale=8)
        box((rng.uniform(0.4, 0.7),) * 2 + (rng.uniform(0.2, 0.5),),
            (rng.uniform(-1.4, 1.4), rng.uniform(-0.6, 0.6), 1.35), g, rot=(0, 0, rng.uniform(0, 1)))


def crate(seed=4, colour=(0.85, 0.70, 0.15)):
    """A slatted crate on a low stand, heaped with round fruit."""
    rng = random.Random(seed)
    plank = mat("plank", PLANK)
    fruit = studio.material(f"fruit{seed}", colour=colour, dark=[c * 0.55 for c in colour], scale=12, rough=0.5, bump=0.1)
    for sx in (-1, 1):
        for sy in (-1, 1):
            box((0.1, 0.1, 0.6), (sx * 0.65, sy * 0.65, 0.3), plank)
    for z in (0.75, 1.0):
        for a in range(4):
            r = a * math.pi / 2
            box((1.5, 0.08, 0.18), (0.72 * math.sin(r), -0.72 * math.cos(r), z), plank, rot=(0, 0, r))
    box((1.45, 1.45, 0.06), (0, 0, 0.65), plank)
    for _ in range(40):
        ball(0.16, (rng.uniform(-0.55, 0.55), rng.uniform(-0.55, 0.55), rng.uniform(1.0, 1.35)), fruit)


def well(seed=5):
    """A round stone well with a wooden frame, a roller and a bucket."""
    stone, timber, iron = mat("mossy", MOSSY), mat("timber", TIMBER), mat("iron", IRON)
    cyl(1.3, 1.3, (0, 0, 0.65), stone, verts=20)
    water = mat("water", colour=(0.05, 0.07, 0.08), scale=3, rough=0.1, bump=0)
    cyl(1.0, 0.05, (0, 0, 1.28), water, verts=20)
    for sx in (-1, 1):
        box((0.2, 0.2, 3.0), (sx * 1.2, 0, 2.2), timber)
    cyl(0.12, 2.6, (0, 0, 3.2), timber, rot=(0, math.radians(90), 0), verts=10)
    box((0.5, 0.06, 0.06), (1.55, 0, 3.0), timber)  # crank
    box((0.06, 0.06, 0.3), (1.8, 0, 2.85), timber)
    cyl(0.02, 1.4, (0, 0, 2.5), iron, verts=6)  # rope
    cyl(0.28, 0.4, (0, 0, 1.6), timber, r2=0.22, verts=12)
    gable_roof(2.8, 1.6, 0.9, 3.7, mat("tiles", TILES), overhang=0.15)


def cart(seed=6):
    """A two-wheeled handcart with plank sides and long shafts."""
    plank, timber, iron = mat("plank", PLANK), mat("timber", TIMBER), mat("iron", IRON)
    box((3.2, 1.8, 0.12), (0, 0, 1.2), plank)
    for sy in (-1, 1):
        box((3.2, 0.08, 0.6), (0, sy * 0.9, 1.55), plank)
    box((0.08, 1.8, 0.6), (-1.6, 0, 1.55), plank)
    for sy in (-1, 1):
        cyl(0.95, 0.12, (0.2, sy * 1.05, 0.95), timber, rot=(math.radians(90), 0, 0), verts=24)
        cyl(0.97, 0.06, (0.2, sy * 1.05, 0.95), iron, rot=(math.radians(90), 0, 0), verts=24)
        box((3.4, 0.1, 0.1), (2.6, sy * 0.7, 1.0), timber, rot=(0, math.radians(12), 0))
    cyl(0.06, 2.3, (0.2, 0, 0.95), iron, rot=(math.radians(90), 0, 0))
    bpy.ops.object.select_all(action="DESELECT")


def barrel(seed=7):
    plank, iron = mat("plank", PLANK), mat("iron", IRON)
    bpy.ops.mesh.primitive_cylinder_add(vertices=16, radius=0.5, depth=1.3, location=(0, 0, 0.65))
    b = bpy.context.object
    bm = bmesh.new()
    bm.from_mesh(b.data)
    bmesh.ops.subdivide_edges(bm, edges=[e for e in bm.edges if abs(e.verts[0].co.z - e.verts[1].co.z) > 0.1], cuts=4)
    for v in bm.verts:
        v.co.x *= 1 + 0.18 * (1 - (v.co.z / 0.65) ** 2)
        v.co.y *= 1 + 0.18 * (1 - (v.co.z / 0.65) ** 2)
    bm.to_mesh(b.data)
    studio.smooth(studio.assign(b, plank))
    for z in (0.15, 0.45, 0.85, 1.15):
        r = 0.5 * (1 + 0.18 * (1 - ((z - 0.65) / 0.65) ** 2)) + 0.02
        cyl(r, 0.06, (0, 0, z), iron, verts=16)


def sacks(seed=8):
    """A heap of filled sacks: soft, pillow-shaped and lying flat."""
    rng = random.Random(seed)
    sack = mat("sacking", SACKING)
    spots = [(-0.6, -0.2, 0.25, 0.4), (0.5, -0.4, 0.25, -0.3), (0.0, 0.5, 0.25, 1.2), (0.0, 0.0, 0.7, 0.2)]
    for i, (x, y, z, r) in enumerate(spots):
        o = box((1.3, 0.8, 0.45), (x, y, z), sack, rot=(0, rng.uniform(-0.15, 0.15), r))
        sub = o.modifiers.new("soft", "SUBSURF")
        sub.levels = 2
        studio.smooth(o)
        studio.displace(o, 0.08, 0.3, seed * 10 + i)


def firewood(seed=9):
    rng = random.Random(seed)
    bark = mat("bark", BARK)
    for row in range(3):
        for i in range(4 - row):
            cyl(0.17, 1.6, (rng.uniform(-0.05, 0.05), (i - (3 - row) / 2) * 0.34, 0.17 + row * 0.3), bark,
                rot=(0, math.radians(90), rng.uniform(-0.1, 0.1)), verts=8)


def signpost(seed=10):
    """A tall post with a hanging painted board."""
    timber, iron = mat("timber", TIMBER), mat("iron", IRON)
    paint = mat("paint", colour=(0.20, 0.25, 0.45), dark=(0.10, 0.12, 0.25), scale=6)
    gold = mat("gold", colour=(0.85, 0.65, 0.20), dark=(0.45, 0.30, 0.08), scale=10, rough=0.3)
    box((0.25, 0.25, 6.0), (0, 0, 3.0), timber)
    box((1.6, 0.14, 0.14), (0.75, 0, 5.6), timber)
    for x in (0.3, 1.3):
        cyl(0.02, 0.35, (x, 0, 5.35), iron, verts=6)
    box((1.3, 0.08, 1.6), (0.8, 0, 4.35), paint)
    box((0.5, 0.1, 0.5), (0.8, -0.02, 4.4), gold, rot=(0, math.radians(45), 0))


def ruin_wall(seed=11, length=3.2, broken=False, along_y=False):
    """A mossy wall of rough stone blocks; broken walls step down and drop
    blocks on the ground."""
    rng = random.Random(seed)
    stone = mat("mossy", MOSSY)
    rows = 9
    for r in range(rows):
        z = 0.14 + r * 0.27
        x = -length / 2 + (0.3 if r % 2 else 0)
        while x < length / 2:
            bw = min(rng.uniform(0.6, 1.0), length / 2 - x)
            top = rows if not broken else int(rows * (0.35 + 0.65 * abs(x) / (length / 2)))
            if r < top and bw > 0.15:
                cx, cy = x + bw / 2, rng.uniform(-0.03, 0.03)
                o = box((bw - 0.04, 0.5, 0.26), (cy, -cx, z) if along_y else (cx, cy, z), stone,
                        rot=(0, 0, (math.pi / 2 if along_y else 0) + rng.uniform(-0.03, 0.03)))
                studio.displace(o, 0.03, 0.2, seed * 100 + r * 10 + int(x * 3))
            x += bw
    if broken:
        for i in range(6):
            o = box((rng.uniform(0.3, 0.6), rng.uniform(0.3, 0.5), 0.3),
                    (rng.uniform(-1.5, 1.5), rng.uniform(-1.4, -0.5), 0.15), stone, rot=(0, 0, rng.uniform(0, 3)))


def pillar(seed=12, broken=False):
    """A weathered stone column on a tall plinth."""
    stone = mat("oldstone", dict(STONE, colour=(0.40, 0.35, 0.27), dark=(0.16, 0.13, 0.09), grime=0.6))
    box((1.9, 1.9, 0.5), (0, 0, 0.25), stone)
    h = 3.0 if broken else 6.0
    box((1.5, 1.5, 1.6), (0, 0, 1.6), stone)  # a tall plinth with a sunk panel
    box((1.1, 0.04, 1.0), (0, -0.76, 1.6), mat("stone_dark", STONE, ))
    # A fluted shaft: a cylinder whose rim alternates in and out.
    bpy.ops.mesh.primitive_cylinder_add(vertices=48, radius=0.55, depth=h, location=(0, 0, 2.4 + h / 2))
    shaft = bpy.context.object
    for v in shaft.data.vertices:
        a = math.atan2(v.co.y, v.co.x)
        k = 0.93 if round(a / (2 * math.pi / 48)) % 2 else 1.0
        v.co.x *= k * (1 - 0.1 * (v.co.z / h + 0.5))
        v.co.y *= k * (1 - 0.1 * (v.co.z / h + 0.5))
    studio.smooth(studio.assign(shaft, stone))
    h += 1.5
    if broken:
        bpy.ops.mesh.primitive_cone_add(vertices=8, radius1=0.42, depth=0.6, location=(0, 0, 0.9 + h - 0.5))
        studio.displace(studio.assign(bpy.context.object, stone), 0.2, 0.3, seed)
    else:
        box((1.2, 1.2, 0.4), (0, 0, 0.9 + h + 0.2), stone)


def standing_stone(seed=13):
    stone = mat("stone", STONE)
    o = box((2.4, 1.8, 9.0), (0, 0, 4.5), stone)
    bm = bmesh.new()
    bm.from_mesh(o.data)
    for v in bm.verts:
        if v.co.z > 0:
            v.co.x *= 0.75
            v.co.y *= 0.8
    bm.to_mesh(o.data)
    studio.displace(o, 0.06, 0.4, seed)


def log(seed=14):
    bark = mat("bark", BARK)
    cut = mat("cut", colour=(0.62, 0.50, 0.34), dark=(0.36, 0.28, 0.18), scale=20, bump=0.3)
    cyl(0.22, 5.0, (0, 0, 0.22), bark, rot=(0, math.radians(90), 0), verts=12)
    for s in (-1, 1):
        cyl(0.21, 0.02, (s * 2.5, 0, 0.22), cut, rot=(0, math.radians(90), 0), verts=12)


def oak(seed=15):
    """A broad leafy tree: a short trunk forking into upward limbs that
    split again, with clusters of small leaves at the twigs, so the crown
    is lumpy and see-through like the birches."""
    rng = random.Random(seed)
    bark = mat("bark", BARK)
    leaves = bmesh.new()
    shade = leaves.faces.layers.float.new("shade")

    def leaf(at):
        s_ = rng.uniform(0.06, 0.1)
        a, b = rng.uniform(0, math.pi), rng.uniform(0, 2 * math.pi)
        u = Vector((math.cos(b), math.sin(b), 0)) * s_
        v = Vector((-math.sin(b) * math.cos(a), math.cos(b) * math.cos(a), math.sin(a))) * s_ * 0.7
        c = Vector(at)
        f = leaves.faces.new([leaves.verts.new(c + u), leaves.verts.new(c + v), leaves.verts.new(c - u), leaves.verts.new(c - v)])
        f[shade] = min(1.0, rng.random() ** 2 + 0.3 * (c.z > 6.0))

    def grow(base, direction, length, radius, depth):
        d = Vector(direction).normalized()
        tip = Vector(base) + d * length
        mid = (Vector(base) + tip) / 2 + Vector((rng.uniform(-0.15, 0.15), rng.uniform(-0.15, 0.15), 0.1))
        studio.assign(branch_curve([tuple(base), tuple(mid), tuple(tip)], radius), bark)
        if depth == 0:
            for _ in range(45):
                leaf(tip + Vector((rng.gauss(0, 0.4), rng.gauss(0, 0.4), rng.gauss(0, 0.28))))
            return
        for k in range(3 if depth > 1 else 2):
            a = rng.uniform(0, 2 * math.pi)
            nd = d * 0.9 + Vector((math.cos(a), math.sin(a), 0)) * rng.uniform(0.5, 0.9) + Vector((0, 0, 0.25))
            grow(tip, nd, length * rng.uniform(0.6, 0.75), radius * 0.6, depth - 1)

    trunk_h = 3.4
    studio.assign(branch_curve([(0, 0, 0), (0.1, 0, trunk_h / 2), (0, 0.1, trunk_h)], 0.38), bark)
    for i in range(4):
        a = i * math.pi / 2 + rng.uniform(-0.4, 0.4)
        grow((0, 0.1, trunk_h), (math.cos(a) * 0.7, math.sin(a) * 0.7, 1), 1.9, 0.22, 3)
    me = bpy.data.meshes.new("leaves")
    leaves.to_mesh(me)
    o = bpy.data.objects.new("leaves", me)
    bpy.context.scene.collection.objects.link(o)
    lm = bpy.data.materials.new("oakleaves")
    lm.use_nodes = True
    nodes, links = lm.node_tree.nodes, lm.node_tree.links
    attr = nodes.new("ShaderNodeAttribute")
    attr.attribute_name = "shade"
    ramp = nodes.new("ShaderNodeValToRGB")
    ramp.color_ramp.elements[0].color = (*studio.linear((0.03, 0.09, 0.03)), 1)
    ramp.color_ramp.elements[1].color = (*studio.linear((0.28, 0.58, 0.20)), 1)
    links.new(attr.outputs["Fac"], ramp.inputs["Fac"])
    links.new(ramp.outputs["Color"], nodes["Principled BSDF"].inputs["Base Color"])
    me.materials.append(lm)
    o.visible_shadow = False


def branch_curve(points, radius):
    curve = bpy.data.curves.new("branch", "CURVE")
    curve.dimensions = "3D"
    curve.bevel_depth = radius
    curve.bevel_resolution = 1
    sp = curve.splines.new("POLY")
    sp.points.add(len(points) - 1)
    for i, p in enumerate(points):
        sp.points[i].co = (*p, 1)
        sp.points[i].radius = 1 - 0.5 * i / (len(points) - 1)
    o = bpy.data.objects.new("branch", curve)
    bpy.context.scene.collection.objects.link(o)
    return o


def stones(seed=16):
    rng = random.Random(seed)
    stone = mat("stone", STONE)
    for i in range(rng.randint(2, 4)):
        o = ball(rng.uniform(0.25, 0.4), (rng.uniform(-0.35, 0.35), rng.uniform(-0.35, 0.35), 0.08), stone,
                 scale=(1, 0.8, 0.6))
        studio.displace(o, 0.06, 0.3, seed * 10 + i)


def tank(seed=17):
    """A copper vat on a timber cradle with a pipe out of its end."""
    copper, timber = mat("copper", COPPER), mat("timber", TIMBER)
    cyl(0.85, 2.6, (0, 0, 1.6), copper, rot=(0, math.radians(90), 0), verts=20)
    for x in (-1.3, 1.3):
        ball(0.85, (x, 0, 1.6), copper, scale=(0.3, 1, 1))
    for x in (-0.6, 0.6):
        cyl(0.9, 0.1, (x, 0, 1.6), mat("iron", IRON), rot=(0, math.radians(90), 0), verts=20)
    for sx in (-1, 1):
        for sy in (-1, 1):
            box((0.16, 0.16, 1.0), (sx * 0.9, sy * 0.6, 0.5), timber)
        box((0.16, 1.4, 0.16), (sx * 0.9, 0, 0.9), timber)
    cyl(0.12, 1.0, (1.8, 0, 1.6), copper, rot=(0, math.radians(90), 0), verts=10)
    cyl(0.12, 1.3, (2.3, 0, 1.0), copper, verts=10)


def resize(k):
    """Scale everything built so far about the origin."""
    for o in bpy.context.scene.objects:
        if o.type in ("MESH", "CURVE") and o.name != "shadow catcher" and o.parent is None:
            o.location = o.location * k
            o.scale = o.scale * k


PROPS = {
    "cottage": (cottage, 640),
    "hall": (lambda: (hall(), resize(1.3)), 900),
    "stall": (stall, 224),
    "crate_yellow": (lambda: crate(4, (0.88, 0.74, 0.14)), 128),
    "crate_red": (lambda: crate(5, (0.75, 0.10, 0.08)), 128),
    "well": (well, 224),
    "cart": (lambda: (cart(), resize(0.8)), 192),
    "barrel": (lambda: (barrel(), resize(0.65)), 48),
    "sacks": (lambda: (sacks(), resize(0.8)), 96),
    "firewood": (lambda: (firewood(), resize(0.6)), 64),
    "signpost": (signpost, 192),
    "wall_x": (ruin_wall, 160),
    "wall_y": (lambda: ruin_wall(11, along_y=True), 160),
    "wall_broken": (lambda: ruin_wall(12, broken=True), 192),
    "pillar": (pillar, 288),
    "pillar_broken": (lambda: pillar(13, broken=True), 192),
    "standing_stone": (standing_stone, 384),
    "log": (log, 160),
    "oak": (lambda: (oak(), resize(0.75)), 256),
    "stones": (stones, 64),
    "tank": (tank, 160),
}


def main():
    args = [a for a in sys.argv[sys.argv.index("--") + 1:]] if "--" in sys.argv else sys.argv[1:]
    out = Path(args[0])
    names = args[1:] or list(PROPS)
    out.mkdir(parents=True, exist_ok=True)
    listing = out / "town.txt"
    anchors = dict(l.split(" ", 1) for l in listing.read_text().split("\n") if l) if listing.exists() else {}
    for name in names:
        build, size = PROPS[name]
        _mats.clear()
        scene = studio.reset(samples=32)
        build()
        studio.camera(scene, size, size, target=(0, 0, size / studio.PIXELS_PER_UNIT * 0.3))
        ax, ay = studio.anchor(scene)
        studio.render(scene, str(out / f"{name}.png"))
        anchors[name] = f"{ax:.0f} {ay:.0f}"
        print(name, "done", flush=True)
    listing.write_text("".join(f"{k} {v}\n" for k, v in anchors.items()))


if __name__ == "__main__":
    main()
