"""Dungeon pieces in ShadowFlare's style, rendered with studio.py: the old
waterworks under the first area, with mossy block walls, copper pipes,
iron gates and floor hatches.

    python tools/art/dungeon.py [--blend] <output folder> [piece ...]

(run with a Python that has the `bpy` module). Writes one PNG per piece plus
dungeon.txt listing each file's anchor point, like town.py. Walls are two
tiles long (6.4 Blender units) and about 5.9 units high, so a corridor is
a little taller than two heroes.
"""

import math
import random
import sys
from pathlib import Path

import bpy

sys.path.insert(0, str(Path(__file__).resolve().parent))
import studio  # noqa: E402
from town import IRON, _mats, ball, box, cyl, mat, mesh, resize  # noqa: E402

# Wall boxes have their scale applied (see solid()), so these sizes are in
# Blender units: blocks about 0.8 x 0.45.
# Dingy brick, like the floor's but greyer: small courses with dark joints.
BLOCKS = dict(colour=(0.86, 0.77, 0.57), dark=(0.10, 0.08, 0.05), scale=30, detail=16, bump=1.0, grime=0.55,
              band=(0.30, 0.70), bricks=dict(width=0.75, height=0.32, mortar=0.04, vary=0.65, joint=0.3),
              spot=0.25, spot_colour=(0.45, 0.42, 0.32))
CAP = dict(colour=(0.48, 0.44, 0.26), dark=(0.14, 0.13, 0.07), scale=8, bump=0.8, grime=0.5,
           spot=0.4, spot_colour=(0.32, 0.38, 0.14))
OLDCOPPER = dict(colour=(0.58, 0.34, 0.17), dark=(0.16, 0.08, 0.04), scale=16, rough=0.4, bump=0.6,
                 band=(0.35, 0.7), spot=0.35, spot_colour=(0.14, 0.09, 0.05), grime=0.4)
RUST = dict(colour=(0.30, 0.20, 0.14), dark=(0.10, 0.07, 0.05), scale=24, rough=0.5, bump=0.5,
            spot=0.3, spot_colour=(0.48, 0.24, 0.10))
# Bright red-brown rust, for the braced iron panels.
REDRUST = dict(colour=(0.52, 0.24, 0.11), dark=(0.15, 0.06, 0.03), scale=20, rough=0.5, bump=0.5)
# The carved landing in front of the stairs.
DDAIS = dict(colour=(0.46, 0.40, 0.24), dark=(0.08, 0.07, 0.04), scale=12, detail=16, bump=0.8, grime=0.6)


def stairway(**kw):
    """The fort's stair gateway (fort.py) in this area's stone."""
    import fort
    # As thin as the walls and flat on top, so it sits flush in the wall line.
    fort.stairway(look=dict(stone=BLOCKS, cap=CAP, dais=DDAIS), raise_top=0.0, cap=False,
                  thick=WALL_T, deep=WALL_T, **kw)


def landing():
    import fort
    fort.landing(dict(stone=BLOCKS, cap=CAP, dais=DDAIS))


DARK = dict(colour=(0.05, 0.05, 0.05), dark=(0.02, 0.02, 0.02), scale=4, bump=0)

WALL_L, WALL_H, WALL_T = 6.4, 5.9, 0.7
DWALL_L = 5.8  # the dungeon's own walls are a little shorter, like the original's


def solid(o):
    """Apply an object's scale, so its texture is laid out in Blender units."""
    bpy.ops.object.select_all(action="DESELECT")
    o.select_set(True)
    bpy.context.view_layer.objects.active = o
    bpy.ops.object.transform_apply(location=False, rotation=False, scale=True)
    return o


def wall(along_y=False, fittings=None, seed=1):
    """A plain slab wall of big mossy blocks, flat on top like the
    original's, two tiles long. fittings: None, "pipes" (a fat copper pipe
    along it and others coming down to a valve and a drum) or "grate" (a
    rusty iron panel braced across)."""
    rng = random.Random(seed)
    blocks = mat("blocks", BLOCKS)
    copper, iron = mat("copper", OLDCOPPER), mat("iron", IRON)
    solid(box((DWALL_L, WALL_T, WALL_H), (0, 0, WALL_H / 2), blocks))
    face = -WALL_T / 2  # the side facing the camera (Blender -y)
    if fittings == "pipes":
        cyl(0.34, DWALL_L, (0, face - 0.40, WALL_H - 1.0), copper, rot=(0, math.pi / 2, 0))
        for x in (-DWALL_L / 2 + 0.4, 0.6, DWALL_L / 2 - 0.4):
            cyl(0.42, 0.2, (x, face - 0.40, WALL_H - 1.0), copper, rot=(0, math.pi / 2, 0))
            box((0.14, 0.45, 0.6), (x, face - 0.15, WALL_H - 1.3), iron)
        x2 = rng.uniform(1.2, 2.2)
        cyl(0.26, WALL_H - 1.0 - 1.5, (x2, face - 0.40, (WALL_H - 1.0 + 1.5) / 2), copper)
        ball(0.34, (x2, face - 0.40, WALL_H - 1.0), copper)
        cyl(0.6, 1.5, (x2, face - 0.65, 0.75), copper, verts=20)  # a drum at its foot
        for z in (0.2, 1.3):
            cyl(0.63, 0.12, (x2, face - 0.65, z), iron, verts=20)
        x = rng.uniform(-1.8, -0.4)
        cyl(0.24, 3.2, (x, face - 0.40, WALL_H - 2.6), copper)
        ball(0.32, (x, face - 0.40, WALL_H - 1.0), copper)
        cyl(0.36, 0.2, (x, face - 0.40, 2.6), iron)
        # The valve wheel, facing out.
        bpy.ops.mesh.primitive_torus_add(major_radius=0.36, minor_radius=0.05, location=(x, face - 0.62, 2.0),
                                         rotation=(math.pi / 2, 0, 0))
        studio.assign(bpy.context.object, iron)
        cyl(0.07, 0.4, (x, face - 0.45, 2.0), iron, rot=(math.pi / 2, 0, 0))
        cyl(0.18, 0.9, (x, face - 0.30, 1.2), copper)
        cyl(0.28, 0.3, (x, face - 0.30, 0.6), copper)
    elif fittings == "grate":
        # A rusty iron frame over most of the face, braced corner to
        # corner.
        frame = mat("redrust", REDRUST)
        w, h = DWALL_L - 0.8, WALL_H - 0.8
        for dz in (-h / 2, h / 2):
            box((w + 0.2, 0.24, 0.24), (0, face - 0.12, WALL_H / 2 + dz), frame)
        for dx in (-w / 2, w / 2):
            box((0.24, 0.24, h + 0.2), (dx, face - 0.12, WALL_H / 2), frame)
        for sa in (-1, 1):
            box((0.2, 0.2, math.hypot(w, h)), (0, face - 0.14, WALL_H / 2), frame, rot=(0, sa * math.atan2(w, h), 0))
    if along_y:
        turn(math.pi / 2)


def turn(angle):
    """Rotate everything built so far about the vertical axis."""
    for o in bpy.context.scene.objects:
        if o.type == "MESH" and not o.is_shadow_catcher and o.parent is None:
            o.location = (o.location.x * math.cos(angle) - o.location.y * math.sin(angle),
                          o.location.x * math.sin(angle) + o.location.y * math.cos(angle), o.location.z)
            o.rotation_euler.z += angle


def pillar():
    """A plain square pillar of the same blocks, as tall as the walls."""
    solid(box((2.0, 2.0, WALL_H), (0, 0, WALL_H / 2), mat("blocks", BLOCKS)))


def pipe(along_y=False, valve=False):
    """A fat copper pipe lying on iron saddles, flanged at the joints."""
    copper, iron = mat("copper", OLDCOPPER), mat("iron", IRON)
    length, r, z = 3.6, 0.55, 0.85
    cyl(r, length, (0, 0, z), copper, rot=(0, math.pi / 2, 0), verts=24)
    for x in (-length / 2 + 0.1, length / 2 - 0.1):
        cyl(r + 0.12, 0.22, (x, 0, z), copper, rot=(0, math.pi / 2, 0), verts=24)
        for k in range(8):
            a = 2 * math.pi * k / 8
            ball(0.05, (x, (r + 0.1) * math.cos(a), z + (r + 0.1) * math.sin(a)), iron)
    for x in (-0.9, 0.9):
        rust = mat("rust", RUST)
        box((0.3, 1.3, 0.35), (x, 0, 0.18), rust)
        box((0.3, 0.15, 0.7), (x, 0.62, 0.45), rust)
        box((0.3, 0.15, 0.7), (x, -0.62, 0.45), rust)
    if valve:
        cyl(0.18, 0.5, (0.2, 0, z + r + 0.2), copper)
        bpy.ops.mesh.primitive_torus_add(major_radius=0.35, minor_radius=0.05, location=(0.2, 0, z + r + 0.48))
        studio.assign(bpy.context.object, iron)
    if along_y:
        turn(math.pi / 2)


def elbow():
    """A pipe bend: a quarter torus on a saddle, ends flanged."""
    copper, iron = mat("copper", OLDCOPPER), mat("iron", IRON)
    R, r, z = 1.1, 0.55, 0.85
    bpy.ops.mesh.primitive_torus_add(major_radius=R, minor_radius=r, major_segments=32, minor_segments=16,
                                     location=(0, 0, z))
    o = bpy.context.object
    studio.smooth(studio.assign(o, copper))
    # Keep one quarter: cut the rest away with a boolean box.
    bpy.ops.mesh.primitive_cube_add(size=1, location=(-1.6, 0, z))
    cutter = bpy.context.object
    cutter.scale = (3.2, 6.0, 3.0)
    mod = o.modifiers.new("cut", "BOOLEAN")
    mod.operation = "DIFFERENCE"
    mod.object = cutter
    bpy.ops.mesh.primitive_cube_add(size=1, location=(1.6, -1.6, z))
    cutter2 = bpy.context.object
    cutter2.scale = (3.2, 3.2, 3.0)
    mod2 = o.modifiers.new("cut2", "BOOLEAN")
    mod2.operation = "DIFFERENCE"
    mod2.object = cutter2
    cutter.hide_render = cutter2.hide_render = True
    cyl(r + 0.12, 0.22, (R, -0.05, z), copper, rot=(math.pi / 2, 0, 0), verts=24)
    cyl(r + 0.12, 0.22, (0.05, R, z), copper, rot=(0, math.pi / 2, 0), verts=24)
    box((1.3, 0.3, 0.35), (0.8, 0.8, 0.18), iron)


def hub():
    """A big junction: a squat copper drum with four pipes running out and
    iron feet round it."""
    copper, iron = mat("copper", OLDCOPPER), mat("iron", IRON)
    cyl(1.6, 1.4, (0, 0, 1.2), copper, verts=32)
    cyl(1.75, 0.2, (0, 0, 1.95), copper, verts=32)
    cyl(1.75, 0.2, (0, 0, 0.45), copper, verts=32)
    ball(1.2, (0, 0, 1.9), copper, scale=(1, 1, 0.45))
    cyl(0.35, 0.5, (0, 0, 2.6), iron)
    for k in range(4):
        a = math.pi / 4 + k * math.pi / 2
        ca, sa = math.cos(a), math.sin(a)
        cyl(0.45, 2.4, (2.6 * ca, 2.6 * sa, 1.0), copper, rot=(0, math.pi / 2, a), verts=20)
        cyl(0.58, 0.2, (3.7 * ca, 3.7 * sa, 1.0), copper, rot=(0, math.pi / 2, a), verts=20)
        cyl(0.3, 0.6, (2.0 * math.cos(a + math.pi / 4), 2.0 * math.sin(a + math.pi / 4), 0.3), iron)


def vat():
    """A tall copper vat with a domed lid, banded with iron."""
    copper, iron = mat("copper", OLDCOPPER), mat("iron", IRON)
    cyl(0.9, 3.4, (0, 0, 1.7), copper, verts=24)
    ball(0.9, (0, 0, 3.4), copper, scale=(1, 1, 0.5))
    cyl(0.25, 0.4, (0, 0, 3.95), copper)
    for z in (0.3, 1.7, 3.1):
        cyl(0.94, 0.14, (0, 0, z), iron, verts=24)


def gate(along_y=False):
    """A gate of iron bars with spear tips between two posts."""
    iron, rust = mat("iron", IRON), mat("rust", RUST)
    w, h = 3.0, 5.6
    for k in range(9):
        x = -w / 2 + 0.15 + k * (w - 0.3) / 8
        cyl(0.05, h, (x, 0, h / 2), iron, verts=8)
        bpy.ops.mesh.primitive_cone_add(vertices=6, radius1=0.1, depth=0.3, location=(x, 0, h + 0.15))
        studio.assign(bpy.context.object, iron)
    for z in (0.4, h - 0.5):
        box((w, 0.1, 0.12), (0, 0, z), rust)
    # A curl of bars in the middle.
    bpy.ops.mesh.primitive_torus_add(major_radius=0.45, minor_radius=0.04, location=(0, 0, h * 0.5),
                                     rotation=(math.pi / 2, 0, 0))
    studio.assign(bpy.context.object, iron)
    for x in (-w / 2 - 0.1, w / 2 + 0.1):
        box((0.22, 0.22, h + 0.4), (x, 0, (h + 0.4) / 2), rust)
        ball(0.16, (x, 0, h + 0.5), rust)
    if along_y:
        turn(math.pi / 2)


def hatch(big=False):
    """An iron plate set in the floor, riveted round the edge."""
    iron, rust = mat("iron", IRON), mat("rust", RUST)
    w, d = (1.8, 1.4) if big else (1.4, 1.0)
    box((w, d, 0.08), (0, 0, 0.04), rust)
    box((w - 0.25, d - 0.25, 0.1), (0, 0, 0.06), mat("copper", OLDCOPPER))
    for sx in (-1, 1):
        for sy in (-1, 1):
            ball(0.05, (sx * (w / 2 - 0.08), sy * (d / 2 - 0.08), 0.09), iron)
    box((0.5, 0.06, 0.06), (0, -d / 2 + 0.3, 0.13), iron)


def stairs(along_y=False):
    """Steps going down into the dark between two block walls, with an
    arch of stone over the top: the way to the next level."""
    blocks, cap, hole = mat("blocks", BLOCKS), mat("cap", CAP), mat("hole", DARK)
    w, d = 3.0, 4.0
    box((w, d, 0.05), (0, 0, -0.02), hole)
    for k in range(5):
        box((w, d - k * 0.8, 0.25), (0, k * 0.4, -0.2 - k * 0.45), cap)
    for x in (-w / 2 - 0.4, w / 2 + 0.4):
        solid(box((0.8, d, 3.6), (x, 0, 1.8), blocks))
        box((1.0, d + 0.2, 0.35), (x, 0, 3.7), cap)
    box((w + 1.8, 0.9, 0.8), (0, d / 2 - 0.45, 4.1), cap)
    if along_y:
        turn(math.pi / 2)


def brazier():
    """An iron fire basket on three legs, heaped with glowing coals."""
    iron = mat("iron", IRON)
    coal = studio.material("coal", (1.0, 0.45, 0.08), (0.35, 0.05, 0.02), scale=14, spot=0.3,
                           spot_colour=(1.0, 0.85, 0.3))
    em = coal.node_tree.nodes["Principled BSDF"]
    em.inputs["Emission Strength"].default_value = 2.0
    coal.node_tree.links.new(em.inputs["Base Color"].links[0].from_socket, em.inputs["Emission Color"])
    cyl(0.75, 0.6, (0, 0, 2.0), iron, r2=0.5, verts=16)
    ball(0.62, (0, 0, 2.3), coal, scale=(1, 1, 0.35))
    for k in range(3):
        a = 2 * math.pi * k / 3
        o = cyl(0.06, 2.0, (0.45 * math.cos(a), 0.45 * math.sin(a), 0.9), iron, verts=8)
        o.rotation_euler = (0.25 * math.sin(a), -0.25 * math.cos(a), 0)


def obelisk():
    """A tall dark stone obelisk on a stepped base."""
    stone = mat("obsidian", colour=(0.20, 0.20, 0.24), dark=(0.06, 0.06, 0.08), scale=10, rough=0.4, bump=0.4)
    cap = mat("cap", CAP)
    box((1.6, 1.6, 0.5), (0, 0, 0.25), cap)
    box((1.2, 1.2, 0.4), (0, 0, 0.7), cap)
    v = [(-0.45, -0.45, 0.9), (0.45, -0.45, 0.9), (0.45, 0.45, 0.9), (-0.45, 0.45, 0.9),
         (-0.3, -0.3, 6.0), (0.3, -0.3, 6.0), (0.3, 0.3, 6.0), (-0.3, 0.3, 6.0), (0, 0, 6.6)]
    f = [(0, 1, 5, 4), (1, 2, 6, 5), (2, 3, 7, 6), (3, 0, 4, 7), (4, 5, 8), (5, 6, 8), (6, 7, 8), (7, 4, 8)]
    mesh(v, f, stone, "obelisk")


def chest():
    """A heavy iron-bound chest with a curved lid."""
    wood = mat("chestwood", colour=(0.40, 0.24, 0.12), dark=(0.15, 0.08, 0.04), scale=10, bump=0.5,
               stretch=(0.3, 4, 1))
    iron = mat("iron", IRON)
    box((1.3, 0.85, 0.7), (0, 0, 0.35), wood)
    cyl(0.43, 1.3, (0, 0, 0.7), wood, rot=(0, math.pi / 2, 0), verts=20).scale = (1, 1, 0.7)
    for x in (-0.5, 0, 0.5):
        box((0.1, 0.9, 0.72), (x, 0, 0.36), iron)
    box((0.22, 0.06, 0.26), (0, -0.45, 0.62), iron)


PROPS = {
    "dwall_x": (wall, 288),
    "dwall_y": (lambda: wall(along_y=True), 288),
    "dwall_pipes_x": (lambda: wall(fittings="pipes", seed=2), 288),
    "dwall_pipes_y": (lambda: wall(along_y=True, fittings="pipes", seed=3), 288),
    "dwall_grate_x": (lambda: wall(fittings="grate"), 288),
    "dwall_grate_y": (lambda: wall(along_y=True, fittings="grate"), 288),
    "dpillar": (pillar, 224),
    "pipe_x": (pipe, 128),
    "pipe_y": (lambda: pipe(along_y=True), 128),
    "pipe_valve": (lambda: pipe(valve=True), 128),
    "pipe_elbow": (elbow, 128),
    "pipe_hub": (lambda: (hub(), resize(1.35)), 320),
    "vat": (vat, 128),
    "gate_x": (gate, 224),
    "gate_y": (lambda: gate(along_y=True), 224),
    "hatch": (hatch, 64),
    "hatch_big": (lambda: hatch(big=True), 64),
    "stairs_down": (stairs, 256),
    "dstairs": (lambda: stairway(dais=False, sides="lr", far_jamb=1.55, near_jamb=1.4), 320),
    "dstairs_pit": (lambda: stairway(dais=False, sides="lr", down=True, far_jamb=1.4, near_jamb=1.65), 320),
    "ddais": (landing, 224),
    "brazier": (brazier, 96),
    "obelisk": (obelisk, 192),
    "chest": (chest, 64),
}


def main():
    args = [a for a in sys.argv[sys.argv.index("--") + 1:]] if "--" in sys.argv else sys.argv[1:]
    blend = "--blend" in args
    args = [a for a in args if a != "--blend"]
    out = Path(args[0])
    names = args[1:] or list(PROPS)
    out.mkdir(parents=True, exist_ok=True)
    listing = out / "dungeon.txt"
    anchors = dict(l.split(" ", 1) for l in listing.read_text().split("\n") if l) if listing.exists() else {}
    for name in names:
        build, size = PROPS[name]
        _mats.clear()
        scene = studio.reset(samples=32)
        build()
        walls = name.startswith(("dwall", "dpillar", "dstairs", "ddais"))
        if walls:  # the original's walls cast no shadows
            bpy.data.objects["shadow catcher"].hide_render = True
        # and are drawn looking down at 45 degrees, so their tops slope like the original's
        studio.camera(scene, size, size, target=(0, 0, size / studio.PIXELS_PER_UNIT * 0.3),
                      elevation=math.radians(45) if walls else None)
        ax, ay = studio.anchor(scene)
        if blend:
            studio.save_blend(out / f"{name}.blend")
        studio.render(scene, str(out / f"{name}.png"))
        anchors[name] = f"{ax:.0f} {ay:.0f}"
        print(name, "done", flush=True)
    listing.write_text("".join(f"{k} {v}\n" for k, v in anchors.items()))


if __name__ == "__main__":
    main()
