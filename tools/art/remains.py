"""Blood, bones, the fallen and beds, in ShadowFlare's style, rendered with
studio.py: splashes of blood on the ground, blood-soaked earth, skeletons
lying in the dirt, a skeleton hung on a wooden cross, dead fighters among
their broken weapons, wooden beds under grey blankets (made, rumpled,
broken, one with its sleeper long dead) and a wrecked cart.

    python tools/art/remains.py [--blend] <output folder> [piece ...]

(run with a Python that has the `bpy` module). Writes one PNG per piece plus
remains.txt listing each file's anchor point, like deep.py; add_props.py
puts them on the props sheet and content/skin.txt draws them in place of
the original's blood00, deadbody00, jimen_x00, hone and ex_obj pieces. Our
own models, made from scratch to the original's sizes and colours.
"""

import math
import random
import sys
from pathlib import Path

import bpy
import bmesh  # noqa: E402  (after bpy)
from mathutils import Euler, Vector  # noqa: E402

sys.path.insert(0, str(Path(__file__).resolve().parent))
import studio  # noqa: E402
from town import _mats, ball, box, cyl, mat  # noqa: E402

BLOOD = dict(colour=(0.26, 0.015, 0.01), dark=(0.09, 0.0, 0.0), scale=10, rough=0.5, bump=0.2)
DRIED = dict(colour=(0.22, 0.03, 0.015), dark=(0.08, 0.01, 0.0), scale=14, rough=0.8, bump=0.3)
SOIL = dict(colour=(0.24, 0.14, 0.07), dark=(0.07, 0.05, 0.03), scale=16, detail=10, bump=1.0, grime=0.5)
DIRT = dict(colour=(0.40, 0.29, 0.17), dark=(0.10, 0.07, 0.04), scale=16, detail=10, bump=1.0, grime=0.5)
BONE = dict(colour=(0.84, 0.79, 0.66), dark=(0.42, 0.37, 0.28), scale=12, bump=0.4, grime=0.3)
WOOD = dict(colour=(0.42, 0.27, 0.15), dark=(0.13, 0.08, 0.04), scale=10, bump=0.6, stretch=(6, 1, 1), grime=0.4)
TIMBER = dict(WOOD, colour=(0.30, 0.22, 0.14), stretch=(1, 1, 6))
ROPE = dict(colour=(0.55, 0.47, 0.32), dark=(0.25, 0.20, 0.12), scale=30, bump=0.6)
BLANKET = dict(colour=(0.55, 0.57, 0.55), dark=(0.30, 0.32, 0.31), scale=7, detail=8, bump=0.5, grime=0.4)
STRAW = dict(colour=(0.58, 0.48, 0.30), dark=(0.30, 0.24, 0.13), scale=40, bump=0.8)
SKIN = dict(colour=(0.62, 0.45, 0.35), dark=(0.36, 0.24, 0.18), scale=8, bump=0.3)
LEATHER = dict(colour=(0.40, 0.25, 0.14), dark=(0.15, 0.09, 0.05), scale=12, bump=0.5, grime=0.5)
CLOTH = dict(colour=(0.36, 0.30, 0.24), dark=(0.14, 0.11, 0.09), scale=9, bump=0.4, grime=0.5)
MAIL = dict(colour=(0.45, 0.45, 0.46), dark=(0.16, 0.16, 0.17), scale=60, rough=0.45, bump=1.0)
STEEL = dict(colour=(0.62, 0.63, 0.66), dark=(0.28, 0.28, 0.30), scale=20, rough=0.3, bump=0.2)
IRON = dict(colour=(0.22, 0.21, 0.20), dark=(0.06, 0.06, 0.06), scale=14, rough=0.5, bump=0.4)
HAIR = dict(colour=(0.18, 0.12, 0.07), dark=(0.06, 0.04, 0.02), scale=30, bump=0.5)


def flat(r, at, m, sx=1.0, sy=1.0, h=0.02, turnz=0.0):
    """A flattened blob lying on the ground: a puddle or a smear."""
    o = ball(r, at, m, scale=(sx, sy, h / r))
    o.rotation_euler = (0, 0, turnz)
    return o


def splash(rng, size=1.0, wet=True, pool=True):
    """Blood thrown on the ground: a pool, smaller blots round it and drops
    flung out along a few lines."""
    m = mat("blood" if wet else "dried", BLOOD if wet else DRIED)
    if pool:
        for _ in range(rng.randint(6, 10)):
            a, d = rng.uniform(0, 2 * math.pi), rng.uniform(0, 0.7) * size
            flat(rng.uniform(0.2, 0.45) * size, (d * math.cos(a), d * math.sin(a), 0.01), m,
                 rng.uniform(0.8, 1.5), rng.uniform(0.6, 1.0), turnz=rng.uniform(0, math.pi))
    for _ in range(rng.randint(3, 6)):
        a = rng.uniform(0, 2 * math.pi)
        d = rng.uniform(0.3, 1.2) * size
        for k in range(rng.randint(3, 8)):
            dd = d + k * rng.uniform(0.15, 0.35) * size
            r = max(0.04, rng.uniform(0.08, 0.22) * size * (1 - k / 10))
            x, y = dd * math.cos(a) + rng.gauss(0, 0.06), dd * math.sin(a) + rng.gauss(0, 0.06)
            flat(r, (x, y, 0.01), m, rng.uniform(1.0, 2.2), 1.0, turnz=a)


def clods(rng, radius, count, m):
    for _ in range(count):
        a, d = rng.uniform(0, 2 * math.pi), radius * math.sqrt(rng.random())
        ball(rng.uniform(0.03, 0.08), (d * math.cos(a), d * math.sin(a), 0.02), m, scale=(1, 1, 0.5))


def blood(seed):
    rng = random.Random(seed)
    splash(rng, size=rng.uniform(1.6, 2.1), pool=seed % 4 != 0)


def soaked_earth(seed):
    """A wide patch of earth soaked dark with old blood."""
    rng = random.Random(seed)
    dirt = mat("soil", SOIL)
    dried = mat("dried", DRIED)
    for _ in range(rng.randint(5, 8)):
        a, d = rng.uniform(0, 2 * math.pi), rng.uniform(0, 1.5)
        flat(rng.uniform(0.7, 1.3), (d * math.cos(a), d * math.sin(a), 0.0), dirt, rng.uniform(1, 1.6), rng.uniform(0.7, 1),
             h=0.008, turnz=rng.uniform(0, math.pi))
    for _ in range(rng.randint(4, 7)):
        a, d = rng.uniform(0, 2 * math.pi), rng.uniform(0, 1.3)
        flat(rng.uniform(0.5, 1.0), (d * math.cos(a), d * math.sin(a), 0.02), dried, rng.uniform(1, 1.6), rng.uniform(0.6, 1),
             h=0.03, turnz=rng.uniform(0, math.pi))
    clods(rng, 2.2, 70, dirt)
    clods(rng, 1.8, 260, dried)
    splash(rng, size=0.7, wet=True, pool=False)


def bone_rod(a, b, r, m):
    """A long bone from a to b, knobbed at both ends."""
    a, b = Vector(a), Vector(b)
    d = b - a
    o = cyl(r, d.length, (a + b) / 2, m, verts=6)
    o.rotation_euler = d.to_track_quat("Z", "Y").to_euler()
    for p in (a, b):
        ball(r * 1.6, p, m, scale=(1, 1, 0.8))


def skeleton(rng, x=0.0, y=0.0, z=0.0, turnz=0.0, scatter=0.0, sit=0.0):
    """A skeleton on its back, head toward -x before turning by turnz;
    scatter throws its bones about, sit raises the chest (in radians)."""
    bone = mat("bone", BONE)
    parts = []

    def j(v):
        return Vector((v[0] + rng.gauss(0, scatter), v[1] + rng.gauss(0, scatter), v[2]))

    parts.append(ball(0.28, j((-1.75, 0, 0.26)), bone, scale=(1.1, 0.9, 0.95)))  # skull
    parts.append(ball(0.16, j((-1.55, 0, 0.12)), bone, scale=(0.8, 1.2, 0.6)))  # jaw
    spine = cyl(0.05, 1.5, (-0.7, 0, 0.12), bone, verts=6, rot=(0, math.pi / 2, 0))
    parts.append(spine)
    for k in range(5):
        bpy.ops.mesh.primitive_torus_add(major_radius=0.30 - 0.02 * k, minor_radius=0.035,
                                         location=j((-1.25 + 0.14 * k, 0, 0.18)), rotation=(0, math.pi / 2, 0))
        o = studio.assign(bpy.context.object, bone)
        o.scale = (1, 1, 0.55)
        parts.append(o)
    parts.append(ball(0.26, j((0.05, 0, 0.12)), bone, scale=(0.6, 1.25, 0.45)))  # hips
    for side in (-1, 1):
        sh = j((-1.3, side * 0.36, 0.1))
        el = j((-0.55, side * (0.5 + rng.uniform(0, 0.35)), 0.08))
        ha = j((0.1, side * (0.45 + rng.uniform(0, 0.5)), 0.06))
        bone_rod(sh, el, 0.045, bone)
        bone_rod(el, ha, 0.038, bone)
        hip = j((0.1, side * 0.22, 0.1))
        kn = j((0.95, side * (0.28 + rng.uniform(0, 0.3)), 0.1))
        ft = j((1.85, side * (0.3 + rng.uniform(0, 0.45)), 0.07))
        bone_rod(hip, kn, 0.06, bone)
        bone_rod(kn, ft, 0.05, bone)
        ball(0.09, ft + Vector((0.12, 0, -0.02)), bone, scale=(1.6, 0.8, 0.5))
    pivot = gather(bpy.context.scene.objects, exclude=set())
    pivot.rotation_euler = (0, -sit, turnz)
    pivot.location = (x, y, z)
    return pivot


def gather(objects, exclude):
    """Parent every mesh made since the last gather to a new empty."""
    bpy.ops.object.empty_add(location=(0, 0, 0))
    pivot = bpy.context.object
    for o in list(objects):
        if o is pivot or o.parent or o.type != "MESH" or o.name == "shadow catcher" or o.get("gathered"):
            continue
        o.parent = pivot
        o["gathered"] = True
    pivot["gathered"] = True
    return pivot


def bones(seed):
    """A skeleton in a scuff of loose earth; some lie apart."""
    rng = random.Random(seed)
    dirt = mat("dirt", DIRT)
    for _ in range(4):
        a, d = rng.uniform(0, 2 * math.pi), rng.uniform(0, 0.8)
        flat(rng.uniform(0.8, 1.2), (d * math.cos(a), d * math.sin(a), 0.0), dirt, 1.4, 1.0, h=0.03,
             turnz=rng.uniform(0, math.pi))
    gather(bpy.context.scene.objects, set())
    skeleton(rng, turnz=rng.uniform(0, 2 * math.pi), scatter=0.12 * (seed % 3), sit=0.0)
    clods(rng, 1.8, 90, dirt)


def cross(seed):
    """A skeleton bound to a rough wooden cross, its arms tied along the
    beam and its legs hanging, the skull dropped to one side."""
    rng = random.Random(seed)
    timber, rope, bone = mat("timber", TIMBER), mat("rope", ROPE), mat("bone", BONE)
    box((0.32, 0.3, 6.4), (0, 0, 3.2), timber, rot=(0, rng.uniform(-0.04, 0.04), 0))
    box((0.26, 3.4, 0.26), (0.0, 0, 5.0), timber, rot=(rng.uniform(-0.06, 0.06), 0, 0))
    z = 5.0
    ball(0.27, (0.25, rng.choice((-0.15, 0.15)), z + 0.45), bone, scale=(0.95, 0.9, 1.1))
    for k in range(5):
        bpy.ops.mesh.primitive_torus_add(major_radius=0.28 - 0.02 * k, minor_radius=0.035,
                                         location=(0.28, 0, z - 0.25 - 0.17 * k))
        studio.assign(bpy.context.object, bone).scale = (0.8, 1, 0.6)
    cyl(0.05, 1.3, (0.2, 0, z - 0.6), bone, verts=6)
    ball(0.24, (0.25, 0, z - 1.35), bone, scale=(0.5, 1.2, 0.6))
    for side in (-1, 1):
        bone_rod((0.22, side * 0.35, z - 0.15), (0.2, side * 1.0, z + 0.05), 0.045, bone)
        bone_rod((0.2, side * 1.0, z + 0.05), (0.2, side * 1.6, z + 0.1), 0.038, bone)
        cyl(0.16, 0.18, (0.0, side * 1.2, z), rope, rot=(math.pi / 2, 0, 0))
        dangle = rng.uniform(-0.1, 0.15)
        bone_rod((0.25, side * 0.2, z - 1.4), (0.3 + dangle, side * 0.28, z - 2.3), 0.055, bone)
        bone_rod((0.3 + dangle, side * 0.28, z - 2.3), (0.25, side * 0.22, z - 3.2), 0.05, bone)
    cyl(0.2, 0.16, (0.05, 0, z - 2.9), rope)
    clods(rng, 0.9, 40, mat("dirt", DIRT))


def body(rng, x=0.0, y=0.0, turnz=0.0, armour="leather"):
    """A fallen fighter face down, limbs flung out."""
    top = mat(armour, {"leather": LEATHER, "mail": MAIL, "cloth": CLOTH}[armour])
    skin, cloth, hair = mat("skin", SKIN), mat("cloth", CLOTH), mat("hair", HAIR)
    ball(0.42, (0, 0, 0.32), top, scale=(1.5, 1.0, 0.6))  # chest and back
    ball(0.36, (0.75, 0, 0.26), cloth, scale=(1.0, 1.0, 0.65))  # hips
    ball(0.24, (-0.85, rng.uniform(-0.1, 0.1), 0.28), skin, scale=(1.1, 0.9, 0.95))  # head
    ball(0.24, (-0.88, 0, 0.36), hair, scale=(1.0, 0.9, 0.6))
    for side in (-1, 1):
        sh = Vector((-0.3, side * 0.45, 0.3))
        el = Vector((-0.5 + rng.uniform(-0.5, 0.5), side * (0.9 + rng.uniform(0, 0.3)), 0.14))
        ha = el + Vector((rng.uniform(-0.6, 0.4), side * rng.uniform(0.2, 0.6), -0.04))
        limb(sh, el, 0.13, top)
        limb(el, ha, 0.1, skin)
        ball(0.11, ha, skin)
        hip = Vector((0.9, side * 0.2, 0.25))
        kn = Vector((1.5, side * (0.3 + rng.uniform(0, 0.4)), 0.16))
        ft = kn + Vector((0.65, side * rng.uniform(0, 0.4), -0.03))
        limb(hip, kn, 0.17, cloth)
        limb(kn, ft, 0.13, mat("boot", LEATHER))
    pivot = gather(bpy.context.scene.objects, set())
    pivot.rotation_euler = (0, 0, turnz)
    pivot.location = (x, y, 0)


def limb(a, b, r, m):
    d = b - a
    o = cyl(r, d.length + r, (a + b) / 2, m, verts=10)
    o.rotation_euler = d.to_track_quat("Z", "Y").to_euler()
    o.scale = (1, 1, 1)


def sword(at, turnz, m_blade, m_grip):
    x, y, z = at
    o = box((2.0, 0.16, 0.04), (x, y, z + 0.04), m_blade, rot=(0, 0, turnz))
    g = box((0.08, 0.6, 0.08), (x - math.cos(turnz) * 1.0, y - math.sin(turnz) * 1.0, z + 0.06), m_grip, rot=(0, 0, turnz))
    h = box((0.45, 0.08, 0.08), (x - math.cos(turnz) * 1.25, y - math.sin(turnz) * 1.25, z + 0.06), m_grip,
            rot=(0, 0, turnz))
    return o, g, h


def axe(at, turnz, m_head, m_haft):
    x, y, z = at
    box((2.2, 0.09, 0.09), (x, y, z + 0.06), m_haft, rot=(0, 0, turnz))
    hx, hy = x + math.cos(turnz) * 1.0, y + math.sin(turnz) * 1.0
    box((0.25, 0.65, 0.05), (hx + math.sin(turnz) * 0.2, hy - math.cos(turnz) * 0.2, z + 0.06), m_head,
        rot=(0, 0, turnz))


def spear(at, turnz, tilt, m_head, m_haft):
    """A spear or arrow standing in the ground (or the fallen), leaning."""
    x, y, z = at
    length = 3.0
    rot = Euler((0, tilt, turnz))
    shaft = cyl(0.05, length, (0, 0, 0), m_haft, verts=6)
    tip = cyl(0.1, 0.4, (0, 0, -length / 2 - 0.1), m_head, verts=6, r2=0.0)
    tip.rotation_euler = (math.pi, 0, 0)
    for o in (shaft, tip):
        v = Vector(o.location) + Vector((0, 0, length / 2 - 0.3))
        v.rotate(rot)
        o.location = v + Vector((x, y, z))
        o.rotation_euler = (Euler((rot.x, rot.y, rot.z)).to_matrix() @ o.rotation_euler.to_matrix()).to_euler()


def fallen(seed):
    """A dead fighter in a pool of blood, weapons dropped and spears or
    arrows standing in them; some variants are two."""
    rng = random.Random(seed)
    splash(rng, size=1.2)
    gather(bpy.context.scene.objects, set())
    body(rng, turnz=rng.uniform(0, 2 * math.pi), armour=rng.choice(("leather", "mail", "cloth")))
    steel, wood = mat("steel", STEEL), mat("wood", WOOD)
    for _ in range(rng.randint(1, 3)):
        a, d = rng.uniform(0, 2 * math.pi), rng.uniform(0.8, 1.8)
        at = (d * math.cos(a), d * math.sin(a), 0.0)
        kind = rng.random()
        if kind < 0.45:
            sword(at, rng.uniform(0, 2 * math.pi), steel, mat("grip", LEATHER))
        elif kind < 0.75:
            axe(at, rng.uniform(0, 2 * math.pi), mat("iron", IRON), wood)
        else:
            bpy.ops.mesh.primitive_cylinder_add(vertices=20, radius=0.6, depth=0.1, location=(at[0], at[1], 0.08),
                                                rotation=(rng.uniform(-0.2, 0.2), rng.uniform(-0.2, 0.2), 0))
            studio.assign(bpy.context.object, wood)
            cyl(0.15, 0.14, (at[0], at[1], 0.12), mat("iron", IRON))
    for _ in range(rng.randint(0, 3)):
        a, d = rng.uniform(0, 2 * math.pi), rng.uniform(0, 0.6)
        spear((d * math.cos(a), d * math.sin(a), 0.2), rng.uniform(0, 2 * math.pi), rng.uniform(0.2, 0.7), steel, wood)


def bed(seed, state="made", sleeper=False):
    """A plain wooden bed, a straw mattress and a grey blanket. state:
    made, rumpled (the blanket thrown half off), broken (the frame fallen
    in) or wrecked (the bare frame in pieces)."""
    rng = random.Random(seed)
    wood, straw, cloth = mat("wood", WOOD), mat("straw", STRAW), mat("blanket", BLANKET)
    L, W, H = 4.0, 2.2, 0.75
    sag = 0.0
    if state in ("broken", "wrecked"):
        sag = 0.4
    for sy in (-1, 1):
        box((L, 0.18, 0.35), (0, sy * W / 2, H - sag * 0.5), wood,
            rot=(0, rng.uniform(-0.12, 0.12) if sag else 0, rng.uniform(-0.05, 0.05) if sag else 0))
    for sx in (-1, 1):
        for sy in (-1, 1):
            if state == "wrecked" and rng.random() < 0.5:
                continue
            box((0.2, 0.2, H + (0.6 if sx < 0 else 0.25)), (sx * L / 2, sy * W / 2, (H + (0.6 if sx < 0 else 0.25)) / 2),
                wood)
    box((0.12, W, 0.9), (-L / 2, 0, H + 0.2), wood)  # headboard
    box((0.12, W, 0.4), (L / 2, 0, H + 0.05), wood, rot=(rng.uniform(-0.3, 0.3) if sag else 0, 0, 0))
    if state == "wrecked":
        for _ in range(5):
            box((rng.uniform(1.0, 2.6), 0.2, 0.1), (rng.uniform(-1.5, 1.5), rng.uniform(-1.5, 1.5), 0.08), wood,
                rot=(0, 0, rng.uniform(0, math.pi)))
        return
    box((L - 0.2, W - 0.15, 0.3), (0, 0, H + 0.1 - sag), straw)
    ball(0.45, (-L / 2 + 0.6, 0, H + 0.35 - sag), mat("pillow", BLANKET), scale=(0.8, 1.7, 0.45))
    if sleeper:
        gather(bpy.context.scene.objects, set())
        skeleton(rng, x=-0.1, z=H + 0.3 - sag, turnz=0.0, scatter=0.03)
    drape(rng, L, W, H + 0.3 - sag, cloth, cover=0.0 if state == "made" else rng.uniform(0.3, 0.55),
          slide=0.5 if state == "rumpled" else 0.0, sleeper=sleeper)


def drape(rng, L, W, top, m, cover=0.0, slide=0.0, sleeper=False):
    """The blanket: a cloth over the mattress, hanging down the sides,
    folded back off the head end by `cover` and pulled to one side by
    `slide`; it rises over a sleeper."""
    nx, ny = 40, 26
    x0 = -L / 2 + 0.5 + cover * L
    width = W + 1.8
    bpy.ops.mesh.primitive_grid_add(x_subdivisions=nx, y_subdivisions=ny, size=1, location=(0, 0, 0))
    o = bpy.context.object
    o.data.materials.append(m)
    bm = bmesh.new()
    bm.from_mesh(o.data)
    for v in bm.verts:
        u, w = v.co.x + 0.5, v.co.y + 0.5
        x = x0 + u * (L / 2 + 0.2 - x0)
        y = (w - 0.5) * width + slide
        z = top
        over = abs(y) - W / 2
        if over > 0:
            # Hang down over the edge.
            z = top - over * 1.4
            y = math.copysign(W / 2 + over * 0.25, y)
        if x > L / 2:
            z -= (x - L / 2) * 1.2
        if sleeper and abs(y) < 0.6 and -1.5 < x < 2.0:
            z += 0.25 * math.cos(y / 0.6 * math.pi / 2)
        z += 0.06 * math.sin(u * 17 + rng.uniform(0, 0.3)) * math.sin(w * 11) + rng.uniform(-0.02, 0.02)
        v.co = (x, y, max(z, 0.05))
    bm.to_mesh(o.data)
    bm.free()
    studio.smooth(o)
    o.modifiers.new("thick", "SOLIDIFY").thickness = 0.05


def wreck(seed):
    """A cart smashed to pieces: a tipped bed of planks, its wheels fallen
    off and spokes and boards scattered."""
    rng = random.Random(seed)
    wood, iron = mat("wood", WOOD), mat("iron", IRON)
    box((3.4, 1.9, 0.12), (0.2, 0, 0.7), wood, rot=(0.5, 0.25, 0.3))
    box((3.2, 0.1, 0.6), (0.4, 1.0, 0.5), wood, rot=(0.3, 0.2, 0.3))
    for k, (x, y, tilt) in enumerate(((-1.4, 1.6, 1.3), (1.8, -1.2, 0.2), (-0.8, -1.8, 1.45))):
        rot = (tilt, 0, rng.uniform(0, math.pi))
        z = 1.0 if tilt < 0.5 else 0.14
        cyl(0.2, 0.3, (x, y, z), wood, rot=rot, verts=12)  # hub
        for s in range(8):
            a = s * math.pi / 4 + rng.uniform(-0.05, 0.05)
            spoke = Vector((0.5 * math.cos(a), 0.5 * math.sin(a), 0))
            spoke.rotate(Euler(rot))
            o = box((0.9, 0.07, 0.07), (x + spoke.x, y + spoke.y, z + spoke.z), wood)
            o.rotation_euler = (Euler(rot).to_matrix() @ Euler((0, 0, a)).to_matrix()).to_euler()
        bpy.ops.mesh.primitive_torus_add(major_radius=1.0, minor_radius=0.07, location=(x, y, 1.0 if tilt < 0.5 else 0.14),
                                         rotation=rot)
        studio.assign(bpy.context.object, iron)
    for _ in range(14):
        box((rng.uniform(0.6, 2.4), rng.uniform(0.12, 0.3), 0.08),
            (rng.uniform(-2.6, 2.6), rng.uniform(-2.6, 2.6), 0.06), wood, rot=(0, rng.uniform(-0.2, 0.2), rng.uniform(0, math.pi)))
    box((3.6, 0.12, 0.12), (1.4, 0.8, 0.9), wood, rot=(0, -0.35, 0.6))


PROPS = {
    **{f"blood_{c}": ((lambda s=s: blood(s)), 192) for s, c in enumerate("abcdefg", 1)},
    **{f"soaked_earth_{c}": ((lambda s=s: (soaked_earth(s), resize(1.35))), 256) for s, c in enumerate("abcde", 11)},
    **{f"bones_{c}": ((lambda s=s: bones(s)), 192) for s, c in enumerate("abcdef", 21)},
    "bones_cross": (lambda: (cross(31), resize(0.95)), 192),
    **{f"fallen_{c}": ((lambda s=s: (fallen(s), resize(0.95))), 192) for s, c in enumerate("abcdefgh", 41)},
    "bed": (lambda: (bed(51), resize(1.15), turn(-math.pi / 2)), 192),
    "bed_rumpled": (lambda: (bed(52, "rumpled"), resize(1.15), turn(-math.pi / 2)), 192),
    "bed_broken": (lambda: (bed(53, "broken"), resize(1.15), turn(-math.pi / 2)), 192),
    "bed_wrecked": (lambda: (bed(54, "wrecked"), resize(1.15), turn(-math.pi / 2)), 192),
    "bed_dead": (lambda: (bed(55, "rumpled", sleeper=True), resize(1.15), turn(-math.pi / 2)), 192),
    "cart_wreck": (lambda: (wreck(61), resize(1.7)), 320),
}


def resize(k):
    """Scale everything built so far about the origin."""
    for o in bpy.context.scene.objects:
        if o.type in ("MESH", "CURVE", "EMPTY") and o.name != "shadow catcher" and o.parent is None:
            o.location = o.location * k
            o.scale = o.scale * k


def turn(angle):
    """Turn everything built so far about the upright through the origin."""
    rot = Euler((0, 0, angle))
    for o in bpy.context.scene.objects:
        if o.type in ("MESH", "CURVE", "EMPTY") and o.name != "shadow catcher" and o.parent is None:
            v = Vector(o.location)
            v.rotate(rot)
            o.location = v
            o.rotation_euler = (rot.to_matrix() @ o.rotation_euler.to_matrix()).to_euler()


def main():
    args = [a for a in sys.argv[sys.argv.index("--") + 1:]] if "--" in sys.argv else sys.argv[1:]
    blend = "--blend" in args
    args = [a for a in args if a != "--blend"]
    out = Path(args[0])
    names = args[1:] or list(PROPS)
    out.mkdir(parents=True, exist_ok=True)
    listing = out / "remains.txt"
    anchors = dict(l.split(" ", 1) for l in listing.read_text().split("\n") if l) if listing.exists() else {}
    for name in names:
        build, size = PROPS[name]
        _mats.clear()
        scene = studio.reset(samples=32)
        build()
        for m in bpy.data.materials:
            # No glossy sheen: it greys the blood and the dark earth.
            if m.node_tree and "Principled BSDF" in m.node_tree.nodes:
                m.node_tree.nodes["Principled BSDF"].inputs["Specular IOR Level"].default_value = 0.0
        studio.camera(scene, size, size, target=(0, 0, 0.6))
        ax, ay = studio.anchor(scene)
        if blend:
            studio.save_blend(out / f"{name}.blend")
        studio.render(scene, str(out / f"{name}.png"))
        anchors[name] = f"{ax:.0f} {ay:.0f}"
        print(name, "done", flush=True)
    listing.write_text("".join(f"{k} {v}\n" for k, v in anchors.items()))


if __name__ == "__main__":
    main()
