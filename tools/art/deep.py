"""Deep cavern and lava pieces in ShadowFlare's style, rendered with
studio.py: thin walls of crumbling red-brown earth, dark back walls, rail
barriers between earth posts, a chained post, stone stairs, glowing
obelisks, a pit, fallen boulders and pools of lava.

    python tools/art/deep.py [--blend] <output folder> [piece ...]

(run with a Python that has the `bpy` module). Writes one PNG per piece plus
deep.txt listing each file's anchor point, like cave.py. Walls are a little
two tiles long (6.4 Blender units) and about 6.6 units high.
"""

import math
import random
import sys
from pathlib import Path

import bpy
import bmesh  # noqa: E402  (after bpy)

sys.path.insert(0, str(Path(__file__).resolve().parent))
import studio  # noqa: E402
from cave import crags, rough  # noqa: E402
from dungeon import solid, turn  # noqa: E402
from town import IRON, _mats, ball, box, cyl, mat  # noqa: E402

# Red-brown rock, finely grained; the light on its craggy facets (cave.crags)
# brings out the orange.
EARTHWALL = dict(colour=(0.55, 0.30, 0.11), dark=(0.035, 0.018, 0.008), scale=18, detail=16, rough=1.0, bump=1.2,
                 band=(0.36, 0.72), grime=0.6)
# The tall faces and blocks: a little browner.
GORGE = dict(EARTHWALL, colour=(0.46, 0.28, 0.13))
SHADOW = dict(colour=(0.035, 0.022, 0.015), dark=(0.01, 0.006, 0.004), scale=14, rough=1.0, bump=0.6)
RAIL = dict(colour=(0.40, 0.39, 0.36), dark=(0.10, 0.10, 0.09), scale=6, bump=0.6, stretch=(8, 1, 1), grime=0.5)
STONE = dict(colour=(0.30, 0.30, 0.31), dark=(0.07, 0.07, 0.08), scale=6, bump=0.8, grime=0.6,
             bricks=dict(width=0.9, height=0.45, mortar=0.04, vary=0.75, joint=0.3))

WALL_L, WALL_H = 6.4, 6.6
TALL_L, TALL_H = 5.0, 9.6   # the deep caves' tall rock faces
BLOCK = 6.8                 # and their broad blocks


def glow(name, colour, dark, strength, **kw):
    """A noisy material that lights itself, for lava and crystals."""
    m = studio.material(name, colour, dark, **kw)
    p = m.node_tree.nodes["Principled BSDF"]
    p.inputs["Emission Strength"].default_value = strength
    m.node_tree.links.new(p.inputs["Base Color"].links[0].from_socket, p.inputs["Emission Color"])
    return m


def earth_wall(along_y=False, seed=1, dark=False):
    """A thin, crumbling wall of red-brown earth with a ragged foot; dark
    ones are the unlit back walls of a cavern."""
    rng = random.Random(seed)
    m = mat("shadow", SHADOW) if dark else mat("earthwall", EARTHWALL)
    rough(solid(box((WALL_L, 0.6, WALL_H), (0, 0.15, WALL_H / 2), m)), 0.5, 0.9, seed, cuts=26)
    if not dark:
        crags(WALL_L, WALL_H * 0.9, 0.7, m, seed)
    if dark:
        rough(solid(box((WALL_L + 0.2, 0.9, 0.5), (0, 0.15, WALL_H), mat("earthwall", EARTHWALL))), 0.25, 0.5, seed,
              cuts=12)
    else:
        # Earth fallen from the face, heaped along the foot.
        for k in range(4):
            x = rng.uniform(-2.4, 2.4)
            o = rough(solid(box((rng.uniform(0.8, 1.6), 0.7, rng.uniform(0.6, 1.4)), (x, -0.35, 0.3), m)),
                      0.25, 0.5, seed * 10 + k, cuts=6)
            o.rotation_euler.z = rng.uniform(-0.3, 0.3)
    if along_y:
        turn(math.pi / 2)


def earth_tall(along_y=False, seed=11):
    """A tall, thick face of rock at a deep cavern's edge, standing above
    the floor like the side of a gorge, with a ragged lit top."""
    m = mat("gorge", GORGE)
    rough(solid(box((TALL_L, 1.6, TALL_H), (0, 0.3, TALL_H / 2), m)), 0.9, 1.5, seed, cuts=30)
    crags(TALL_L, TALL_H * 0.9, 1.0, m, seed, count=12, at=(0, -0.3, 0))
    rough(solid(box((TALL_L + 0.3, 2.0, 0.8), (0, 0.3, TALL_H), m)), 0.6, 0.9, seed + 1, cuts=14)
    if along_y:
        turn(math.pi / 2)


def earth_block(seed=12, dark=False):
    """A broad block of rock between cavern passages, seen from its corner:
    two earth faces (or, dark, the black drop below the floor) under a
    ragged earth top."""
    m = mat("gorge", GORGE)
    rough(solid(box((BLOCK, BLOCK, 4.2), (0, 0, 2.1), mat("shadow", SHADOW) if dark else m)), 0.8, 1.5, seed, cuts=26)
    if not dark:
        # Crags on the two faces the camera sees.
        crags(BLOCK, 3.6, 0.9, m, seed, count=12, at=(0, -BLOCK / 2, 0))
        crags(BLOCK, 3.6, 0.9, m, seed + 50, count=12, at=(BLOCK / 2, 0, 0), along_y=True)
    rough(solid(box((BLOCK + 0.3, BLOCK + 0.3, 0.8), (0, 0, 4.3), m)), 0.6, 0.9, seed + 1, cuts=18)


def earth_post(seed=3):
    """A short ragged stub of earth wall, for ends and corners."""
    rough(solid(box((1.3, 1.1, WALL_H), (0, 0, WALL_H / 2), mat("earthwall", EARTHWALL))), 0.4, 0.7, seed, cuts=14)


def rail_gate(along_y=False, seed=5):
    """Three weathered grey rails nailed across a gap between earth posts."""
    m, rail = mat("earthwall", EARTHWALL), mat("rail", RAIL)
    for k, x in enumerate((-2.3, 2.3)):
        rough(solid(box((1.0, 1.0, WALL_H - k * 0.6), (x, 0, (WALL_H - k * 0.6) / 2), m)), 0.35, 0.7, seed + k,
              cuts=14)
    for k, z in enumerate((1.5, 2.9, 4.3)):
        box((4.4, 0.22, 0.4), (0, -0.55, z), rail, rot=(0.0, math.radians((-6, 4, -3)[k]), 0))
    if along_y:
        turn(math.pi / 2)


def chain_gate(along_y=False, seed=7):
    """Iron chains slung between two earth posts, caught up on a ring that
    holds a glowing red stone: a sealed way."""
    m, iron = mat("earthwall", EARTHWALL), mat("iron", IRON)
    for k, x in enumerate((-2.3, 2.3)):
        rough(solid(box((1.0, 1.0, WALL_H - k * 0.6), (x, 0, (WALL_H - k * 0.6) / 2), m)), 0.35, 0.7, seed + k,
              cuts=14)
    # Four chains from the posts to the ring in the middle.
    for x0, z0 in ((-1.8, 5.2), (1.8, 4.8), (-1.8, 1.4), (1.8, 1.2)):
        for k in range(11):
            t = k / 10
            ball(0.1, (x0 * (1 - t), -0.3, z0 + (3.2 - z0) * t - 0.4 * math.sin(t * math.pi)), iron,
                 scale=(1.6, 0.6, 1.0) if k % 2 else (1.0, 0.6, 1.6))
    bpy.ops.mesh.primitive_torus_add(major_radius=0.55, minor_radius=0.09, location=(0, -0.3, 3.2),
                                     rotation=(math.pi / 2, 0, 0))
    studio.assign(bpy.context.object, iron)
    ball(0.32, (0, -0.35, 3.2), glow("lamp", (1.0, 0.25, 0.15), (0.5, 0.05, 0.03), 3.0, scale=10))
    if along_y:
        turn(math.pi / 2)


def stone_stairs():
    """A broad flight of dark stone steps between block walls."""
    stone = mat("deepstone", STONE)
    for k in range(11):
        box((4.2, 0.75, 0.6 + k * 0.6), (0, 3.75 - k * 0.75, (0.6 + k * 0.6) / 2), stone)
    # A low parapet down one side, and the face of earth the flight climbs.
    for k in range(5):
        h = 1.4 + k * 1.3
        box((0.7, 1.65, h), (-2.45, 3.3 - k * 1.65, h / 2), stone)
    solid(box((2.0, 1.6, 1.6), (-3.2, 3.4, 0.8), stone))
    rough(solid(box((6.0, 1.4, 7.4), (0, -4.8, 3.7), mat("earthwall", EARTHWALL))), 0.4, 0.8, 3, cuts=16)
    turn(math.pi)


def stone_ledge(corner=False):
    """A low wall of dark stone blocks with a flat coping, straight or bent."""
    stone = mat("deepstone", STONE)
    solid(box((5.6, 0.8, 1.4), (0, 0, 0.7), stone))
    box((5.8, 1.0, 0.2), (0, 0, 1.5), stone)
    if corner:
        solid(box((0.8, 5.6, 1.4), (2.4, 2.4, 0.7), stone))
        box((1.0, 5.8, 0.2), (2.4, 2.4, 1.5), stone)


def obelisk(colour):
    """A slim black stone obelisk with a glowing star set in its face."""
    stone = mat("obelisk", colour=(0.10, 0.09, 0.10), dark=(0.02, 0.02, 0.02), scale=10, rough=0.35, bump=0.4)
    v = [(-0.45, -0.45, 0), (0.45, -0.45, 0), (0.45, 0.45, 0), (-0.45, 0.45, 0),
         (-0.36, -0.36, 4.4), (0.36, -0.36, 4.4), (0.36, 0.36, 4.4), (-0.36, 0.36, 4.4), (0, 0, 4.9)]
    f = [(0, 1, 5, 4), (1, 2, 6, 5), (2, 3, 7, 6), (3, 0, 4, 7), (4, 5, 8), (5, 6, 8), (6, 7, 8), (7, 4, 8), (3, 2, 1, 0)]
    me = bpy.data.meshes.new("obelisk")
    me.from_pydata(v, [], f)
    o = bpy.data.objects.new("obelisk", me)
    bpy.context.collection.objects.link(o)
    studio.assign(o, stone)
    light = glow("star", colour, tuple(c * 0.5 for c in colour), 6.0, scale=8)
    # A four-pointed star on the faces turned to the viewer.
    streak = glow("streak", colour, tuple(c * 0.3 for c in colour), 1.5)
    for face_y, face_x, rot in ((-0.41, 0.0, 0.0), (0.0, 0.41, math.pi / 2)):
        box((0.05, 0.04, 0.9), (face_x, face_y, 2.8), light, rot=(0, 0, rot))
        box((0.45, 0.04, 0.05), (face_x, face_y, 2.8), light, rot=(0, 0, rot))
        ball(0.13, (face_x, face_y, 2.8), light)
        # A long faint streak of light down the stone.
        box((0.04, 0.03, 3.4), (face_x, face_y, 2.3), streak, rot=(0, 0, rot))


def pit(seed=2):
    """A ragged black pit opening in the cavern floor, with an earth lip."""
    m = mat("earthwall", EARTHWALL)
    hole = bpy.data.materials.new("void")
    hole.use_nodes = True
    nodes = hole.node_tree.nodes
    nodes.clear()
    emit = nodes.new("ShaderNodeEmission")
    emit.inputs["Color"].default_value = (0, 0, 0, 1)
    hole.node_tree.links.new(emit.outputs[0], nodes.new("ShaderNodeOutputMaterial").inputs[0])
    # A long gash in the floor. Its far inner wall shows as a band of
    # earth between the rim and the black, wider on the far side; the
    # pieces stay above the ground so the shadow catcher cannot hide them.
    rng = random.Random(seed)
    inner = mat("innerwall", dict(EARTHWALL, colour=(0.20, 0.09, 0.03), dark=(0.02, 0.008, 0.004)))
    o = rough(solid(ball(1.0, (0, 0, 0.01), inner, scale=(6.2, 2.8, 0.02))), 0.25, 0.5, seed, cuts=6)
    o.scale.z = 0.05
    ball(1.0, (0.0, -1.5, 0.04), hole, scale=(5.0, 0.9, 0.02))
    # A crumbly lip of loose earth round the edge.
    for k in range(40):
        a = 2 * math.pi * k / 40
        x, y = 6.2 * rng.uniform(0.97, 1.04) * math.cos(a), 2.8 * rng.uniform(0.97, 1.04) * math.sin(a)
        ball(rng.uniform(0.08, 0.18), (x, y, 0.03), inner, scale=(1.0, 1.0, 0.4))


def boulders(seed=4, ledge=False):
    """A great cracked block of fallen earth and rock; as a ledge, the
    black drop below a cavern's edge with the earth showing on top."""
    m = mat("earthwall", EARTHWALL)
    if ledge:
        rough(solid(box((5.4, 5.4, 3.6), (0, 0, 1.8), mat("shadow", SHADOW))), 0.4, 0.8, seed, cuts=20)
        rough(solid(box((5.6, 5.6, 0.6), (0, 0, 3.7), m)), 0.3, 0.5, seed + 1, cuts=14)
        return
    big = mat("boulder", dict(EARTHWALL, scale=6, detail=16, bump=2.4))
    rough(solid(box((6.6, 6.6, 3.2), (0, 0, 1.6), big)), 0.7, 2.2, seed, cuts=26)
    rng = random.Random(seed)
    for k in range(3):
        o = rough(solid(box((1.2, 1.0, 0.9), (rng.uniform(-2.5, 2.5), -1.5, 0.4), m)), 0.25, 0.5, seed * 10 + k,
                  cuts=6)
        o.rotation_euler.z = rng.uniform(0, math.pi)


def lava(seed=1):
    """A pool of molten rock, glowing yellow to deep red under patches of
    dark crust, with a crusted rim."""
    rng = random.Random(seed)
    melt = glow(f"lava{seed}", (1.0, 0.62, 0.10), (0.42, 0.04, 0.0), 1.3, scale=22, detail=15, band=(0.55, 0.88),
                spot=0.42, spot_colour=(0.05, 0.025, 0.015))
    crust = mat("crust", colour=(0.10, 0.06, 0.04), dark=(0.02, 0.01, 0.01), scale=14, bump=1.4)
    w, d = rng.uniform(7.0, 8.6), rng.uniform(5.6, 7.2)
    lobes = [(rng.uniform(0, 2 * math.pi), rng.uniform(0.04, 0.12), k) for k in (2, 3, 4, 7)]

    def edge(a):
        return 1 + sum(amp * math.sin(k * a + ph) for ph, amp, k in lobes)

    # Broad islands of cooled crust floating on the melt.
    waves = [(rng.uniform(0.9, 2.0), rng.uniform(0, 2 * math.pi), rng.uniform(0, 2 * math.pi)) for _ in range(6)]

    def crusted(x, y):
        v = sum(math.sin(f * (x * math.cos(a) + y * math.sin(a)) + ph) for f, a, ph in waves) / len(waves)
        return v > 0.12 + 0.12 * rng.random()

    # The molten pool: a flat ragged sheet, and a broken rim of crust
    # that thins out into specks on the floor round it.
    bpy.ops.mesh.primitive_grid_add(x_subdivisions=140, y_subdivisions=140, size=2 * max(w, d) * 1.5,
                                    location=(0, 0, 0.02))
    o = bpy.context.object
    o.data.materials.append(melt)
    o.data.materials.append(crust)
    bm = bmesh.new()
    bm.from_mesh(o.data)
    outside = []
    for f in bm.faces:
        c = f.calc_center_median()
        r = math.hypot(c.x / w, c.y / d) / edge(math.atan2(c.y / d, c.x / w))
        if r > 1.0 + rng.uniform(0, 0.35) or (r > 1.0 and rng.random() < 0.55):
            outside.append(f)
        elif r > 0.9 or crusted(c.x, c.y):
            f.material_index = 1
    bmesh.ops.delete(bm, geom=outside, context="FACES")
    bm.to_mesh(o.data)


PROPS = {
    "earth_x": (earth_wall, 288),
    "earth_y": (lambda: earth_wall(along_y=True, seed=2), 288),
    "earth2_x": (lambda: earth_wall(seed=3), 288),
    "earth2_y": (lambda: earth_wall(along_y=True, seed=4), 288),
    "shadow_x": (lambda: earth_wall(dark=True, seed=5), 288),
    "shadow_y": (lambda: earth_wall(along_y=True, dark=True, seed=6), 288),
    "earth_post": (earth_post, 224),
    "earth_tall_x": (earth_tall, 320),
    "earth_tall_y": (lambda: earth_tall(along_y=True, seed=13), 320),
    "earth_block": (earth_block, 352),
    "earth_drop": (lambda: earth_block(seed=14, dark=True), 352),
    "rail_gate_x": (rail_gate, 288),
    "rail_gate_y": (lambda: rail_gate(along_y=True, seed=6), 288),
    "chain_gate_x": (chain_gate, 288),
    "chain_gate_y": (lambda: chain_gate(along_y=True, seed=9), 288),
    "stone_stairs": (stone_stairs, 320),
    "stone_ledge": (stone_ledge, 224),
    "stone_ledge_corner": (lambda: stone_ledge(corner=True), 256),
    "obelisk_red": (lambda: obelisk((1.0, 0.45, 0.45)), 192),
    "obelisk_blue": (lambda: obelisk((0.55, 0.8, 1.0)), 192),
    "pit": (pit, 384),
    "boulders": (boulders, 320),
    "ledge": (lambda: boulders(seed=8, ledge=True), 320),
    "lava_0": (lava, 600),
    "lava_1": (lambda: lava(2), 600),
    "lava_2": (lambda: lava(3), 600),
    "lava_3": (lambda: lava(4), 600),
}


# Drawn at the original's steeper angle for walls (studio.WALL_ELEVATION).
WALLS = ("earth_x", "earth_y", "earth2", "shadow_", "earth_tall", "earth_block", "earth_post", "stone_ledge")


def main():
    args = [a for a in sys.argv[sys.argv.index("--") + 1:]] if "--" in sys.argv else sys.argv[1:]
    blend = "--blend" in args
    args = [a for a in args if a != "--blend"]
    out = Path(args[0])
    names = args[1:] or list(PROPS)
    out.mkdir(parents=True, exist_ok=True)
    listing = out / "deep.txt"
    anchors = dict(l.split(" ", 1) for l in listing.read_text().split("\n") if l) if listing.exists() else {}
    for name in names:
        build, size = PROPS[name]
        _mats.clear()
        scene = studio.reset(samples=32)
        build()
        for m in bpy.data.materials:
            # No glossy sheen: it greys the dark earth under the studio sky.
            if m.node_tree and "Principled BSDF" in m.node_tree.nodes:
                m.node_tree.nodes["Principled BSDF"].inputs["Specular IOR Level"].default_value = 0.0
        studio.camera(scene, size, size, target=(0, 0, size / studio.PIXELS_PER_UNIT * 0.3),
                      elevation=studio.WALL_ELEVATION if name.startswith(WALLS) else None)
        ax, ay = studio.anchor(scene)
        if blend:
            studio.save_blend(out / f"{name}.blend")
        studio.render(scene, str(out / f"{name}.png"))
        anchors[name] = f"{ax:.0f} {ay:.0f}"
        print(name, "done", flush=True)
    listing.write_text("".join(f"{k} {v}\n" for k, v in anchors.items()))


if __name__ == "__main__":
    main()
