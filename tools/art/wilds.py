"""Pieces for the third area's open country in ShadowFlare's style,
rendered with studio.py: broadleaf, conifer, willow, fruit and dead trees
in several colourings, palms and cycads, boulders, flowers, grass and
reeds.

    python tools/art/wilds.py [--blend] <output folder> [piece ...]

(run with a Python that has the `bpy` module). Writes one PNG per piece plus
wilds.txt listing each file's anchor point, like fort.py.
"""

import math
import random
import sys
from pathlib import Path

import bpy
import bmesh  # noqa: E402  (bmesh needs bpy loaded first)
from mathutils import Vector

sys.path.insert(0, str(Path(__file__).resolve().parent))
import studio  # noqa: E402
from cave import ROCK, rough  # noqa: E402
from dungeon import DARK, solid  # noqa: E402
from fort import BLACKIRON, BRASS  # noqa: E402
from props import WOOD, branch  # noqa: E402
from town import COPPER, MOSSY, _mats, ball, box, cyl, mat, resize  # noqa: E402

BARK = dict(colour=(0.36, 0.26, 0.24), dark=(0.10, 0.07, 0.06), scale=14, bump=0.6)
GREYBARK = dict(colour=(0.40, 0.36, 0.34), dark=(0.12, 0.10, 0.09), scale=14, bump=0.6)
PALMBARK = dict(colour=(0.40, 0.26, 0.18), dark=(0.10, 0.06, 0.04), scale=24, bump=1.0, stretch=(1, 1, 6))

# Leaf colourings: (shadow, middle, highlight), sRGB.
GREEN = ((0.03, 0.07, 0.03), (0.16, 0.30, 0.13), (0.50, 0.62, 0.40))
DARKGREEN = ((0.02, 0.05, 0.02), (0.10, 0.22, 0.09), (0.30, 0.46, 0.22))
AUTUMN = ((0.07, 0.03, 0.01), (0.40, 0.22, 0.06), (0.85, 0.60, 0.28))
BLOSSOM = ((0.06, 0.06, 0.06), (0.34, 0.35, 0.34), (0.80, 0.81, 0.80))
GOLD = ((0.08, 0.06, 0.01), (0.42, 0.36, 0.10), (0.80, 0.70, 0.34))
BLUE = ((0.01, 0.05, 0.05), (0.10, 0.28, 0.28), (0.44, 0.66, 0.64))
WILLOW = ((0.03, 0.04, 0.05), (0.16, 0.21, 0.26), (0.50, 0.58, 0.62))
PALM = ((0.06, 0.14, 0.03), (0.32, 0.56, 0.16), (0.70, 0.88, 0.44))
DARKPALM = ((0.03, 0.08, 0.02), (0.20, 0.40, 0.10), (0.58, 0.78, 0.34))
FERN = ((0.08, 0.12, 0.04), (0.38, 0.48, 0.22), (0.76, 0.82, 0.54))


class Leaves:
    """Small leaf quads gathered into one mesh, each with its own shade
    (0 shadow to 1 highlight) for a speckled, see-through crown."""

    def __init__(self, rng, size=(0.06, 0.1)):
        self.rng, self.size = rng, size
        self.bm = bmesh.new()
        self.shade = self.bm.faces.layers.float.new("shade")

    def leaf(self, at, light=0.0, size=None):
        rng = self.rng
        s = rng.uniform(*(size or self.size))
        a, b = rng.uniform(0, math.pi), rng.uniform(0, 2 * math.pi)
        u = Vector((math.cos(b), math.sin(b), 0)) * s
        v = Vector((-math.sin(b) * math.cos(a), math.cos(b) * math.cos(a), math.sin(a))) * s * 0.7
        c = Vector(at)
        verts = [self.bm.verts.new(c + u), self.bm.verts.new(c + v), self.bm.verts.new(c - u), self.bm.verts.new(c - v)]
        self.bm.faces.new(verts)[self.shade] = min(1.0, rng.random() ** 2 + light)

    def strip(self, a, b, width, light=0.0):
        """A long thin leaf or leaflet from a to b."""
        a, b = Vector(a), Vector(b)
        side = (b - a).cross(Vector((0, 0, 1)))
        side = side.normalized() * width / 2 if side.length > 1e-6 else Vector((width / 2, 0, 0))
        mid = (a + b) / 2
        verts = [self.bm.verts.new(a), self.bm.verts.new(mid + side), self.bm.verts.new(b), self.bm.verts.new(mid - side)]
        self.bm.faces.new(verts)[self.shade] = min(1.0, self.rng.random() * 0.6 + light)

    def finish(self, palette, name="leaves"):
        me = bpy.data.meshes.new(name)
        self.bm.to_mesh(me)
        obj = bpy.data.objects.new(name, me)
        bpy.context.scene.collection.objects.link(obj)
        obj.visible_shadow = False
        m = bpy.data.materials.new(name)
        m.use_nodes = True
        nodes, links = m.node_tree.nodes, m.node_tree.links
        nodes["Principled BSDF"].inputs["Roughness"].default_value = 0.8
        attr = nodes.new("ShaderNodeAttribute")
        attr.attribute_name = "shade"
        ramp = nodes.new("ShaderNodeValToRGB")
        els = ramp.color_ramp.elements
        els[0].color = (*studio.linear(palette[0]), 1)
        els[1].position = 1.0
        els[1].color = (*studio.linear(palette[2]), 1)
        mid = els.new(0.45)
        mid.color = (*studio.linear(palette[1]), 1)
        links.new(attr.outputs["Fac"], ramp.inputs["Fac"])
        links.new(ramp.outputs["Color"], nodes["Principled BSDF"].inputs["Base Color"])
        me.materials.append(m)
        return obj


def fruit(points, colour=(0.85, 0.30, 0.06)):
    m = mat("fruit", dict(colour=colour, dark=tuple(c * 0.5 for c in colour), scale=30, rough=0.4))
    for p in points:
        bpy.ops.mesh.primitive_uv_sphere_add(segments=8, ring_count=6, radius=0.11, location=p)
        studio.assign(bpy.context.object, m)


def broadleaf(seed, palette, height=4.0, reach=2.0, cluster=110, spread=0.55, bark=BARK, leaves=True,
              lean=0.0, depth=3, fruits=0):
    """A spreading tree: a trunk that forks into upward limbs, splitting
    again, with clusters of small leaves at the twigs."""
    rng = random.Random(seed)
    bk = mat("bark", bark)
    lv = Leaves(rng, (0.10, 0.16))
    tips = []

    def grow(base, direction, length, radius, d):
        dirn = Vector(direction).normalized()
        tip = Vector(base) + dirn * length
        mid = (Vector(base) + tip) / 2 + Vector((rng.uniform(-0.15, 0.15), rng.uniform(-0.15, 0.15), 0.08))
        branch([tuple(base), tuple(mid), tuple(tip)], radius, bk)
        if d == 0:
            tips.append(tip)
            if leaves:
                for _ in range(cluster):
                    lv.leaf(tip + Vector((rng.gauss(0, spread), rng.gauss(0, spread), rng.gauss(0, spread * 0.7))),
                            0.25 * (tip.z > height + reach * 0.8))
            return
        for _ in range(3 if d > 1 else 2):
            a = rng.uniform(0, 2 * math.pi)
            nd = dirn * 0.9 + Vector((math.cos(a), math.sin(a), 0)) * rng.uniform(0.5, 0.9) + Vector((0, 0, 0.2))
            grow(tip, nd, length * rng.uniform(0.6, 0.75), radius * 0.6, d - 1)

    top = (lean, 0.1, height)
    branch([(0, 0, 0), (lean * 0.3, 0, height / 2), top], 0.30, bk)
    n = 4
    for i in range(n):
        a = i * 2 * math.pi / n + rng.uniform(-0.4, 0.4)
        grow(top, (math.cos(a) * 0.75, math.sin(a) * 0.75, 1), reach, 0.18, depth)
    if leaves:
        lv.finish(palette)
    if fruits:
        fruit([t + Vector((rng.gauss(0, 0.3), rng.gauss(0, 0.3), rng.gauss(0, 0.2))) for t in rng.sample(tips, min(fruits, len(tips)))])


def conifer(seed, palette, height=10.0, base=1.6, width=2.6, per=66, fruits=0, droop=0.25):
    """A tall cone of whorled limbs, shorter towards the top, each with
    leaves along it, so the outline is a ragged Christmas tree."""
    rng = random.Random(seed)
    bk = mat("bark", BARK)
    lv = Leaves(rng, (0.10, 0.16))
    branch([(0, 0, 0), (0, 0, height * 0.5), (0, 0, height)], 0.22, bk)
    n = int((height - base) / 0.38)
    on = []
    for i in range(n):
        z = base + (height - base) * i / n
        k = (z - base) / (height - base)
        reach = width * (1 - k) ** 0.9 + 0.3
        for j in range(3):
            a = i * 2.4 + j * 2.1 + rng.uniform(-0.3, 0.3)
            d = Vector((math.cos(a), math.sin(a), 0))
            pts = [Vector((0, 0, z)) + d * reach * t + Vector((0, 0, reach * (0.25 * t - droop * t * t))) for t in (0, 0.2, 0.4, 0.6, 0.8, 1)]
            branch([tuple(p) for p in pts], 0.05, bk)
            for p in pts[1:]:
                for _ in range(per // 4):
                    q = p + Vector((rng.gauss(0, 0.26), rng.gauss(0, 0.26), rng.gauss(0, 0.16)))
                    lv.leaf(q, 0.3 * (d.x < 0) * rng.random())
                    on.append(q)
    for _ in range(120):
        lv.leaf((rng.gauss(0, 0.25), rng.gauss(0, 0.2), height + rng.uniform(-0.6, 0.2)), 0.2)
    lv.finish(palette)
    if fruits:
        fruit(rng.sample(on, fruits))


def willow(seed, palette, height=4.2):
    """A weeping willow: limbs rising from a stout trunk and arching
    over, with long strands of leaves hanging to near the ground."""
    rng = random.Random(seed)
    bk = mat("bark", BARK)
    lv = Leaves(rng, (0.07, 0.12))
    branch([(0, 0, 0), (0.1, 0, height / 2), (0, 0.1, height)], 0.42, bk)
    for i in range(12):
        a = i * 2 * math.pi / 12 + rng.uniform(-0.3, 0.3)
        d = Vector((math.cos(a), math.sin(a), 0))
        reach = rng.uniform(2.8, 3.6)
        up = rng.uniform(3.6, 4.6)
        pts = [Vector((0, 0.1, height)) + d * reach * t + Vector((0, 0, up * (1.6 * t - t * t))) for t in (0, 0.25, 0.5, 0.75, 1.0)]
        branch([tuple(p) for p in pts], 0.12, bk)
        for p in pts[1:]:
            for _ in range(40):
                lv.leaf(p + Vector((rng.gauss(0, 0.4), rng.gauss(0, 0.4), rng.gauss(0, 0.3))), 0.3)
            for _ in range(9):
                start = p + Vector((rng.gauss(0, 0.3), rng.gauss(0, 0.3), 0))
                drop = rng.uniform(0.5, 0.9) * start.z
                out = d * rng.uniform(0.3, 0.9)
                strand = [start + out * (s / 10) ** 0.5 + Vector((0, 0, -drop * s / 10)) for s in range(11)]
                branch([tuple(q) for q in strand], 0.012, bk)
                for q in strand[1:]:
                    for _ in range(5):
                        lv.leaf(q + Vector((rng.gauss(0, 0.06), rng.gauss(0, 0.06), rng.gauss(0, 0.1))),
                                0.3 * (q.z > height + 1.5))
    lv.finish(palette)


def palm(seed, palette, height=7.0, fronds=11, length=3.0, bend=0.6, trunk=0.22, rise=0.8):
    """A palm: a ringed trunk curving up to a head of arching fronds,
    each a midrib with pairs of narrow leaflets."""
    rng = random.Random(seed)
    bk = mat("palmbark", PALMBARK)
    lv = Leaves(rng)
    pts = [Vector((bend * (i / 8) ** 2, 0, height * i / 8)) for i in range(9)]
    branch([tuple(p) for p in pts], trunk, bk)
    for i in range(1, 8):
        bpy.ops.mesh.primitive_torus_add(major_radius=trunk * (1 - 0.6 * i / 8), minor_radius=0.03,
                                         location=pts[i])
        studio.assign(bpy.context.object, bk)
    crown(rng, lv, pts[-1], fronds, length, rise, leaflet=0.9, width=0.16, droop=0.3, hang=0.2, forward=0.8)
    lv.finish(palette)


def crown(rng, lv, top, fronds, length, rise, leaflet=0.55, width=0.06, droop=0.45, hang=0.55, forward=0.6):
    rib = mat("rib", dict(colour=(0.30, 0.34, 0.12), dark=(0.10, 0.12, 0.04), scale=10))
    for i in range(fronds):
        a = i * 2.4 + rng.uniform(-0.2, 0.2)
        d = Vector((math.cos(a), math.sin(a), 0))
        up = rise * rng.uniform(0.5, 1.2)
        L = length * rng.uniform(0.8, 1.1)
        # A quadratic curve: steeply up from the crown, out, and down to
        # end `droop` below the crown (relative to the length).
        c0 = Vector(top)
        c1 = c0 + d * L * 0.45 + Vector((0, 0, L * up * 0.5))
        c2 = c0 + d * L + Vector((0, 0, -L * droop))
        spine = [c0 * (1 - t) ** 2 + c1 * 2 * t * (1 - t) + c2 * t * t for t in [s / 24 for s in range(25)]]
        branch([tuple(p) for p in spine], 0.03, rib)
        side = d.cross(Vector((0, 0, 1)))
        for k, p in enumerate(spine[1:], 1):
            # Leaflets angle forward along the frond and droop (or lift)
            # a little, in pairs, longest a little before the middle.
            along = (p - spine[k - 1]).normalized()
            f = 1 - abs(k / 24 - 0.45) * 1.2
            for s in (-1, 1):
                for j in range(3):
                    tip = p + (side * s + along * forward + Vector((0, 0, -hang))).normalized() * leaflet * max(0.25, f)
                    lv.strip(p + along * 0.04 * j, tip + Vector((rng.gauss(0, 0.04), rng.gauss(0, 0.04), rng.gauss(0, 0.04))),
                             width, 0.25 * (d.x < 0.2))


def cycad(seed, palette, height=0.9, fronds=12, length=3.2):
    """A squat, scaly trunk with a rosette of long fronds."""
    rng = random.Random(seed)
    bk = mat("palmbark", PALMBARK)
    lv = Leaves(rng)
    bpy.ops.mesh.primitive_cone_add(vertices=12, radius1=0.6, radius2=0.42, depth=height, location=(0, 0, height / 2))
    trunk = bpy.context.object
    studio.displace(trunk, 0.08, 0.1, seed)
    studio.assign(trunk, bk)
    crown(rng, lv, (0, 0, height), fronds, length, 1.1, leaflet=0.7, width=0.22, droop=0.0, hang=0.0, forward=1.6)
    lv.finish(palette)


GREYROCK = dict(colour=(0.48, 0.48, 0.44), dark=(0.08, 0.08, 0.08), scale=16, detail=12, bump=1.6, grime=0.7,
                spot=0.15, spot_colour=(0.36, 0.40, 0.20))
BROWNROCK = dict(colour=(0.62, 0.36, 0.18), dark=(0.10, 0.05, 0.02), scale=16, detail=12, bump=1.6, grime=0.6)
CLIFF = dict(colour=(0.46, 0.29, 0.15), dark=(0.09, 0.055, 0.03), scale=30, detail=15, bump=2.4, grime=0.6,
             band=(0.3, 0.8), spot=0.18, spot_colour=(0.80, 0.58, 0.32))
MOSSBRICK = dict(colour=(0.42, 0.42, 0.30), dark=(0.05, 0.05, 0.03), scale=6, detail=12, bump=1.6, grime=0.6,
                 spot=0.5, spot_colour=(0.34, 0.40, 0.12),
                 bricks=dict(width=0.5, height=0.22, mortar=0.05, vary=0.6, joint=0.12))
MOSSWOOD = dict(colour=(0.20, 0.20, 0.16), dark=(0.03, 0.03, 0.03), scale=12, detail=10, bump=1.4, stretch=(1, 1, 0.3),
                spot=0.42, spot_colour=(0.36, 0.44, 0.16))
DISCSTONE = dict(colour=(0.50, 0.50, 0.48), dark=(0.12, 0.12, 0.12), scale=24, detail=10, bump=1.0, grime=0.5)
EARTH = dict(colour=(0.30, 0.20, 0.11), dark=(0.05, 0.035, 0.02), scale=30, detail=14, bump=1.6, grime=0.6)
WATER = dict(colour=(0.18, 0.42, 0.72), dark=(0.04, 0.12, 0.30), scale=40, detail=10, rough=0.3, bump=0.8)
RUSTCOPPER = dict(colour=(0.50, 0.30, 0.18), dark=(0.10, 0.05, 0.03), scale=24, detail=10, rough=0.4, bump=0.6, grime=0.6,
                  spot=0.25, spot_colour=(0.20, 0.12, 0.08))
MESH = dict(colour=(0.36, 0.28, 0.18), dark=(0.04, 0.03, 0.02), scale=3, rough=0.6, bump=1.0,
            bricks=dict(width=0.12, height=0.12, mortar=0.25, vary=0.9, joint=0.05))


def boulder(seed, spec, size=1.0, n=1):
    """One big stone, or a few, roughly lumpy."""
    rng = random.Random(seed)
    m = mat("rock", spec)
    for i in range(n):
        r = size * (1 if i == 0 else rng.uniform(0.3, 0.5))
        at = (0, 0, 0) if i == 0 else (rng.uniform(-1, 1) * size, rng.uniform(-1, 1) * size, 0)
        bpy.ops.mesh.primitive_ico_sphere_add(subdivisions=4, radius=r, location=(at[0], at[1], r * 0.35))
        o = bpy.context.object
        o.scale = (1, rng.uniform(0.75, 0.95), rng.uniform(0.6, 0.8))
        o.rotation_euler = (0, 0, rng.uniform(0, 6.3))
        studio.displace(o, 1.4 * r, 0.6 * r, seed * 10 + i)
        studio.smooth(studio.assign(o, m))


def plants(seed, kind):
    """A clump of flowers, tall grass, or reeds in a small pool."""
    rng = random.Random(seed)
    stem = mat("stem", dict(colour=(0.26, 0.40, 0.12), dark=(0.08, 0.14, 0.03), scale=20))
    lv = Leaves(rng)
    palette = {"grass": GREEN, "reeds": GREEN}.get(kind, DARKGREEN)
    if kind == "reeds":
        bpy.ops.mesh.primitive_grid_add(x_subdivisions=40, y_subdivisions=40, size=3.0, location=(0, 0, 0.02))
        pool = bpy.context.object
        pool.scale = (1.15, 0.85, 1)
        # Keep a ragged blob of the grid: drop faces outside a noisy radius.
        bm = bmesh.new()
        bm.from_mesh(pool.data)
        drop = [f for f in bm.faces if f.calc_center_median().length * (1 + 0.25 * math.sin(7 * math.atan2(*f.calc_center_median().yx)) + rng.uniform(-0.15, 0.15)) > 1.3]
        bmesh.ops.delete(bm, geom=drop, context="FACES")
        bm.to_mesh(pool.data)
        studio.assign(pool, mat("water", WATER))
    n = {"grass": 8, "reeds": 10}.get(kind, 5)
    for _ in range(n):
        x, y = rng.gauss(0, 0.35), rng.gauss(0, 0.35)
        h = rng.uniform(0.9, 1.6) if kind in ("grass", "reeds") else rng.uniform(0.45, 0.8)
        lean = Vector((rng.gauss(0, 0.15), rng.gauss(0, 0.15), 0))
        top = Vector((x, y, h)) + lean
        if kind in ("grass", "reeds"):
            for _ in range(3):
                lv.strip((x, y, 0), tuple(top + Vector((rng.gauss(0, 0.25), rng.gauss(0, 0.25), rng.uniform(-0.4, 0)))),
                         0.12, 0.4 * rng.random())
            continue
        branch([(x, y, 0), tuple((Vector((x, y, 0)) + top) / 2), tuple(top)], 0.025, stem)
        for _ in range(4):
            b = Vector((x, y, rng.uniform(0.1, 0.5) * h))
            lv.strip(tuple(b), tuple(b + Vector((rng.gauss(0, 0.35), rng.gauss(0, 0.35), 0.3))), 0.12, 0.2)
        colour = (0.95, 0.80, 0.10) if kind == "flowers_yellow" else (0.80, 0.10, 0.08)
        m = mat("petal", dict(colour=colour, dark=tuple(c * 0.45 for c in colour), scale=30, rough=0.6))
        for _ in range(3):
            bpy.ops.mesh.primitive_uv_sphere_add(segments=8, ring_count=5, radius=0.12,
                                                 location=top + Vector((rng.gauss(0, 0.1), rng.gauss(0, 0.1), rng.gauss(0, 0.1))))
            studio.assign(bpy.context.object, m)
    lv.finish(palette)


def stakes(seed, along_y=False):
    """A row of sharpened, weathered stakes of uneven height, one tile long."""
    rng = random.Random(seed)
    wood = mat("stake", dict(WOOD, colour=(0.46, 0.32, 0.18), dark=(0.08, 0.05, 0.03)))
    n = 10
    for i in range(n):
        t = (i + 0.5) / n * 5.0 - 2.5
        h = rng.uniform(4.0, 5.6)
        r = rng.uniform(0.15, 0.2)
        x, y = (rng.uniform(-0.1, 0.1), -t) if along_y else (t, rng.uniform(-0.1, 0.1))
        tilt = (rng.uniform(-0.05, 0.05), rng.uniform(-0.05, 0.05), 0)
        cyl(r, h, (x, y, h / 2), wood, rot=tilt, verts=8)
        bpy.ops.mesh.primitive_cone_add(vertices=8, radius1=r, depth=0.5, location=(x, y, h + 0.25), rotation=tilt)
        studio.assign(bpy.context.object, wood)


def mossy_post(seed=1):
    """An old wooden post furred with moss."""
    m = mat("mosswood", MOSSWOOD)
    o = cyl(0.7, 5.0, (0, 0, 2.5), m, verts=14)
    studio.displace(o, 0.08, 0.3, seed)
    studio.smooth(o)


def ruin_wall(seed, along_y=False, broken=False):
    """A low, mossy brick wall one tile long, or a tumble of its bricks."""
    rng = random.Random(seed)
    m = mat("mossbrick", MOSSBRICK)
    if not broken:
        solid(box((4.0, 0.6, 2.6), (0, 0, 1.3), m))
    else:
        for i in range(7):
            h = 2.2 * (1 - i / 7) + rng.uniform(-0.2, 0.2)
            box((0.5, 0.4, max(0.3, h)), (-1.6 + i * 0.5, 0, max(0.3, h) / 2), m, rot=(0, 0, rng.uniform(-0.1, 0.1)))
        for _ in range(9):
            box((0.4, 0.25, 0.2), (rng.uniform(-1.8, 2.0), rng.uniform(-1.0, 1.0), 0.1), m, rot=(0, 0, rng.uniform(0, 3)))
    if along_y:
        for o in bpy.context.scene.objects:
            if o.type == "MESH" and o.name != "shadow catcher":
                o.location = Vector((o.location.y, -o.location.x, o.location.z))
                o.rotation_euler.z += math.pi / 2


def rune_disc(worn=False):
    """A broad, round stone seal set in the ground: a raised rim, a ring
    of carved marks and a brass star at the centre."""
    stone = mat("discstone", dict(DISCSTONE, colour=(0.36, 0.28, 0.20), spot=0.5, spot_colour=(0.22, 0.16, 0.10), grime=0.9)
                if worn else DISCSTONE)
    brass = mat("brass", dict(BRASS, colour=(0.46, 0.40, 0.24)) if worn else BRASS)
    dark = mat("discdark", dict(DISCSTONE, colour=(0.22, 0.22, 0.22), dark=(0.06, 0.06, 0.06)))
    cyl(3.9, 0.3, (0, 0, 0.15), stone, verts=48)
    # A broad raised band carrying the marks, then a sunken dark field.
    cyl(3.85, 0.25, (0, 0, 0.4), stone, verts=48, r2=3.7)
    cyl(2.6, 0.3, (0, 0, 0.42), dark, verts=48)
    for r in (3.7, 2.6):
        bpy.ops.mesh.primitive_torus_add(major_radius=r, minor_radius=0.1, location=(0, 0, 0.55))
        studio.assign(bpy.context.object, stone)
    rng = random.Random(5)
    for i in range(20):
        a = i * 2 * math.pi / 20
        c = Vector((3.15 * math.cos(a), 3.15 * math.sin(a), 0.55))
        t = Vector((-math.sin(a), math.cos(a), 0))
        # Each mark is two or three strokes, a made-up angular script.
        for k in range(rng.randint(2, 3)):
            ang = a + rng.choice((0, 0.5, -0.5, math.pi / 2))
            box((0.12, 0.6, 0.08), tuple(c + t * rng.uniform(-0.18, 0.18)), brass, rot=(0, 0, ang))
    for i in range(6):
        a = i * math.pi / 3
        box((3.6, 0.22, 0.12), (0, 0, 0.6), brass, rot=(0, 0, a))
    for i in range(6):
        a = i * math.pi / 3 + math.pi / 6
        box((0.6, 0.6, 0.12), (1.3 * math.cos(a), 1.3 * math.sin(a), 0.6), brass, rot=(0, 0, a + math.pi / 4))
    cyl(0.7, 0.2, (0, 0, 0.62), brass, verts=16)


def copper_tank():
    """A great copper storage tank on squat feet, with a stepped lid,
    ladders, finned flues and pipes out at the base."""
    cu, iron = mat("rustcopper", RUSTCOPPER), mat("blackiron", BLACKIRON)
    R, H = 5.6, 9.0
    cyl(R, H, (0, 0, H / 2 + 0.8), cu, verts=40)
    for z in (2.5, 5.0, 7.5):
        bpy.ops.mesh.primitive_torus_add(major_radius=R + 0.05, minor_radius=0.09, location=(0, 0, z))
        studio.assign(bpy.context.object, iron)
    ball(R, (0, 0, H + 0.8), cu, scale=(1, 1, 0.22))
    cyl(3.2, 0.7, (0, 0, H + 2.0), cu, verts=32)
    cyl(2.2, 0.6, (0, 0, H + 2.6), cu, verts=32)
    for i in range(4):
        a = math.pi / 4 + i * math.pi / 2
        x, y = (R - 0.4) * math.cos(a), (R - 0.4) * math.sin(a)
        ball(1.4, (x, y, 1.0), cu, scale=(1, 1, 0.8))
        pipe_stand(at=(x * 1.35, y * 1.35), rot=a)
    for i in range(5):
        a = math.radians(150 + i * 14)
        x, y = (R + 0.6) * math.cos(a), (R + 0.6) * math.sin(a)
        h = H + 3.5 - i * 0.6
        cyl(0.22, h, (x, y, h / 2), iron, verts=10)
        for k in range(4):
            cyl(0.5, 0.12, (x, y, h - 0.2 - k * 0.3), cu, verts=14)
    for a in (math.radians(-60), math.radians(-30)):
        for k in range(20):
            z = 1 + k * 0.45
            box((0.08, 0.7, 0.06), ((R + 0.15) * math.cos(a), (R + 0.15) * math.sin(a), z), iron, rot=(0, 0, a))
    for i in range(4):
        x = -0.9 + i * 0.6
        cyl(0.18, 3.0, (R * 0.7 + x * 0.3, -R * 0.7 + x, 1.5), mat("pipe", COPPER), verts=10)


def pipe_stand(at=(0, 0), rot=0.0, n=1):
    """A ribbed copper drum lying on an iron trestle; n drums in a row."""
    cu, iron = mat("rustcopper", RUSTCOPPER), mat("blackiron", BLACKIRON)
    d = Vector((math.cos(rot), math.sin(rot), 0))
    side = Vector((-d.y, d.x, 0))
    base = Vector((at[0], at[1], 0))
    for k in range(n):
        c = base + d * (k * 1.5)
        cyl(0.55, 1.5, tuple(c + Vector((0, 0, 1.1))), cu, rot=(math.pi / 2, 0, rot + math.pi / 2), verts=16)
        for j in (-0.5, 0, 0.5):
            p = c + d * j
            bpy.ops.mesh.primitive_torus_add(major_radius=0.58, minor_radius=0.06, location=tuple(p + Vector((0, 0, 1.1))),
                                             rotation=(math.pi / 2, 0, rot + math.pi / 2))
            studio.assign(bpy.context.object, cu)
        for sx in (-0.55, 0.55):
            for sy in (-0.5, 0.5):
                q = c + d * sx + side * sy
                cyl(0.05, 1.0, tuple(q + Vector((0, 0, 0.5))), iron, verts=6)
        box((1.4, 1.1, 0.06), tuple(c + Vector((0, 0, 0.6))), mat("mesh", MESH), rot=(0, 0, rot))


def engine():
    """A squat engine: a lattice housing at the front, a tall copper
    drum and a slimmer iron one behind, small cylinders to one side and a
    big elbow pipe down into the ground."""
    cu = mat("rustcopper", RUSTCOPPER)
    iron = mat("darkbronze", dict(RUSTCOPPER, colour=(0.26, 0.21, 0.15)))
    frame = mat("bronze", dict(RUSTCOPPER, colour=(0.36, 0.28, 0.18)))
    cx, cy = 0.2, -1.5
    box((2.2, 2.2, 2.0), (cx, cy, 1.0), mat("hole", DARK))
    for k in range(8):
        t = -1.0 + k * 0.29
        for sgn in (-1, 1):
            for face in (-1.12, 1.12):
                box((0.06, 0.06, 2.0), (cx + t, cy + face, 1.0), frame, rot=(0, sgn * 0.6, 0))
                box((0.06, 0.06, 2.0), (cx + face, cy + t, 1.0), frame, rot=(sgn * 0.6, 0, 0))
    for z in (0.1, 2.05):
        box((2.5, 2.5, 0.18), (cx, cy, z), frame)
    cyl(1.4, 5.4, (-0.6, 0.6, 2.7), cu, verts=24)
    cyl(1.6, 0.4, (-0.6, 0.6, 5.5), cu, verts=24)
    cyl(1.0, 0.5, (-0.6, 0.6, 5.8), mat("hole", DARK), verts=20)
    cyl(0.85, 4.0, (-1.6, -1.0, 2.0 + 2.0), iron, verts=20)
    ball(0.85, (-1.6, -1.0, 6.0), iron)
    for x, y in ((0.8, 2.0), (1.4, 1.4)):
        cyl(0.35, 2.2, (x, y, 1.1), cu, verts=12)
        cyl(0.4, 0.15, (x, y, 2.2), iron, verts=12)
    cyl(0.55, 2.2, (1.3, 0.0, 2.0), iron, rot=(0, math.pi / 2, 0), verts=16)
    ball(0.62, (2.4, 0.0, 2.0), iron)
    cyl(0.55, 2.0, (2.4, 0.0, 1.0), iron, verts=16)
    for x in (0.8, 1.6):
        cyl(0.65, 0.18, (x, 0.0, 2.0), frame, rot=(0, math.pi / 2, 0), verts=16)


def iron_fence(along_y=False, post=False):
    """Wrought-iron railings with spear points, one tile long, or one square post."""
    iron = mat("blackiron", BLACKIRON)
    if post:
        box((0.35, 0.35, 4.8), (0, 0, 2.4), iron)
        box((0.5, 0.5, 0.2), (0, 0, 4.85), iron)
        return
    for i in range(8):
        t = -1.4 + i * 0.4
        cyl(0.08, 4.2, (t, 0, 2.1), iron, verts=6)
        bpy.ops.mesh.primitive_cone_add(vertices=4, radius1=0.16, depth=0.45, location=(t, 0, 4.4))
        studio.assign(bpy.context.object, iron)
    for z in (0.4, 2.0, 3.6):
        box((3.3, 0.14, 0.14), (0, 0, z), iron)
    for i in range(4):
        x = -1.2 + i * 0.8
        for sgn in (-1, 1):
            box((0.08, 0.08, 1.1), (x, 0, 2.8), iron, rot=(0, sgn * 0.6, 0))
    if along_y:
        for o in bpy.context.scene.objects:
            if o.type == "MESH" and o.name != "shadow catcher":
                o.location = Vector((o.location.y, -o.location.x, o.location.z))
                o.rotation_euler.z += math.pi / 2


def cliff(seed, kind="x"):
    """A sheer face of brown rock two tiles long and about 7 units high,
    its top edge ragged; "corner" turns through a right angle."""
    rock = mat("cliff", CLIFF)
    parts = {"x": [((0, 0, 0), 0)], "y": [((0, 0, 0), math.pi / 2)],
             "corner": [((-1.6, 1.6, 0), 0), ((1.6, -1.6, 0), math.pi / 2)]}[kind]
    for k, (at, rot) in enumerate(parts):
        L = 8.0 if kind != "corner" else 4.4
        o = rough(solid(box((L, 1.6, 7.6), (0, 0, 3.8), rock)), 0.9, 1.2, seed * 7 + k, cuts=30)
        o.rotation_euler.z = rot
        o.location = Vector(at) + Vector((0, 0, 3.8))


def cave_mouth():
    """A dark opening in a low mound of earth."""
    earth = mat("mound", dict(EARTH, colour=(0.17, 0.12, 0.04), dark=(0.04, 0.03, 0.01), grime=0.0))
    # A ragged patch of loose earth, heaped a little towards the opening.
    rng = random.Random(3)
    bpy.ops.mesh.primitive_grid_add(x_subdivisions=160, y_subdivisions=160, size=13.0, location=(0, 0, 0.02))
    o = bpy.context.object
    bm = bmesh.new()
    bm.from_mesh(o.data)
    drop = []
    for f in bm.faces:
        c = f.calc_center_median()
        r = math.hypot(c.x / 1.3, c.y) * (1 + 0.2 * math.sin(5 * math.atan2(c.y, c.x)) + rng.uniform(-0.25, 0.25))
        if r > 5.2:
            drop.append(f)
    bmesh.ops.delete(bm, geom=drop, context="FACES")
    for v in bm.verts:
        v.co.z = max(0.0, 0.9 - 0.2 * math.hypot(v.co.x / 1.3, v.co.y)) * (1 + rng.uniform(-0.2, 0.2))
    bm.to_mesh(o.data)
    studio.assign(o, earth)
    # A long dark crevice with ragged lips of rock.
    # The opening itself is unlit black, so no sky light greys it.
    hole = bpy.data.materials.new("hole")
    hole.use_nodes = True
    nodes = hole.node_tree.nodes
    nodes.clear()
    emit = nodes.new("ShaderNodeEmission")
    emit.inputs["Color"].default_value = (0, 0, 0, 1)
    hole.node_tree.links.new(emit.outputs[0], nodes.new("ShaderNodeOutputMaterial").inputs[0])
    ball(3.0, (0.2, 0.2, 0.75), hole, scale=(1.0, 0.36, 0.22))
    for sgn in (-1, 1):
        bpy.ops.mesh.primitive_ico_sphere_add(subdivisions=5, radius=2.5, location=(0.2 - sgn * 0.2, 0.2 + sgn * 1.25, 0.9))
        lip = bpy.context.object
        lip.scale = (1.25, 0.3, 0.45)
        studio.displace(lip, 1.2, 0.3, 4 + sgn)
        studio.assign(lip, mat("cliff", CLIFF))


def paving(seed=1):
    """A patch of worn flagstones."""
    rng = random.Random(seed)
    m = mat("flag", dict(DISCSTONE, colour=(0.50, 0.44, 0.36), dark=(0.10, 0.08, 0.06)))
    for i in range(7):
        for j in range(5):
            if rng.random() < 0.08:
                continue
            o = box((0.5, rng.uniform(0.45, 0.6), 0.14), (-1.6 + i * 0.52 + 0.26 * (j % 2), -1.0 + j * 0.52, 0.07), m,
                    rot=(0, 0, rng.gauss(0, 0.06)))
            studio.displace(o, 0.03, 0.1, seed * 100 + i * 7 + j)


PROPS = {
    "tree_broad": (lambda: broadleaf(1, DARKGREEN, height=2.8, reach=1.8, lean=0.3), 224),
    "tree_autumn": (lambda: broadleaf(2, AUTUMN, height=5.4, reach=1.8, cluster=80), 288),
    "tree_blossom": (lambda: broadleaf(3, BLOSSOM, height=5.6, reach=1.9, cluster=80, bark=GREYBARK), 288),
    "tree_dead": (lambda: broadleaf(4, GREEN, height=3.2, reach=1.9, leaves=False, depth=4), 192),
    "conifer_green": (lambda: conifer(5, DARKGREEN, height=10.0, width=1.7, base=3.0), 224),
    "conifer_gold": (lambda: conifer(6, GOLD, height=9.0, width=2.2, base=2.6), 224),
    "conifer_blue": (lambda: conifer(7, BLUE, height=11.0, width=2.6, base=3.2), 256),
    "tree_fruit": (lambda: conifer(8, DARKGREEN, height=10.0, width=2.0, base=3.0, fruits=14), 224),
    "willow": (lambda: willow(9, WILLOW), 256),
    "palm_tall": (lambda: palm(10, PALM, height=7.2, fronds=14, length=2.6, rise=1.6, trunk=0.3), 224),
    "palm_low": (lambda: palm(11, DARKPALM, height=2.0, fronds=14, length=3.4, bend=0.1, trunk=0.45, rise=2.4), 256),
    "cycad": (lambda: cycad(12, FERN), 256),
    "boulder": (lambda: boulder(13, GREYROCK, 2.5), 160),
    "boulder_brown": (lambda: boulder(14, BROWNROCK, 1.3), 96),
    "stone_small": (lambda: boulder(15, GREYROCK, 0.7), 64),
    "flowers_yellow": (lambda: plants(16, "flowers_yellow"), 64),
    "flowers_red": (lambda: plants(17, "flowers_red"), 64),
    "grass_tall": (lambda: plants(18, "grass"), 64),
    "reeds": (lambda: plants(19, "reeds"), 96),
    "stakes_x": (lambda: stakes(20), 192),
    "stakes_y": (lambda: stakes(21, along_y=True), 192),
    "mossy_post": (mossy_post, 128),
    "ruin_wall_x": (lambda: ruin_wall(22), 128),
    "ruin_wall_y": (lambda: ruin_wall(23, along_y=True), 128),
    "ruin_rubble": (lambda: ruin_wall(24, broken=True), 160),
    "rune_disc": (rune_disc, 192),
    "rune_disc_worn": (lambda: rune_disc(worn=True), 192),
    "copper_tank": (copper_tank, 448),
    "pipe_stand": (pipe_stand, 64),
    "pipe_stand_long": (lambda: pipe_stand(n=3, rot=-math.pi / 2), 128),
    "engine": (engine, 192),
    "iron_fence_x": (iron_fence, 128),
    "iron_fence_y": (lambda: iron_fence(along_y=True), 128),
    "fence_post": (lambda: iron_fence(post=True), 64),
    "cliff_x": (lambda: cliff(25), 256),
    "cliff_y": (lambda: cliff(26, "y"), 256),
    "cliff_corner": (lambda: cliff(27, "corner"), 256),
    "paving": (paving, 128),
    "cave_mouth": (cave_mouth, 352),
}


def main():
    args = [a for a in sys.argv[sys.argv.index("--") + 1:]] if "--" in sys.argv else sys.argv[1:]
    blend = "--blend" in args
    args = [a for a in args if a != "--blend"]
    out = Path(args[0])
    names = args[1:] or list(PROPS)
    out.mkdir(parents=True, exist_ok=True)
    listing = out / "wilds.txt"
    anchors = dict(l.split(" ", 1) for l in listing.read_text().split("\n") if l) if listing.exists() else {}
    for name in names:
        build, size = PROPS[name]
        _mats.clear()
        scene = studio.reset(samples=32)
        build()
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
