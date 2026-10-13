"""The tall town walls of Remote Town and Losmans in ShadowFlare's style,
rendered with studio.py: sandy ashlar panels whose feet sweep out in a
concave flare, square piers between them, a corner tower with two drain
holes, gateways barred by iron portcullises, and the drawbridge posts by
the town gate with their chain winches and a hanging row of stakes.

    python tools/art/townwall.py [--blend] <output folder> [piece ...]

(run with a Python that has the `bpy` module). Writes one PNG per piece plus
townwall.txt listing each file's anchor point, like castle.py. The original
sets its panels and piers 7.5 units apart; a panel is about 11 units high.
"""

import math
import random
import sys
from pathlib import Path

import bpy
import bmesh  # noqa: E402  (needs bpy loaded first)
from mathutils import Euler, Vector  # noqa: E402

sys.path.insert(0, str(Path(__file__).resolve().parent))
import studio  # noqa: E402
from castle import cut  # noqa: E402
from dungeon import solid, turn  # noqa: E402
from town import _mats, box, cyl, mat, mesh  # noqa: E402

# Pale sandy stone laid in small courses, stained darker in patches.
SANDSTONE = dict(colour=(0.64, 0.60, 0.48), dark=(0.26, 0.23, 0.17), scale=26, detail=12, bump=0.9, grime=0.35,
                 bricks=dict(width=0.5, height=0.2, mortar=0.05, vary=0.7, joint=0.5))
PALE = dict(colour=(0.80, 0.77, 0.66), dark=(0.52, 0.49, 0.40), scale=10, bump=0.5, grime=0.3)
IRON = dict(colour=(0.20, 0.19, 0.18), dark=(0.05, 0.05, 0.05), scale=14, rough=0.45, bump=0.4)
RUST = dict(colour=(0.36, 0.19, 0.11), dark=(0.10, 0.05, 0.03), scale=24, rough=0.7, bump=0.8)
STAKE = dict(colour=(0.24, 0.23, 0.22), dark=(0.07, 0.07, 0.07), scale=12, rough=0.6, bump=0.5, stretch=(1, 1, 6))
WOOD = dict(colour=(0.30, 0.20, 0.12), dark=(0.09, 0.06, 0.03), scale=10, bump=0.6, stretch=(6, 1, 1))
BLACK = dict(colour=(0.015, 0.015, 0.02), dark=(0.005, 0.005, 0.01), scale=4, bump=0)

PANEL_L, PANEL_T, PANEL_H = 6.0, 0.7, 11.2
FLARE_W, FLARE_H = 0.8, 3.6  # how far a foot sweeps out, and how high the sweep reaches
PIER_A, PIER_B, PIER_H = 1.3, 2.6, 11.9  # along the wall, across it, height


def profile(t, h, flare_w, flare_h, steps=10):
    """The cross-section of a flared wall, in (across, up), round the
    outline: a slab t thick and h high whose feet curve out flare_w on
    both faces, concave like a buttress."""
    front = []  # from the front foot (-t/2 - flare_w, 0) up to the face (-t/2, flare_h)
    for k in range(steps + 1):
        a = math.pi / 2 * (1 - k / steps)
        front.append((-t / 2 - flare_w * (1 - math.cos(a)), flare_h * (1 - math.sin(a))))
    back = [(-y, z) for y, z in reversed(front)]
    return front + [(-t / 2, h), (t / 2, h)] + back


def flared(length, t=PANEL_T, h=PANEL_H, flare_w=FLARE_W, flare_h=FLARE_H, x0=0.0, m=None):
    """A wall panel along x with flared feet on both faces, centred on x0."""
    outline = profile(t, h, flare_w, flare_h)
    n = len(outline)
    verts = [(x0 + sx * length / 2, y, z) for sx in (-1, 1) for y, z in outline]
    faces = [list(range(n))[::-1], list(range(n, 2 * n))]
    for k in range(n):
        j = (k + 1) % n
        faces.append([k, j, n + j, n + k])
    o = mesh(verts, faces, m or mat("sandstone", SANDSTONE), "panel")
    bm = bmesh.new()
    bm.from_mesh(o.data)
    bmesh.ops.recalc_face_normals(bm, faces=bm.faces)
    bm.to_mesh(o.data)
    bm.free()
    return o


def panel(along_y=False):
    """One flared panel of the town wall."""
    flared(PANEL_L)
    if along_y:
        turn(math.pi / 2)


def wall_end(along_y=False):
    """A short slice of flared wall, where the wall meets a gateway."""
    flared(1.6)
    if along_y:
        turn(math.pi / 2)


def pier(along_y=False):
    """A plain square pier standing proud of the wall on both faces."""
    stone = mat("sandstone", SANDSTONE)
    solid(box((PIER_A, PIER_B, PIER_H), (0, 0, PIER_H / 2), stone))
    if along_y:
        turn(math.pi / 2)


def corner_tower():
    """A tall square tower at a corner of the walls, two round drain
    holes high on its faces."""
    stone, black = mat("sandstone", SANDSTONE), mat("black", BLACK)
    s, h = 3.1, 14.5
    o = solid(box((s, s, h), (0, 0, h / 2), stone))
    for rot, (x, y) in ((0, (0.0, -s / 2)), (math.pi / 2, (s / 2, 0.0))):
        c = cyl(0.36, 1.0, (x, y, h - 2.6), stone, rot=(math.pi / 2, 0, rot), verts=20)
        cut(o, c)
        cyl(0.36, 0.1, (x * 0.8, y * 0.8, h - 2.6), black, rot=(math.pi / 2, 0, rot), verts=20)
        bpy.ops.mesh.primitive_torus_add(major_radius=0.4, minor_radius=0.07, location=(x * 1.01, y * 1.01, h - 2.6),
                                         rotation=(math.pi / 2, 0, rot))
        studio.assign(bpy.context.object, mat("iron", IRON))


def chain(a, b, m, link=0.36):
    """A chain of flat links from point a to point b."""
    ax, ay, az = a
    dx, dy, dz = b[0] - ax, b[1] - ay, b[2] - az
    d = math.sqrt(dx * dx + dy * dy + dz * dz)
    n = max(1, int(d / link))
    pitch = math.acos(dz / d)
    yaw = math.atan2(dy, dx)
    for k in range(n):
        f = (k + 0.5) / n
        bpy.ops.mesh.primitive_torus_add(major_radius=link * 0.55, minor_radius=0.07, major_segments=10,
                                         minor_segments=5, location=(ax + dx * f, ay + dy * f, az + dz * f),
                                         rotation=(0, pitch, yaw))
        o = bpy.context.object
        o.scale = (0.6, 1.0, 1.0)
        o.rotation_euler.rotate_axis("Z", (k % 2) * math.pi / 2)
        studio.assign(o, m)


def winch(at):
    """A chain winch on two timber skids shod with iron: a fat drum wound
    thick with chain between two iron wheels, a crank through its hub."""
    x, y = at
    iron, rust, wood = mat("iron", IRON), mat("rust", RUST), mat("wood", WOOD)
    for sx in (-1, 1):
        box((0.4, 3.0, 0.4), (x + sx * 1.0, y, 0.25), wood, rot=(0.15, 0, 0))
        cyl(0.2, 0.5, (x + sx * 1.0, y - 1.6, 0.3), iron, r2=0.0, rot=(math.pi / 2, 0, 0), verts=6)
        box((0.3, 0.7, 1.2), (x + sx * 0.9, y, 0.9), iron)
        cyl(1.0, 0.25, (x + sx * 0.75, y, 1.5), iron, rot=(0, math.pi / 2, 0), verts=24)
        cyl(0.45, 0.35, (x + sx * 0.95, y, 1.5), iron, rot=(0, math.pi / 2, 0), verts=16)
    cyl(0.62, 1.3, (x, y, 1.5), iron, rot=(0, math.pi / 2, 0), verts=20)
    # Turns of chain round the drum, link by link.
    for k in range(6):
        cx = x - 0.55 + k * 0.22
        for j in range(14):
            a = j * 2 * math.pi / 14 + k * 0.3
            bpy.ops.mesh.primitive_torus_add(major_radius=0.15, minor_radius=0.05, major_segments=8, minor_segments=4,
                                             location=(cx, y + 0.72 * math.cos(a), 1.5 + 0.72 * math.sin(a)),
                                             rotation=(a + (j % 2) * math.pi / 2, 0, (j % 2) * math.pi / 2))
            studio.assign(bpy.context.object, rust)
    cyl(0.1, 3.0, (x, y, 1.5), iron, rot=(0, math.pi / 2, 0))
    box((0.12, 0.12, 1.0), (x + 1.45, y, 1.95), iron)
    cyl(0.07, 0.6, (x + 1.6, y, 2.4), iron, rot=(0, math.pi / 2, 0))


def stake_curtain(x0, x1, top, z, rust, stakes, iron, n=7):
    """A row of iron-tipped stakes hung on short chains from a beam at
    height top, linked across by two chains."""
    for k in range(n):
        x = x0 + k * (x1 - x0) / (n - 1)
        t = z - 0.1 * k
        chain((x, 0, top), (x, 0, t), rust, link=0.26)
        cyl(0.17, 4.0, (x, 0, t - 2.0), stakes, verts=8)
        cyl(0.17, 0.8, (x, 0, t - 4.4), iron, r2=0.0, rot=(math.pi, 0, 0), verts=8)
        cyl(0.21, 0.14, (x, 0, t - 0.1), iron, verts=8)
    for dz in (0.6, 2.4):
        chain((x0, -0.2, z - dz), (x1, -0.2, z - dz - 0.1 * (n - 1)), rust, link=0.26)


def drawbridge_post(beam=False):
    """One post of the town gate: a tall pier with a dark iron collar under
    its head, a pale stone strip down its face, and a chain running down
    from the collar to a winch out on the ground before it. beam=True adds
    the timber arm across the gateway with a row of iron-shod stakes
    hanging from it on chains."""
    stone, pale, iron = mat("sandstone", SANDSTONE), mat("pale", PALE), mat("iron", IRON)
    rust, stakes = mat("rust", RUST), mat("stake", STAKE)
    s, h = 1.8, 17.0
    solid(box((s, s, h - 3.2), (0, 0, (h - 3.2) / 2), stone))
    box((s * 0.82, s * 0.82, 0.9), (0, 0, h - 2.75), iron)  # the collar
    for k in range(4):
        a = k * math.pi / 2
        box((0.18, 0.18, 0.18), (math.cos(a) * s * 0.42, math.sin(a) * s * 0.42, h - 2.3), iron)
    box((s * 1.04, s * 1.04, 0.25), (0, 0, h - 3.25), pale)
    solid(box((s, s, 2.6), (0, 0, h - 1.3), stone))  # the head
    box((0.6, 0.12, 0.8), (0, -s / 2 - 0.02, h - 1.4), mat("black", BLACK))
    if beam:
        for dx in (-0.55, 0.0):
            box((0.32, 0.25, h - 3.2), (dx, -s / 2 - 0.12, (h - 3.2) / 2), pale)
    # The chain from the collar down to the winch in front.
    wy = -6.4
    chain((0.0, -s / 2 - 0.1, h - 2.9), (0.0, wy + 0.3, 1.5), rust)
    winch((0.0, wy))
    if beam:
        # A timber arm reaching out over the gateway, iron at both ends.
        L = 5.4
        box((L, 0.55, 0.55), (s / 2 + L / 2, 0, h - 1.6), mat("pale", PALE))
        box((0.7, 0.7, 0.7), (s / 2 + L, 0, h - 1.6), iron)
        box((0.6, 0.8, 0.9), (s / 2 + 0.2, 0, h - 1.6), iron)
        stake_curtain(s / 2 + 0.7, s / 2 + L - 0.5, h - 1.9, h - 4.6, rust, stakes, iron)


def gate_wall(along_y=False, half=None):
    """A panel of the town wall pierced by a gateway: the opening's sides
    flare out like the wall's feet, and an iron portcullis bars it.
    half: "left" or "right" draws one side of it only."""
    stone = mat("sandstone", SANDSTONE)
    L, gw, gh = PANEL_L, 3.4, 5.6  # length, opening width and height
    o = flared(L)
    # The opening: straight up, then each side sweeping out into the
    # lintel in a concave curve.
    cutter = solid(box((gw, PANEL_T + 2 * FLARE_W + 1, gh - 1.4), (0, 0, (gh - 1.4) / 2 - 0.01), stone))
    cut(o, cutter)
    for sx in (-1, 1):
        c = cyl(1.5, PANEL_T + 2 * FLARE_W + 1, (sx * (gw / 2 - 1.5), 0, gh - 1.4), stone,
                rot=(math.pi / 2, 0, 0), verts=32)
        cut(o, c)
    cap = solid(box((gw - 0.2, PANEL_T + 2 * FLARE_W + 1, 1.5), (0, 0, gh - 0.75), stone))
    cut(o, cap)
    if not half:
        box((gw - 0.1, 0.1, gh - 0.1), (0, 0.45, (gh - 0.1) / 2), mat("black", BLACK))
    # The portcullis: a grid of rusty bars in the opening, or, in the open
    # halves, drawn up so only its spiked foot hangs under the lintel.
    rust = mat("rust", RUST)
    bars = gh if not half else 1.6
    for k in range(7):
        x = -gw / 2 + 0.3 + k * (gw - 0.6) / 6
        box((0.15, 0.15, bars), (x, 0.0, gh - bars / 2), rust)
        cyl(0.09, 0.35, (x, 0.0, gh - bars - 0.17), rust, r2=0.0, rot=(math.pi, 0, 0), verts=6)
    for z in ((0.9, 2.0, 3.1, 4.2) if not half else (gh - 0.9,)):
        box((gw, 0.15, 0.15), (0, 0.0, z), rust)
    if half:
        # Cut the wall off at one side of the gateway: keep the -x end
        # ("left") or the +x end ("right") and the lintel over the gateway.
        keep = -1 if half == "left" else 1
        drop = solid(box((L, 20, 40), (-keep * (gw / 2 + L / 2), 0, 0), stone))
        cut(o, drop)
    if along_y:
        turn(math.pi / 2)


BARK = dict(colour=(0.78, 0.60, 0.34), dark=(0.08, 0.06, 0.03), scale=7, detail=5, bump=1.0,
            stretch=(9, 9, 0.25), band=(0.44, 0.56))
CUT = dict(colour=(0.52, 0.48, 0.40), dark=(0.30, 0.27, 0.22), scale=20, bump=0.4)


def stockade(n, length, along_y=False, seed=1, heights=(4.0, 5.0)):
    """A row of n rough stakes along game x (or y), the palisade round the
    fields outside Remote Town: thin barked poles set a little apart, each
    leaning its own way, cut flat on top."""
    rng = random.Random(seed)
    bark, cut_end = mat("bark", BARK), mat("cut", CUT)
    for i in range(n):
        h = rng.uniform(*heights)
        r = rng.uniform(0.15, 0.19)
        t = (i + 0.5) / n * length - length / 2 + rng.uniform(-0.05, 0.05)
        off = rng.uniform(-0.1, 0.1)
        lean = (rng.uniform(-0.06, 0.06), rng.uniform(-0.06, 0.06), rng.uniform(0, 6))
        pole = cyl(r, h, (0, 0, h / 2), bark, verts=9)
        top = cyl(r * 0.98, 0.04, (0, 0, h), cut_end, verts=9)
        for o in (pole, top):
            v = Vector(o.location)
            v.rotate(Euler(lean))
            o.rotation_euler = lean
            o.location = v + Vector((t, off, 0))
    if along_y:
        turn(math.pi / 2)


PROPS = {
    "town_wall_x": (panel, 320, 11.2),
    "town_wall_y": (lambda: panel(along_y=True), 320, 11.2),
    "town_wall_end_x": (wall_end, 320, 11.2),
    "town_wall_end_y": (lambda: wall_end(along_y=True), 320, 11.2),
    "town_pier_x": (pier, 320, 11.9),
    "town_pier_y": (lambda: pier(along_y=True), 320, 11.9),
    "town_tower": (corner_tower, 384, 14.5),
    "town_gate_x": (gate_wall, 320, 11.2),
    "town_gate_y": (lambda: gate_wall(along_y=True), 320, 11.2),
    "town_gate_left_x": (lambda: gate_wall(half="left"), 320, 11.2),
    "town_gate_right_y": (lambda: gate_wall(along_y=True, half="right"), 320, 11.2),
    "drawbridge_post": (drawbridge_post, 512, 17),
    "drawbridge_beam": (lambda: drawbridge_post(beam=True), 512, 17),
    "stockade_a_x": (lambda: stockade(6, 2.5, seed=13, heights=(3.4, 4.2)), 192, 5),
    "stockade_b_x": (lambda: stockade(10, 4.4, seed=14, heights=(3.6, 5.0)), 192, 5),
    "stockade_a_y": (lambda: stockade(6, 2.8, True, seed=16, heights=(3.4, 4.2)), 192, 5),
    "stockade_b_y": (lambda: stockade(10, 4.6, True, seed=17, heights=(3.6, 5.0)), 192, 5),
}


def light(scene):
    """The original lights its town walls hard from the screen's right:
    faces turned to the lower right are bright, those turned to the lower
    left almost black."""
    scene.world.node_tree.nodes["Background"].inputs[1].default_value = AMBIENT
    sun = bpy.data.objects["sun"]
    sun.data.energy = SUN[0]
    sun.rotation_euler = (math.radians(SUN[1]), 0, math.radians(SUN[2]))


AMBIENT, SUN = 0.1, (5.0, 50, 90)


def main():
    args = [a for a in sys.argv[sys.argv.index("--") + 1:]] if "--" in sys.argv else sys.argv[1:]
    blend = "--blend" in args
    args = [a for a in args if a != "--blend"]
    out = Path(args[0])
    names = args[1:] or list(PROPS)
    out.mkdir(parents=True, exist_ok=True)
    listing = out / "townwall.txt"
    anchors = dict(l.split(" ", 1) for l in listing.read_text().split("\n") if l) if listing.exists() else {}
    for name in names:
        build, size, top = PROPS[name]
        _mats.clear()
        scene = studio.reset(samples=32)
        if name.startswith("stockade"):
            # Softer fill, so each stake's shaded side reads against the next.
            scene.world.node_tree.nodes["Background"].inputs[1].default_value = 0.5
        else:
            light(scene)
        build()
        bpy.data.objects["shadow catcher"].hide_render = True  # the original's walls cast no shadows
        for m in bpy.data.materials:
            # No glossy sheen on stone; iron keeps its own.
            if m.node_tree and "Principled BSDF" in m.node_tree.nodes and m.name not in ("iron", "rust"):
                m.node_tree.nodes["Principled BSDF"].inputs["Specular IOR Level"].default_value = 0.0
        studio.camera(scene, size, size, target=(0, 0, top / 2),
                      elevation=studio.WALL_ELEVATION)
        ax, ay = studio.anchor(scene)
        if blend:
            studio.save_blend(out / f"{name}.blend")
        studio.render(scene, str(out / f"{name}.png"))
        anchors[name] = f"{ax:.0f} {ay:.0f}"
        print(name, "done", flush=True)
    listing.write_text("".join(f"{k} {v}\n" for k, v in anchors.items()))


if __name__ == "__main__":
    main()
