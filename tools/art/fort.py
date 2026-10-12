"""Fort and tower pieces in ShadowFlare's style, rendered with studio.py:
grey stone walls with iron-studded doors, torch brackets and old blood,
a stairway down, a teleport dais, levers and glowing portal pads, and the
tower's boiler, iron railings and copper pipes.

    python tools/art/fort.py [--blend] <output folder> [piece ...]

(run with a Python that has the `bpy` module). Writes one PNG per piece plus
fort.txt listing each file's anchor point, like dungeon.py. Walls are the
same size as the dungeon's: two tiles long and about 5.9 units high.
"""

import math
import random
import sys
from pathlib import Path

import bpy

sys.path.insert(0, str(Path(__file__).resolve().parent))
import studio  # noqa: E402
from dungeon import DARK, WALL_H, WALL_L, solid, turn  # noqa: E402
from town import IRON, _mats, ball, box, cyl, mat, resize  # noqa: E402

# Grey stone in blocks about 1.0 x 0.5 units, streaked with grime.
# Weathered stone, its courses faint: pale grey patches through a dark
# brown mottle of grime and old stains.
STONE = dict(colour=(0.52, 0.49, 0.43), dark=(0.07, 0.055, 0.04), scale=16, detail=16, bump=1.0, grime=0.5,
             band=(0.4, 0.68), spot=0.45, spot_colour=(0.20, 0.13, 0.08),
             bricks=dict(width=1.0, height=0.45, mortar=0.03, vary=0.9, joint=0.65),
             fade=(1.6, 0.3, 6.3))  # worn pale at the foot, darker toward the top
CAPSTONE = {k: v for k, v in STONE.items() if k != "bricks"}  # the same stone, uncoursed
BLOODY = dict(STONE, spot=0.3, spot_colour=(0.34, 0.03, 0.02))
DOORIRON = dict(colour=(0.09, 0.09, 0.10), dark=(0.02, 0.02, 0.03), scale=20, rough=0.4, bump=0.5)
RIVET = dict(colour=(0.86, 0.80, 0.62), dark=(0.45, 0.40, 0.28), scale=40, rough=0.3)
BRASS = dict(colour=(0.66, 0.56, 0.32), dark=(0.28, 0.22, 0.10), scale=18, rough=0.35, bump=0.3)
# The round dais under the stairs: dark worn stone.
DAIS = dict(colour=(0.30, 0.28, 0.24), dark=(0.05, 0.045, 0.035), scale=14, detail=16, bump=0.8, grime=0.5,
            band=(0.4, 0.7), spot=0.3, spot_colour=(0.12, 0.08, 0.05))
SLATE = dict(colour=(0.58, 0.58, 0.60), dark=(0.24, 0.24, 0.26), scale=8, rough=0.5, bump=0.5)
BRONZE = dict(colour=(0.40, 0.31, 0.20), dark=(0.10, 0.07, 0.04), scale=16, rough=0.35, bump=0.5,
              band=(0.30, 0.70), grime=0.4)
BLACKIRON = dict(colour=(0.08, 0.075, 0.07), dark=(0.02, 0.02, 0.02), scale=24, rough=0.75, bump=0.4)
PIPECOPPER = dict(colour=(0.44, 0.26, 0.13), dark=(0.12, 0.06, 0.03), scale=20, rough=0.5, bump=0.6,
                  spot=0.3, spot_colour=(0.16, 0.10, 0.06))
WALL_T = 0.8


def wall(along_y=False, kind=None, seed=1):
    """A wall of grey stone blocks between a plinth and a dark capping
    course. kind: None, "torch" (an iron bracket on the face) or "blood"
    (old stains)."""
    stone = mat("bloody", BLOODY) if kind == "blood" else mat("stone", STONE)
    cap = mat("capstone", CAPSTONE)
    solid(box((WALL_L, WALL_T, WALL_H), (0, 0, WALL_H / 2), stone))
    box((WALL_L + 0.1, WALL_T + 0.2, 0.3), (0, 0, WALL_H + 0.1), cap)
    box((WALL_L + 0.1, WALL_T + 0.15, 0.35), (0, 0, 0.17), cap)
    face = -WALL_T / 2
    if kind == "torch":
        iron = mat("iron", IRON)
        x = random.Random(seed).uniform(-0.6, 0.6)
        box((0.6, 0.35, 1.2), (x, face - 0.15, 3.1), iron)
        box((0.2, 0.8, 0.2), (x, face - 0.5, 3.0), iron)
        cyl(0.42, 0.5, (x, face - 0.95, 3.35), iron, r2=0.24, verts=10)
    if along_y:
        turn(math.pi / 2)


def door(along_y=False):
    """An iron double door studded with rivets, set between stone posts
    under a lintel."""
    stone, cap = mat("stone", STONE), mat("capstone", CAPSTONE)
    iron, rivet = mat("dooriron", DOORIRON), mat("rivet", RIVET)
    post = 1.0
    for x in (-WALL_L / 2 + post / 2, WALL_L / 2 - post / 2):
        solid(box((post, WALL_T + 0.2, WALL_H), (x, 0, WALL_H / 2), stone))
    solid(box((WALL_L - 2 * post + 0.02, WALL_T + 0.1, 0.9), (0, 0, WALL_H - 0.45), stone))
    box((WALL_L + 0.1, WALL_T + 0.3, 0.3), (0, 0, WALL_H + 0.1), cap)
    w, h = WALL_L - 2 * post, WALL_H - 0.9
    face = -WALL_T / 2 + 0.1
    box((w, 0.3, h), (0, 0.05, h / 2), iron)
    box((0.06, 0.05, h), (0, face - 0.02, h / 2), mat("hole", DARK))
    for x in (-w / 2 + 0.25, -0.25, 0.25, w / 2 - 0.25):
        box((0.18, 0.08, h), (x, face - 0.04, h / 2), iron)
    for side in (-1, 1):
        for row in range(6):
            for col in range(3):
                ball(0.06, (side * (0.45 + 0.55 * col), face - 0.1, 0.5 + row * (h - 0.9) / 5), rivet)
    if along_y:
        turn(math.pi / 2)


def pillar():
    """A square stone pillar with a dark cap and base."""
    stone, cap = mat("stone", STONE), mat("capstone", CAPSTONE)
    solid(box((2.0, 2.0, WALL_H + 1.0), (0, 0, (WALL_H + 1.0) / 2), stone))
    box((2.2, 2.2, 0.35), (0, 0, WALL_H + 1.1), cap)
    box((2.2, 2.2, 0.4), (0, 0, 0.2), cap)


def stairway():
    """A flight of stone steps climbing between low side walls to a dark
    doorway in the wall behind, on a round dais of dark carved stone."""
    stone, cap, hole = mat("stone", STONE), mat("capstone", CAPSTONE), mat("hole", DARK)
    dais = mat("dais", DAIS)
    w, n, rise, run = 2.6, 6, 0.42, 0.55
    y0 = -1.1  # front edge of the bottom step
    back = y0 + n * run  # where the flight meets the wall
    # The wall behind, with a dark doorway at the top of the flight.
    solid(box((w + 2.8, 0.8, WALL_H), (0, back + 0.4, WALL_H / 2), stone))
    box((w + 2.9, 1.0, 0.3), (0, back + 0.4, WALL_H + 0.1), cap)
    top = n * rise
    box((w, 0.2, 2.6), (0, back - 0.02, top + 1.3), hole)
    box((w + 0.6, 0.4, 0.4), (0, back - 0.1, top + 2.8), cap)
    # The steps, each a block down to the floor; worn pale treads.
    for k in range(n):
        h = (k + 1) * rise
        box((w, back - (y0 + k * run), h), (0, (y0 + k * run + back) / 2, h / 2), cap)
    # Low side walls that step up with the flight.
    for x in (-w / 2 - 0.35, w / 2 + 0.35):
        for k in range(n):
            h = (k + 1) * rise + 0.5
            y = y0 + k * run - (0.2 if k == 0 else 0)
            solid(box((0.7, back - y, h), (x, (y + back) / 2, h / 2), stone))
    # The dais: a round slab, carved with rings and a curl in front.
    bpy.ops.mesh.primitive_cylinder_add(vertices=48, radius=3.3, depth=0.3, location=(0, y0 + 0.4, 0.15))
    studio.assign(bpy.context.object, dais)
    bpy.ops.mesh.primitive_torus_add(major_radius=3.15, minor_radius=0.12, location=(0, y0 + 0.4, 0.3))
    rim = studio.assign(bpy.context.object, dais)
    rim.scale = (1, 1, 0.6)
    for r, cx in ((0.55, -0.7), (0.55, 0.7), (1.1, 0.0)):
        bpy.ops.mesh.primitive_torus_add(major_radius=r, minor_radius=0.07, location=(cx, y0 - 1.3, 0.31))
        studio.assign(bpy.context.object, cap).scale = (1, 0.6, 0.4)


def teleporter():
    """A square stone dais with a sunken plate in the middle and four
    brass fins pointing out from the corners."""
    slab, cap, brass = mat("slate", SLATE), mat("capstone", CAPSTONE), mat("brass", BRASS)
    box((3.0, 3.0, 0.45), (0, 0, 0.22), slab)
    box((2.2, 2.2, 0.15), (0, 0, 0.5), cap)
    box((1.4, 1.4, 0.1), (0, 0, 0.55), slab)
    box((0.5, 0.5, 0.12), (0, 0, 0.62), mat("hole", DARK))
    for k in range(4):
        a = math.pi / 4 + k * math.pi / 2
        x, y = 2.2 * math.cos(a), 2.2 * math.sin(a)
        box((1.8, 0.9, 0.12), (x, y, 0.2), brass, rot=(0, 0, a))
        box((0.5, 0.5, 0.25), (1.6 * math.cos(a), 1.6 * math.sin(a), 0.45), slab, rot=(0, 0, a))
        bpy.ops.mesh.primitive_cone_add(vertices=3, radius1=0.18, depth=0.05,
                                        location=(2.6 * math.cos(a), 2.6 * math.sin(a), 0.3))
        studio.assign(bpy.context.object, mat("rivet", RIVET))


def switch(up=True):
    """A squat iron box with a lever and a red knob, thrown up or down."""
    iron = mat("iron", IRON)
    knob = studio.material("knob", (0.85, 0.12, 0.08), (0.40, 0.04, 0.03), scale=30, rough=0.3)
    box((0.9, 0.7, 0.45), (0, 0, 0.22), iron)
    box((0.5, 0.45, 0.15), (0.1, 0, 0.5), iron)
    angle = math.radians(-35 if up else 55)
    length = 1.1
    rod = cyl(0.04, length, (0.1 + math.sin(angle) * length / 2, 0, 0.55 + math.cos(angle) * length / 2), iron, verts=8)
    rod.rotation_euler = (0, angle, 0)
    ball(0.1, (0.1 + math.sin(angle) * length, 0, 0.55 + math.cos(angle) * length), knob)


def broken_ring():
    """Pieces of a ring of pale slabs, broken away on one side."""
    slab = mat("slate", SLATE)
    rng = random.Random(4)
    for k in range(9):
        a = math.radians(-120 + k * 28)
        box((1.0 + rng.uniform(-0.1, 0.1), 0.7, 0.3 + rng.uniform(0, 0.15)),
            (1.6 * math.cos(a), 1.6 * math.sin(a), 0.18), slab, rot=(0, 0, a + math.pi / 2))
    box((0.6, 0.5, 0.25), (-0.4, -1.9, 0.12), slab, rot=(0, 0.2, 0.5))


def portal(colour):
    """A round brass ring set in the floor round a pool of light."""
    brass = mat("brass", BRASS)
    glow = studio.material("glow", colour, tuple(c * 0.3 for c in colour), scale=6)
    p = glow.node_tree.nodes["Principled BSDF"]
    p.inputs["Emission Strength"].default_value = 2.5
    glow.node_tree.links.new(p.inputs["Base Color"].links[0].from_socket, p.inputs["Emission Color"])
    bpy.ops.mesh.primitive_torus_add(major_radius=1.25, minor_radius=0.14, location=(0, 0, 0.12))
    studio.assign(bpy.context.object, brass).scale = (1, 1, 0.6)
    bpy.ops.mesh.primitive_torus_add(major_radius=0.95, minor_radius=0.06, location=(0, 0, 0.14))
    studio.assign(bpy.context.object, brass)
    cyl(0.9, 0.12, (0, 0, 0.08), glow, verts=32)


def boiler():
    """The tower's engine: a tall riveted tank, a domed cylinder beside it,
    a grilled cabinet underneath and a fat pipe curling out of the front."""
    bronze, iron = mat("bronze", BRONZE), mat("iron", IRON)
    grille = mat("grille", dict(colour=(0.32, 0.26, 0.18), dark=(0.04, 0.03, 0.02), scale=6, bump=0.8,
                                bricks=dict(width=0.18, height=0.18, mortar=0.25, vary=0.1, joint=0.5)))
    solid(box((2.6, 2.6, 2.2), (-0.6, -0.6, 1.1), grille))
    box((2.8, 2.8, 0.2), (-0.6, -0.6, 2.25), bronze)
    cyl(1.25, 4.6, (0.7, 0.9, 2.3), bronze, verts=28)
    for z in (1.0, 2.6, 4.2):
        cyl(1.32, 0.18, (0.7, 0.9, z), bronze, verts=28)
    cyl(1.0, 0.4, (0.7, 0.9, 4.8), iron, verts=28)
    cyl(0.75, 0.3, (0.7, 0.9, 5.0), mat("hole", DARK), verts=28)
    cyl(0.8, 3.0, (-0.8, -0.6, 3.8), bronze, verts=24)
    ball(0.8, (-0.8, -0.6, 5.3), bronze, scale=(1, 1, 0.6))
    for x, y in ((1.8, -0.4), (2.2, 0.4)):
        cyl(0.35, 1.4, (x, y, 2.9), bronze, verts=16)
        ball(0.35, (x, y, 3.6), bronze, scale=(1, 1, 0.5))
    # The big pipe: down from the tank, then out along the floor.
    bpy.ops.mesh.primitive_torus_add(major_radius=1.0, minor_radius=0.55, location=(1.0, -1.3, 1.6),
                                     rotation=(math.pi / 2, 0, 0))
    torus = studio.assign(bpy.context.object, bronze)
    cut = box((3.0, 3.0, 3.0), (2.5, -1.3, 1.6), iron)
    boolean = torus.modifiers.new("cut", "BOOLEAN")
    boolean.object, boolean.operation = cut, "DIFFERENCE"
    bpy.context.view_layer.objects.active = torus
    bpy.ops.object.modifier_apply(modifier="cut")
    bpy.data.objects.remove(cut)
    cyl(0.55, 2.0, (2.0, -1.3, 0.6), bronze, rot=(0, math.pi / 2, 0), verts=20)
    for x in (1.3, 2.6):
        cyl(0.62, 0.15, (x, -1.3, 0.6), iron, rot=(0, math.pi / 2, 0), verts=20)


def railing(along_y=False, lattice=True):
    """A railing of iron bars with spear tips between a top and bottom rail,
    crossed with a diamond lattice."""
    iron = mat("blackiron", BLACKIRON)
    w, h = 4.6, 4.2
    for k in range(7):
        x = -w / 2 + k * w / 6
        cyl(0.05, h, (x, 0, h / 2), iron, verts=8)
        bpy.ops.mesh.primitive_cone_add(vertices=6, radius1=0.12, depth=0.35, location=(x, 0, h + 0.15))
        studio.assign(bpy.context.object, iron)
    for z in (0.3, h - 0.5, h - 1.6):
        box((w, 0.08, 0.1), (0, 0, z), iron)
    if lattice:
        for k in range(6):
            x = -w / 2 + (k + 0.5) * w / 6
            for s in (-1, 1):
                rod = box((0.05, 0.05, 1.45), (x, 0, h - 1.05), iron)
                rod.rotation_euler = (0, s * math.atan2(w / 6, 1.1), 0)
    if along_y:
        turn(math.pi / 2)


def post():
    """A square iron post with a cap."""
    iron = mat("blackiron", BLACKIRON)
    box((0.4, 0.4, 5.2), (0, 0, 2.6), iron)
    box((0.55, 0.55, 0.25), (0, 0, 5.3), iron)
    for z in (1.0, 4.2):
        box((0.5, 0.5, 0.12), (0, 0, z), iron)


def floor_pipe(along_y=False):
    """A long, thin copper pipe lying along the floor."""
    copper = mat("pipecopper", PIPECOPPER)
    cyl(0.24, 6.0, (0, 0, 0.24), copper, rot=(0, math.pi / 2, 0), verts=12)
    for x in (-2.95, 2.95):
        cyl(0.30, 0.14, (x, 0, 0.24), copper, rot=(0, math.pi / 2, 0), verts=12)
    if along_y:
        turn(math.pi / 2)


def pipe_bend():
    """A short elbow of the same copper pipe."""
    bpy.ops.mesh.primitive_torus_add(major_radius=0.45, minor_radius=0.17, location=(0, 0, 0.17))
    torus = studio.assign(bpy.context.object, mat("pipecopper", PIPECOPPER))
    cut = box((2.0, 2.0, 1.0), (-1.0, 0, 0.2), mat("hole", DARK))
    cut2 = box((2.0, 2.0, 1.0), (0, -1.0, 0.2), mat("hole", DARK))
    for c in (cut, cut2):
        b = torus.modifiers.new("cut", "BOOLEAN")
        b.object, b.operation = c, "DIFFERENCE"
        bpy.context.view_layer.objects.active = torus
        bpy.ops.object.modifier_apply(modifier="cut")
        bpy.data.objects.remove(c)


PROPS = {
    "fwall_x": (wall, 288),
    "fwall_y": (lambda: wall(along_y=True), 288),
    "fwall_torch_x": (lambda: wall(kind="torch", seed=2), 288),
    "fwall_torch_y": (lambda: wall(along_y=True, kind="torch", seed=3), 288),
    "fwall_blood_x": (lambda: wall(kind="blood"), 288),
    "fwall_blood_y": (lambda: wall(along_y=True, kind="blood"), 288),
    "fdoor_x": (door, 288),
    "fdoor_y": (lambda: door(along_y=True), 288),
    "fpillar": (pillar, 224),
    "fstairs": (stairway, 320),
    "teleporter": (teleporter, 160),
    "switch_up": (lambda: (switch(True), resize(1.6)), 96),
    "switch_down": (lambda: (switch(False), resize(1.6)), 96),
    "broken_ring": (broken_ring, 128),
    "portal_blue": (lambda: portal((0.25, 0.55, 1.0)), 96),
    "portal_red": (lambda: portal((1.0, 0.2, 0.25)), 96),
    "boiler": (boiler, 288),
    "railing_x": (railing, 192),
    "railing_y": (lambda: railing(along_y=True), 192),
    "railing_plain_x": (lambda: railing(lattice=False), 192),
    "railing_plain_y": (lambda: railing(along_y=True, lattice=False), 192),
    "iron_post": (post, 128),
    "floor_pipe_x": (floor_pipe, 160),
    "floor_pipe_y": (lambda: floor_pipe(along_y=True), 160),
    "pipe_bend": (pipe_bend, 48),
}


def main():
    args = [a for a in sys.argv[sys.argv.index("--") + 1:]] if "--" in sys.argv else sys.argv[1:]
    blend = "--blend" in args
    args = [a for a in args if a != "--blend"]
    out = Path(args[0])
    names = args[1:] or list(PROPS)
    out.mkdir(parents=True, exist_ok=True)
    listing = out / "fort.txt"
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
