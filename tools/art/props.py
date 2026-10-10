"""Trees, rocks and fences, rendered with studio.py.

    python tools/art/props.py <output folder>

(run with a Python that has the `bpy` module). Writes one PNG per prop
plus props.txt listing each file's anchor point.
"""

import math
import random
import sys
from pathlib import Path

import bpy

sys.path.insert(0, str(Path(__file__).resolve().parent))
import studio  # noqa: E402

BARK = dict(colour=(0.15, 0.10, 0.06), dark=(0.05, 0.035, 0.02), scale=14, bump=0.6)
LEAVES = dict(colour=(0.33, 0.43, 0.11), dark=(0.07, 0.13, 0.03), scale=30, detail=8, bump=1.0,
              spot=0.3, spot_colour=(0.52, 0.58, 0.18))
ROCK = dict(colour=(0.46, 0.43, 0.37), dark=(0.20, 0.19, 0.16), scale=5, bump=0.9,
            spot=0.2, spot_colour=(0.30, 0.36, 0.15))
WOOD = dict(colour=(0.47, 0.35, 0.21), dark=(0.20, 0.14, 0.08), scale=9, bump=0.5)


def tree(seed):
    rng = random.Random(seed)
    bark = studio.material("bark", **BARK)
    leaves = studio.material("leaves", **LEAVES)
    height = rng.uniform(9.0, 11.0)
    bpy.ops.mesh.primitive_cone_add(vertices=10, radius1=0.38, radius2=0.10, depth=height, location=(0, 0, height / 2))
    studio.smooth(studio.assign(bpy.context.object, bark))
    # Branches up the trunk with clumps of leaves along their outer half.
    for i in range(16):
        angle = rng.uniform(0, 2 * math.pi)
        z = rng.uniform(height * 0.28, height * 0.85)
        length = rng.uniform(2.8, 4.4) * (1.25 - z / height)
        tip = (math.cos(angle) * length, math.sin(angle) * length, z + length * 0.45)
        bpy.ops.mesh.primitive_cone_add(vertices=6, radius1=0.09, radius2=0.02, depth=length * 1.1,
                                        location=(tip[0] / 2, tip[1] / 2, z + length * 0.22),
                                        rotation=(0, math.radians(64), angle))
        studio.assign(bpy.context.object, bark)
        for j in range(5):
            k = rng.uniform(0.35, 1.05)
            size = rng.uniform(0.5, 0.9)
            bpy.ops.mesh.primitive_ico_sphere_add(subdivisions=3, radius=size, location=(
                tip[0] * k + rng.uniform(-0.3, 0.3), tip[1] * k + rng.uniform(-0.3, 0.3),
                z + (tip[2] - z) * k + rng.uniform(-0.1, 0.4)))
            clump = bpy.context.object
            clump.scale = (1, 1, 0.6)
            studio.displace(clump, 0.9, 0.18, seed * 100 + i * 5 + j)
            studio.smooth(studio.assign(clump, leaves))
    for j in range(4):
        size = rng.uniform(0.6, 0.9)
        bpy.ops.mesh.primitive_ico_sphere_add(subdivisions=3, radius=size, location=(
            rng.uniform(-0.4, 0.4), rng.uniform(-0.4, 0.4), height - 0.3 + rng.uniform(-0.4, 0.4)))
        studio.displace(bpy.context.object, 0.45, 0.25, seed * 100 + 50 + j)
        studio.smooth(studio.assign(bpy.context.object, leaves))


def rock(seed):
    rng = random.Random(seed)
    mat = studio.material("rock", **ROCK)
    for i in range(rng.randint(1, 3)):
        size = rng.uniform(0.35, 0.8) if i else rng.uniform(0.7, 1.0)
        bpy.ops.mesh.primitive_ico_sphere_add(subdivisions=4, radius=size,
                                              location=(rng.uniform(-0.6, 0.6) * (i > 0), rng.uniform(-0.6, 0.6) * (i > 0), size * 0.25))
        stone = bpy.context.object
        stone.scale = (1, rng.uniform(0.7, 1.0), rng.uniform(0.5, 0.75))
        studio.displace(stone, 0.35 * size, 0.6, seed * 10 + i)
        studio.smooth(studio.assign(stone, mat))


def palisade(seed, along_y):
    """One tile's length of sharpened logs, along game x or y."""
    rng = random.Random(seed)
    mat = studio.material("logs", **WOOD)
    length = 3.2
    n = 9
    for i in range(n):
        h = rng.uniform(3.0, 3.6)
        t = (i + 0.5) / n * length - length / 2
        x, y = (0, -t) if along_y else (t, 0)
        bpy.ops.mesh.primitive_cylinder_add(vertices=8, radius=0.17, depth=h, location=(x, y, h / 2))
        log = bpy.context.object
        log.rotation_euler = (rng.uniform(-0.04, 0.04), rng.uniform(-0.04, 0.04), rng.uniform(0, 6))
        studio.smooth(studio.assign(log, mat))
        bpy.ops.mesh.primitive_cone_add(vertices=8, radius1=0.17, depth=0.45, location=(x, y, h + 0.22))
        studio.smooth(studio.assign(bpy.context.object, mat))
    # Two cross beams holding them together.
    for z in (0.9, 2.4):
        bpy.ops.mesh.primitive_cylinder_add(vertices=6, radius=0.07, depth=length,
                                            location=(0.2 if along_y else 0, 0 if along_y else 0.2, z),
                                            rotation=(math.radians(90), 0, 0) if along_y else (0, math.radians(90), 0))
        studio.assign(bpy.context.object, mat)


PROPS = {
    "tree1": (lambda: tree(1), 448, 448),
    "tree2": (lambda: tree(2), 448, 448),
    "tree3": (lambda: tree(3), 448, 448),
    "rock1": (lambda: rock(1), 128, 128),
    "rock2": (lambda: rock(2), 128, 128),
    "fence_x": (lambda: palisade(1, False), 192, 192),
    "fence_y": (lambda: palisade(2, True), 192, 192),
}


def main():
    out = Path(sys.argv[-1])
    out.mkdir(parents=True, exist_ok=True)
    lines = []
    for name, (build, w, h) in PROPS.items():
        scene = studio.reset(samples=32)
        build()
        # Frame the camera so the prop's base sits low in the image.
        studio.camera(scene, w, h, target=(0, 0, h / studio.PIXELS_PER_UNIT * 0.32))
        ax, ay = studio.anchor(scene)
        studio.render(scene, str(out / f"{name}.png"))
        lines.append(f"{name} {ax:.0f} {ay:.0f}")
        print(name, "done")
    (out / "props.txt").write_text("\n".join(lines) + "\n")


if __name__ == "__main__":
    main()
