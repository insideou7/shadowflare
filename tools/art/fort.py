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
from town import IRON, _mats, ball, box, cyl, mat, mesh, resize  # noqa: E402

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
SCONCE = dict(colour=(0.78, 0.74, 0.66), dark=(0.36, 0.33, 0.28), scale=14, rough=0.35, bump=0.3)
SLATE = dict(colour=(0.58, 0.58, 0.60), dark=(0.24, 0.24, 0.26), scale=8, rough=0.5, bump=0.5)
BRONZE = dict(colour=(0.40, 0.31, 0.20), dark=(0.10, 0.07, 0.04), scale=16, rough=0.35, bump=0.5,
              band=(0.30, 0.70), grime=0.4)
BLACKIRON = dict(colour=(0.08, 0.075, 0.07), dark=(0.02, 0.02, 0.02), scale=24, rough=0.75, bump=0.4)
PIPECOPPER = dict(colour=(0.44, 0.26, 0.13), dark=(0.12, 0.06, 0.03), scale=20, rough=0.5, bump=0.6,
                  spot=0.3, spot_colour=(0.16, 0.10, 0.06))
WALL_T = 0.8


def void():
    """Unlit black, for the faces the original blacks out."""
    if "void" not in bpy.data.materials:
        m = bpy.data.materials.new("void")
        m.use_nodes = True
        nodes = m.node_tree.nodes
        nodes.clear()
        emit = nodes.new("ShaderNodeEmission")
        emit.inputs["Color"].default_value = (0, 0, 0, 1)
        m.node_tree.links.new(emit.outputs[0], nodes.new("ShaderNodeOutputMaterial").inputs[0])
    return bpy.data.materials["void"]


def blackout(size, at):
    """A black sheet over a face: the original draws the near side of the
    fort's walls, the side the player never walks along, as plain black,
    so only the tops show."""
    box(size, at, void())


def wall(along_y=False, kind=None, seed=1):
    """A wall of grey stone blocks between a plinth and a dark capping
    course. kind: None, "torch" (an iron bracket on the face), "blood"
    (old stains) or "black" (its face blacked out)."""
    stone = mat("bloody", BLOODY) if kind == "blood" else mat("stone", STONE)
    cap = mat("capstone", CAPSTONE)
    solid(box((WALL_L, WALL_T, WALL_H), (0, 0, WALL_H / 2), stone))
    box((WALL_L + 0.1, WALL_T + 0.2, 0.3), (0, 0, WALL_H + 0.1), cap)
    box((WALL_L + 0.1, WALL_T + 0.15, 0.35), (0, 0, 0.17), cap)
    face = -WALL_T / 2
    if kind == "torch":
        # A sconce: an iron plate and arm holding a pale metal cup.
        iron, cup = mat("iron", IRON), mat("sconce", SCONCE)
        x = random.Random(seed).uniform(-0.3, 0.3)
        box((0.7, 0.2, 1.4), (x, face - 0.1, 3.2), iron)
        box((0.22, 0.9, 0.22), (x, face - 0.55, 3.0), iron)
        cyl(0.4, 0.55, (x, face - 1.0, 3.35), cup, r2=0.22, verts=12)
    if kind == "black":
        blackout((WALL_L + 0.12, 0.02, WALL_H + 0.25), (0, face - 0.12, (WALL_H + 0.25) / 2))
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


def pillar(black=""):
    """A square stone pillar as tall as the walls, its top flush with
    theirs, so the wall tops run on in one straight line. black: which of
    its faces are blacked out, "l" (the screen's left), "r" or "lr"."""
    stone, cap = mat("stone", STONE), mat("capstone", CAPSTONE)
    solid(box((2.0, 2.0, WALL_H), (0, 0, WALL_H / 2), stone))
    box((2.1, 2.1, 0.3), (0, 0, WALL_H + 0.1), cap)
    box((2.1, 2.1, 0.35), (0, 0, 0.17), cap)
    h = WALL_H + 0.25
    if "l" in black:
        blackout((2.24, 0.02, h), (0, -1.12, h / 2))
    if "r" in black:
        blackout((0.02, 2.24, h), (1.12, 0, h / 2))


def stairway(side=False, look=None, dais=True, sides="l", down=False, raise_top=0.6,
             cap=True, thick=WALL_T, deep=2.25):
    """Stone steps climbing through a gateway in the wall into the dark,
    on a half round dais of dark carved stone in front of it, with a low
    wall along the left of the flight. side: only the low wall along the
    right, which the original draws as a piece of its own. look: material
    specs (stone, cap, dais) for another area's stairs; dais: whether to
    build the dais; sides: which low walls, "l", "r" or "lr"; down: the
    flight goes down into a pit instead of up; raise_top: how much taller
    than a wall the gateway stands; cap: a capstone along the top; thick:
    how thick the gateway is; deep: how far back the dark and the flight
    go behind the wall's face."""
    look = look or dict(stone=STONE, cap=CAPSTONE, dais=DAIS)
    stone, capstone, hole = mat("stone", look["stone"]), mat("capstone", look["cap"]), mat("hole", DARK)
    w, n, rise, run = 2.6, 8, 0.38, 0.5
    y0 = -2.4  # front edge of the bottom step; the wall's face is at y = 0

    def parapet(x, foot=True):
        # A low wall whose top climbs with the flight (or a level stub
        # beside a pit).
        ya, yb = y0 - 0.15, 0.0
        ha, hb = (1.4, 1.4) if down else (rise + 0.6, 6 * rise + 0.6)
        v = [(x + dx, y, z) for dx in (-0.3, 0.3) for y, z in ((ya, 0), (yb, 0), (yb, hb), (ya, ha))]
        f = [(0, 1, 2, 3), (7, 6, 5, 4), (0, 4, 5, 1), (1, 5, 6, 2), (2, 6, 7, 3), (3, 7, 4, 0)]
        mesh(v, f, stone, "parapet")
        if foot and not down:  # a post at the foot
            solid(box((0.7, 0.7, 1.6), (x, y0 + 0.2, 0.8), stone))

    if side:
        parapet(w / 2 + 0.3, foot=False)
        return
    # The gateway: two jambs and a lintel. It stands a little in front of
    # the wall line, so it is built taller for its top to run on in line
    # with the walls beside it.
    jamb, top = 1.0, WALL_H + raise_top
    for x in (-w / 2 - jamb / 2, w / 2 + jamb / 2):
        solid(box((jamb, thick, top), (x, thick / 2, top / 2), stone))
    gate = 4.6
    solid(box((w, thick, top - gate), (0, thick / 2, (top + gate) / 2), stone))
    if cap:
        box((w + 2 * jamb + 0.1, thick + 0.2, 0.3), (0, thick / 2, top + 0.1), capstone)
    # Darkness beyond the gateway, the flight climbing into it (or going
    # down into a pit).
    back = min(0.25, deep / 2)
    box((w, deep - back, gate), (0, (back + deep) / 2, gate / 2), hole)
    if down:  # the pit, seen from above: black between the low walls
        box((w, -y0, 0.02), (0, y0 / 2, 0.01), hole)
    else:
        for k in range(n):
            h = (k + 1) * rise
            y = y0 + k * run
            end = min(1.2, deep)
            box((w, end - y, h), (0, (y + end) / 2, h / 2), capstone)
    if "l" in sides:
        parapet(-w / 2 - 0.3)
    if "r" in sides:
        parapet(w / 2 + 0.3, foot=False)
    if dais:
        landing(look)


def landing(look=None):
    """The half round dais of carved stone in front of a stair gateway,
    with rings and a curl cut into it."""
    look = look or dict(stone=STONE, cap=CAPSTONE, dais=DAIS)
    dais, cap = mat("dais", look["dais"]), mat("capstone", look["cap"])
    bpy.ops.mesh.primitive_cylinder_add(vertices=48, radius=3.4, depth=0.3, location=(0, 0, 0.15))
    disc = studio.assign(bpy.context.object, dais)
    cut = box((8.0, 4.0, 1.0), (0, 2.0, 0.2), cap)
    boolean = disc.modifiers.new("cut", "BOOLEAN")
    boolean.object, boolean.operation = cut, "DIFFERENCE"
    bpy.context.view_layer.objects.active = disc
    bpy.ops.object.modifier_apply(modifier="cut")
    bpy.data.objects.remove(cut)
    for r, cx, cy in ((0.45, -2.3, -1.2), (0.45, -2.0, -2.1), (0.8, -1.4, -2.5)):
        bpy.ops.mesh.primitive_torus_add(major_radius=r, minor_radius=0.07, location=(cx, cy, 0.31))
        studio.assign(bpy.context.object, cap).scale = (1, 1, 0.4)


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
    "fpillar_black": (lambda: pillar("lr"), 224),
    "fpillar_black_l": (lambda: pillar("l"), 224),
    "fpillar_black_r": (lambda: pillar("r"), 224),
    "fwall_black_x": (lambda: wall(kind="black"), 288),
    "fwall_black_y": (lambda: wall(along_y=True, kind="black"), 288),
    "fstairs": (stairway, 320),
    "fstairs_side": (lambda: stairway(side=True), 160),
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
        # The original's fort casts no shadows on the floor.
        bpy.data.objects["shadow catcher"].hide_render = True
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
