"""Cave and mine pieces in ShadowFlare's style, rendered with studio.py:
rough rock walls, timber-propped tunnels, plank-lined mine galleries and
what is found in them.

    python tools/art/cave.py [--blend] <output folder> [piece ...]

(run with a Python that has the `bpy` module). Writes one PNG per piece plus
cave.txt listing each file's anchor point, like town.py. Walls are two
tiles long (6.4 Blender units) and about 7 units high.
"""

import math
import random
import sys
from pathlib import Path

import bpy

sys.path.insert(0, str(Path(__file__).resolve().parent))
import studio  # noqa: E402
from dungeon import DARK, solid, turn  # noqa: E402
from town import IRON, _mats, ball, box, cyl, mat, mesh  # noqa: E402

ROCK = dict(colour=(0.40, 0.22, 0.09), dark=(0.035, 0.02, 0.012), scale=18, detail=14, bump=1.6, grime=0.5,
            band=(0.45, 0.65), spot=0.28, spot_colour=(0.70, 0.44, 0.20))
TIMBER = dict(colour=(0.46, 0.28, 0.13), dark=(0.14, 0.08, 0.04), scale=6, bump=0.6, stretch=(1, 1, 8), grime=0.3)
BOARDS = dict(colour=(0.22, 0.21, 0.19), dark=(0.06, 0.06, 0.05), scale=5, bump=0.5, stretch=(6, 6, 0.4),
              grime=0.6, spot=0.25, spot_colour=(0.40, 0.17, 0.07))
SLAB = dict(colour=(0.48, 0.46, 0.38), dark=(0.20, 0.19, 0.15), scale=4, bump=0.8, grime=0.6)

WALL_L, WALL_H = 6.4, 7.0


def rough(o, strength=0.35, size=0.6, seed=1, cuts=24):
    """Subdivide a box and push it about with noise, for raw rock."""
    bpy.ops.object.select_all(action="DESELECT")
    o.select_set(True)
    bpy.context.view_layer.objects.active = o
    bpy.ops.object.mode_set(mode="EDIT")
    bpy.ops.mesh.subdivide(number_cuts=cuts)
    bpy.ops.object.mode_set(mode="OBJECT")
    studio.displace(o, strength, size, seed)
    return o


def rock_wall(along_y=False, props=False, seed=1):
    """A wall of raw brown rock, darker towards the top; with props, two
    timber posts and a cap beam hold the face up."""
    rock, timber = mat("rock", ROCK), mat("timber", TIMBER)
    rough(solid(box((WALL_L, 1.2, WALL_H), (0, 0.2, WALL_H / 2), rock)), 0.45, 0.8, seed)
    if props:
        for x in (-2.0, 2.0):
            box((0.45, 0.45, WALL_H - 0.8), (x, -0.75, (WALL_H - 0.8) / 2), timber)
        box((5.0, 0.5, 0.5), (0, -0.75, WALL_H - 0.6), timber)
        box((0.25, 0.3, 2.2), (0, -0.7, WALL_H - 1.6), timber, rot=(0, math.radians(40), 0))
    if along_y:
        turn(math.pi / 2)


def rock_pillar(seed=3):
    """A column of rock left standing in a tunnel."""
    rough(solid(box((1.8, 1.8, WALL_H), (0, 0, WALL_H / 2), mat("rock", ROCK))), 0.4, 0.7, seed, cuts=16)


def mine_wall(along_y=False, broken=False, seed=1):
    """A gallery wall lined with dark planks in a timber frame: posts at
    the ends, a beam along the top. Broken walls have boards nailed across
    a hole."""
    timber, boards, hole = mat("timber", TIMBER), mat("boards", BOARDS), mat("hole", DARK)
    rng = random.Random(seed)
    if broken:
        box((WALL_L - 0.6, 0.5, WALL_H - 0.6), (0, 0.15, (WALL_H - 0.6) / 2), hole)
        for k in range(5):
            a = rng.uniform(-50, 50)
            box((0.25, 0.12, WALL_H * 0.75), (rng.uniform(-2.2, 2.2), -0.2, WALL_H * 0.45), boards,
                rot=(0, math.radians(a), 0))
    else:
        solid(box((WALL_L - 0.4, 0.5, WALL_H - 0.4), (0, 0.15, (WALL_H - 0.4) / 2), boards))
    for x in (-WALL_L / 2 + 0.25, WALL_L / 2 - 0.25):
        box((0.5, 0.6, WALL_H), (x, 0, WALL_H / 2), timber)
    # The rock above the gallery, cut flat: a pale stone top behind the beam.
    solid(box((WALL_L + 0.1, 1.3, 0.35), (0, 0.4, WALL_H + 0.15), mat("slab", SLAB)))
    box((WALL_L + 0.1, 0.6, 0.45), (0, 0, WALL_H - 0.2), timber)
    box((WALL_L + 0.1, 0.25, 0.3), (0, -0.35, WALL_H - 1.2), timber)
    if along_y:
        turn(math.pi / 2)


def mine_pillar():
    """A square pillar of grey stone slabs."""
    solid(box((2.4, 2.4, 8.5), (0, 0, 4.25), mat("slab", SLAB)))


def post():
    """A single pit prop."""
    box((0.45, 0.45, 7.0), (0, 0, 3.5), mat("timber", TIMBER))


def frame(along_y=False):
    """Two pit props and a cap beam across a tunnel."""
    timber = mat("timber", TIMBER)
    for x in (-1.5, 1.5):
        box((0.45, 0.45, 6.2), (x, 0, 3.1), timber)
    box((3.8, 0.5, 0.5), (0, 0, 6.3), timber)
    for x in (-1, 1):
        box((0.25, 0.3, 1.2), (x * 1.1, 0, 5.7), timber, rot=(0, math.radians(45 * x), 0))
    if along_y:
        turn(math.pi / 2)


def lantern():
    """An iron lantern hung from a small tripod of sticks."""
    timber, iron = mat("timber", TIMBER), mat("iron", IRON)
    glow = studio.material("glow", (1.0, 0.75, 0.3), (0.8, 0.4, 0.1), scale=10)
    p = glow.node_tree.nodes["Principled BSDF"]
    p.inputs["Emission Strength"].default_value = 3.0
    glow.node_tree.links.new(p.inputs["Base Color"].links[0].from_socket, p.inputs["Emission Color"])
    for k in range(3):
        a = 2 * math.pi * k / 3
        o = cyl(0.05, 1.9, (0.3 * math.cos(a), 0.3 * math.sin(a), 0.9), timber, verts=6)
        o.rotation_euler = (0.3 * math.sin(a), -0.3 * math.cos(a), 0)
    cyl(0.2, 0.35, (0, 0, 1.3), glow, verts=8)
    cyl(0.24, 0.08, (0, 0, 1.5), iron, verts=8)
    cyl(0.24, 0.08, (0, 0, 1.1), iron, verts=8)


def wooden_stairs():
    """A flight of plank steps climbing a wall on timber stringers."""
    timber = mat("timber", TIMBER)
    boards = mat("step", colour=(0.66, 0.42, 0.18), dark=(0.30, 0.17, 0.06), scale=6, bump=0.5, stretch=(1, 8, 1))
    n, run, rise = 12, 0.55, 0.55
    for k in range(n):
        box((run + 0.05, 2.6, 0.12), (k * run - n * run / 2, 0, (k + 1) * rise), boards)
    for y in (-1.3, 1.3):
        length = math.hypot(n * run, n * rise)
        box((length, 0.15, 0.35), (0, y, n * rise / 2 + 0.3), timber, rot=(0, -math.atan2(rise, run), 0))
    box((0.4, 3.0, n * rise), (n * run / 2 + 0.2, 0, n * rise / 2), timber)
    turn(math.pi)  # climbing away from the camera, to the upper left


def rock_stairs():
    """Rough stone steps climbing out of a cave."""
    rock = mat("rock", ROCK)
    for k in range(8):
        rough(solid(box((3.2, 4.0 - k * 0.45, 0.6 + k * 0.6), (0, -k * 0.22, (0.6 + k * 0.6) / 2), rock)),
              0.12, 0.5, k + 1, cuts=6)
    for x in (-2.2, 2.2):
        rough(solid(box((1.2, 4.2, 6.0), (x, 0, 3.0), rock)), 0.35, 0.7, 9 + int(x), cuts=12)


def warp_circle():
    """A round stone dais carved with a ring of runes."""
    slab = mat("darkstone", colour=(0.22, 0.22, 0.24), dark=(0.08, 0.08, 0.09), scale=8, rough=0.4, bump=0.5)
    iron = mat("iron", IRON)
    rune = studio.material("rune", (0.85, 0.72, 0.38), (0.45, 0.34, 0.14), scale=30, rough=0.3)
    cyl(2.6, 0.3, (0, 0, 0.15), slab, verts=48)
    cyl(2.1, 0.08, (0, 0, 0.32), iron, verts=48)
    cyl(1.9, 0.1, (0, 0, 0.33), slab, verts=48)
    for k in range(16):
        a = 2 * math.pi * k / 16
        box((0.25, 0.08, 0.04), (2.0 * math.cos(a), 2.0 * math.sin(a), 0.37), rune, rot=(0, 0, a + 0.4))
    cyl(0.6, 0.12, (0, 0, 0.4), rune, verts=6)


def lever():
    """A lever on an iron base, with a red knob."""
    iron = mat("iron", IRON)
    knob = studio.material("knob", (0.8, 0.1, 0.08), scale=20, rough=0.3)
    box((0.9, 0.6, 0.35), (0, 0, 0.18), iron)
    cyl(0.06, 1.3, (0.25, 0, 0.8), iron, rot=(0, math.radians(30), 0), verts=8)
    ball(0.13, (0.58, 0, 1.38), knob)


def crystal(colour=(0.55, 0.75, 1.0)):
    """A tall glowing crystal on a stone foot."""
    glass = studio.material("crystal", colour, tuple(c * 0.4 for c in colour), scale=8, rough=0.15)
    p = glass.node_tree.nodes["Principled BSDF"]
    p.inputs["Emission Strength"].default_value = 1.2
    glass.node_tree.links.new(p.inputs["Base Color"].links[0].from_socket, p.inputs["Emission Color"])
    v = [(0.3 * math.cos(a), 0.3 * math.sin(a), z) for z in (0.4, 4.2) for a in
         (2 * math.pi * k / 6 for k in range(6))] + [(0, 0, 4.8)]
    f = [(k, (k + 1) % 6, 6 + (k + 1) % 6, 6 + k) for k in range(6)] + [(6 + k, 6 + (k + 1) % 6, 12) for k in range(6)]
    mesh(v, f, glass, "crystal")
    rough(solid(box((1.0, 1.0, 0.5), (0, 0, 0.25), mat("rock", ROCK))), 0.08, 0.4, 5, cuts=4)


def chest(open_=False, gold=False):
    """A metal-bound chest; open ones show a red lining."""
    body = mat("chest", colour=(0.80, 0.68, 0.36) if gold else (0.30, 0.31, 0.30),
               dark=(0.35, 0.28, 0.12) if gold else (0.07, 0.07, 0.07), scale=20, rough=0.3, bump=0.4)
    trim = mat("trim", colour=(0.92, 0.78, 0.30), dark=(0.45, 0.35, 0.10), scale=30, rough=0.3)
    lining = mat("lining", colour=(0.85, 0.30, 0.35), dark=(0.40, 0.10, 0.12), scale=12)
    box((1.4, 0.9, 0.7), (0, 0, 0.35), body)
    for x in (-0.68, 0.68):
        box((0.06, 0.94, 0.72), (x, 0, 0.36), trim)
    if open_:
        box((1.3, 0.8, 0.05), (0, 0, 0.66), lining)
        box((1.4, 0.1, 0.8), (0, 0.5, 1.1), body, rot=(math.radians(-15), 0, 0))
    else:
        box((1.42, 0.92, 0.3), (0, 0, 0.82), body)
        box((0.2, 0.05, 0.25), (0, -0.47, 0.65), trim)


def barrel():
    """A small iron-bound keg lying on its side."""
    wood = mat("keg", colour=(0.45, 0.42, 0.40), dark=(0.18, 0.17, 0.16), scale=6, stretch=(1, 1, 8), bump=0.5)
    iron = mat("iron", IRON)
    cyl(0.5, 1.2, (0, 0, 0.5), wood, rot=(0, math.pi / 2, 0), verts=16)
    for x in (-0.45, 0, 0.45):
        cyl(0.53, 0.08, (x, 0, 0.5), iron, rot=(0, math.pi / 2, 0), verts=16)


def crate():
    """A long plank crate."""
    wood = mat("plank", colour=(0.62, 0.46, 0.26), dark=(0.30, 0.20, 0.10), scale=6, stretch=(8, 1, 1), bump=0.5)
    box((2.2, 1.0, 0.8), (0, 0, 0.4), wood)
    for x in (-1.0, 1.0):
        box((0.12, 1.04, 0.84), (x, 0, 0.42), mat("timber", TIMBER))


PROPS = {
    "rock_x": (rock_wall, 288),
    "rock_y": (lambda: rock_wall(along_y=True, seed=2), 288),
    "rock_props_x": (lambda: rock_wall(props=True, seed=4), 288),
    "rock_props_y": (lambda: rock_wall(along_y=True, props=True, seed=5), 288),
    "rock_pillar": (rock_pillar, 224),
    "rock_stairs": (rock_stairs, 288),
    "mine_x": (mine_wall, 288),
    "mine_y": (lambda: mine_wall(along_y=True), 288),
    "mine_broken_x": (lambda: mine_wall(broken=True, seed=2), 288),
    "mine_broken_y": (lambda: mine_wall(along_y=True, broken=True, seed=3), 288),
    "mine_pillar": (mine_pillar, 288),
    "pit_prop": (post, 192),
    "frame_x": (frame, 224),
    "frame_y": (lambda: frame(along_y=True), 224),
    "lantern": (lantern, 64),
    "wooden_stairs": (wooden_stairs, 320),
    "warp_circle": (warp_circle, 192),
    "lever": (lever, 64),
    "crystal_blue": (crystal, 160),
    "crystal_red": (lambda: crystal((1.0, 0.55, 0.6)), 160),
    "chest_silver": (chest, 64),
    "chest_open": (lambda: chest(open_=True), 64),
    "chest_gold": (lambda: chest(gold=True), 64),
    "keg": (barrel, 64),
    "long_crate": (crate, 96),
}


def main():
    args = [a for a in sys.argv[sys.argv.index("--") + 1:]] if "--" in sys.argv else sys.argv[1:]
    blend = "--blend" in args
    args = [a for a in args if a != "--blend"]
    out = Path(args[0])
    names = args[1:] or list(PROPS)
    out.mkdir(parents=True, exist_ok=True)
    listing = out / "cave.txt"
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
