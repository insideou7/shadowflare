"""Remote Town's warehouse in ShadowFlare's style, rendered with studio.py:
a squat vault of dark courses with lighter corner stones, a flat stone roof
under a sod mound that droops over its right half, and an arched plank
door with iron bands on the left face. The town's storage keeper stands at
it (Scenario/10000000/script.txt draws it over its click area).

    python tools/art/warehouse.py [--blend] <output folder>

(run with a Python that has the `bpy` module). Writes warehouse.png plus
warehouse.txt with its anchor point, like town.py. The click area is about
6.1 units square, so the vault is made to fill it.
"""

import math
import random
import sys
from pathlib import Path

import bpy

sys.path.insert(0, str(Path(__file__).resolve().parent))
import studio  # noqa: E402
from town import IRON, PLANK, _mats, box, mat  # noqa: E402

# Dark, close courses; the corner stones are paler and sit proud.
VAULT = dict(colour=(0.44, 0.37, 0.27), dark=(0.17, 0.14, 0.10), scale=9, bump=0.9, grime=0.55,
             bricks=dict(width=0.6, height=0.3, mortar=0.03, vary=0.85, joint=0.6))
QUOIN = dict(colour=(0.24, 0.22, 0.19), dark=(0.08, 0.07, 0.06), scale=8, bump=0.7, grime=0.4,
             bricks=dict(width=0.7, height=0.35, mortar=0.04, vary=0.6, joint=0.5))
SLAB = dict(colour=(0.52, 0.49, 0.42), dark=(0.28, 0.26, 0.21), scale=6, bump=0.6, grime=0.5,
            bricks=dict(width=0.9, height=0.7, mortar=0.05, vary=0.6, joint=0.5))
SOD = dict(colour=(0.30, 0.21, 0.12), dark=(0.09, 0.06, 0.03), scale=26, detail=12, bump=1.6, grime=0.5,
           spot=0.22, spot_colour=(0.46, 0.38, 0.20))

SIDE, HEIGHT = 6.0, 5.6   # footprint and wall height
SLAB_T = 0.4              # the stone roof's thickness


def arch(r, depth, at, m):
    """A round-topped stub for an arch: a flat-shaded cylinder along y."""
    bpy.ops.mesh.primitive_cylinder_add(vertices=32, radius=r, depth=depth, location=at, rotation=(math.pi / 2, 0, 0))
    return studio.assign(bpy.context.object, m)


def vault():
    wall, quoin, slab = mat("vault", VAULT), mat("quoin", QUOIN), mat("slab", SLAB)
    sod, plank, iron = mat("sod", SOD), mat("plank", PLANK), mat("iron", IRON)
    box((SIDE, SIDE, HEIGHT), (0, 0, HEIGHT / 2), wall)
    # The corner stones, alternately long on one face and then the other,
    # as masons lay them.
    q = 0.8
    for sx in (-1, 1):
        for sy in (-1, 1):
            for k in range(10):
                z = 0.3 + k * 0.56
                long_x = (k % 2 == 0)
                w, d = (q * 1.5, q) if long_x else (q, q * 1.5)
                box((w, d, 0.5), (sx * (SIDE / 2 - w / 2 + 0.12), sy * (SIDE / 2 - d / 2 + 0.12), z), quoin)
    # The flat stone roof with a rim, and a sod mound over the right part
    # (+x) drooping down the right face and the right end of the left face.
    box((SIDE + 0.6, SIDE + 0.6, SLAB_T), (0, 0, HEIGHT + SLAB_T / 2), slab)
    box((SIDE + 0.6, 0.3, 0.35), (0, -(SIDE + 0.3) / 2, HEIGHT + SLAB_T + 0.17), slab)
    box((0.3, SIDE + 0.6, 0.35), (-(SIDE + 0.3) / 2, 0, HEIGHT + SLAB_T + 0.17), slab)
    z0 = HEIGHT + SLAB_T
    box((3.7, SIDE + 0.2, 0.7), (1.45, 0, z0 + 0.3), sod)
    box((2.6, SIDE - 0.8, 0.5), (1.9, 0, z0 + 0.85), sod)
    box((0.7, SIDE + 0.4, 2.2), (SIDE / 2 + 0.2, 0, HEIGHT - 0.8), sod)
    box((3.7, 0.6, 1.7), (1.55, -SIDE / 2 - 0.15, HEIGHT - 0.55), sod)
    # Ragged tufts along the sod's edges.
    rng = random.Random(3)
    for _ in range(40):
        edge = rng.choice("xyf")
        if edge == "x":   # along the right face's lower edge
            x, y, z = SIDE / 2 + 0.2, rng.uniform(-SIDE / 2, SIDE / 2), HEIGHT - rng.uniform(1.4, 2.1)
        elif edge == "y":  # along the left face's lower edge, right of the door
            x, y, z = rng.uniform(0.2, SIDE / 2), -SIDE / 2 - 0.35, HEIGHT - rng.uniform(1.0, 1.6)
        else:              # the top's rim
            x, y, z = rng.uniform(-0.2, SIDE / 2 + 0.3), rng.choice((-1, 1)) * (SIDE / 2 + 0.1), z0 + rng.uniform(0, 0.5)
        box((rng.uniform(0.4, 0.8), rng.uniform(0.4, 0.8), rng.uniform(0.3, 0.6)), (x, y, z), sod,
            rot=(0, 0, rng.uniform(0, 1.5)))
    # The arched door on the left face (-y), towards the left corner: a
    # dark stone frame round plank doors that stand a little proud of it.
    dx, dy = -1.45, -SIDE / 2
    box((3.2, 0.5, 3.0), (dx, dy - 0.05, 1.5), quoin)
    arch(1.6, 0.54, (dx, dy - 0.05, 3.0), quoin)
    box((2.2, 0.2, 3.0), (dx, dy - 0.38, 1.5), plank)
    arch(1.1, 0.24, (dx, dy - 0.38, 3.0), plank)
    box((0.06, 0.1, 3.9), (dx, dy - 0.5, 1.95), iron)
    for z in (0.7, 1.9):
        box((2.25, 0.08, 0.16), (dx, dy - 0.5, z), iron)
    box((0.14, 0.08, 0.4), (dx + 0.8, dy - 0.52, 1.5), iron)


def main():
    args = [a for a in sys.argv[sys.argv.index("--") + 1:]] if "--" in sys.argv else sys.argv[1:]
    blend = "--blend" in args
    args = [a for a in args if a != "--blend"]
    out = Path(args[0])
    out.mkdir(parents=True, exist_ok=True)
    _mats.clear()
    scene = studio.reset(samples=32)
    vault()
    size = 448
    bpy.data.objects["shadow catcher"].hide_render = True  # the original's vault casts no shadow
    studio.camera(scene, size, size, target=(0, 0, size / studio.PIXELS_PER_UNIT * 0.3))
    ax, ay = studio.anchor(scene)
    if blend:
        studio.save_blend(out / "warehouse.blend")
    studio.render(scene, str(out / "warehouse.png"))
    (out / "warehouse.txt").write_text(f"warehouse {ax:.0f} {ay:.0f}\n")
    print("warehouse done", flush=True)


if __name__ == "__main__":
    main()
