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
from mathutils import Vector

sys.path.insert(0, str(Path(__file__).resolve().parent))
import studio  # noqa: E402

ROCK = dict(colour=(0.46, 0.43, 0.37), dark=(0.20, 0.19, 0.16), scale=5, bump=0.9,
            spot=0.2, spot_colour=(0.30, 0.36, 0.15))
WOOD = dict(colour=(0.47, 0.35, 0.21), dark=(0.20, 0.14, 0.08), scale=9, bump=0.5)


BARK = dict(colour=(0.40, 0.24, 0.25), dark=(0.13, 0.07, 0.08), scale=14, bump=0.6)
LEAF_LIGHT = (0.58, 0.66, 0.14)
LEAF_DARK = (0.10, 0.17, 0.04)


def branch(points, radius, mat):
    """A tapering tube through points, as a bevelled curve."""
    curve = bpy.data.curves.new("branch", "CURVE")
    curve.dimensions = "3D"
    curve.bevel_depth = radius
    curve.bevel_resolution = 1
    spline = curve.splines.new("POLY")
    spline.points.add(len(points) - 1)
    for i, p in enumerate(points):
        spline.points[i].co = (*p, 1)
        spline.points[i].radius = 1 - 0.85 * i / (len(points) - 1)
    obj = bpy.data.objects.new("branch", curve)
    bpy.context.scene.collection.objects.link(obj)
    curve.materials.append(mat)
    return obj


def leaf_material():
    """Leaves coloured per leaf from a random attribute, lit through from behind a little."""
    mat = bpy.data.materials.new("leaves")
    mat.use_nodes = True
    nodes, links = mat.node_tree.nodes, mat.node_tree.links
    bsdf = nodes["Principled BSDF"]
    bsdf.inputs["Roughness"].default_value = 0.8
    attr = nodes.new("ShaderNodeAttribute")
    attr.attribute_name = "shade"
    ramp = nodes.new("ShaderNodeValToRGB")
    ramp.color_ramp.elements[0].color = (*studio.linear(LEAF_DARK), 1)
    ramp.color_ramp.elements[1].color = (*studio.linear(LEAF_LIGHT), 1)
    links.new(attr.outputs["Fac"], ramp.inputs["Fac"])
    links.new(ramp.outputs["Color"], bsdf.inputs["Base Color"])
    return mat


def tree(seed):
    """A birch-like tree in the original's manner: a thin dark trunk, arching
    branches and drooping sprays of small leaves with the ground showing
    through."""
    import bmesh
    rng = random.Random(seed)
    bark = studio.material("bark", **BARK)
    height = rng.uniform(7.0, 8.5)
    lean = (rng.uniform(-0.3, 0.3), rng.uniform(-0.3, 0.3))
    trunk = [(lean[0] * (i / 6) ** 2, lean[1] * (i / 6) ** 2, height * i / 6) for i in range(7)]
    branch(trunk, 0.22, bark)
    leaves = bmesh.new()
    shade = leaves.faces.layers.float.new("shade")

    def leaf(at):
        s = rng.uniform(0.09, 0.15)
        a, b = rng.uniform(0, math.pi), rng.uniform(0, 2 * math.pi)
        u = Vector((math.cos(b), math.sin(b), 0)) * s
        v = Vector((-math.sin(b) * math.cos(a), math.cos(b) * math.cos(a), math.sin(a))) * s * 0.7
        c = Vector(at)
        verts = [leaves.verts.new(c + u), leaves.verts.new(c + v), leaves.verts.new(c - u), leaves.verts.new(c - v)]
        face = leaves.faces.new(verts)
        # Leaves on top catch more light than those hanging below.
        face[shade] = min(1.0, max(0.0, rng.random() ** 1.5 * 0.85 + (at[2] / height) * 0.25))

    def droop(start, direction, length, n):
        """A twig hanging from start, curving down, with leaves along it."""
        p, d = Vector(start), Vector(direction).normalized()
        pts = [tuple(p)]
        for i in range(n):
            d = (d + Vector((0, 0, -0.35))).normalized()
            p = p + d * (length / n)
            pts.append(tuple(p))
            for _ in range(7):
                leaf(p + Vector((rng.gauss(0, 0.16), rng.gauss(0, 0.16), rng.gauss(0, 0.12))))
        return pts

    n_branches = rng.randint(11, 14)
    for i in range(n_branches):
        angle = 2 * math.pi * i / n_branches + rng.uniform(-0.3, 0.3)
        k = rng.uniform(0.38, 0.95)
        base = Vector((lean[0] * k ** 2, lean[1] * k ** 2, height * k))
        out = Vector((math.cos(angle), math.sin(angle), 0))
        reach = rng.uniform(2.0, 3.0) * (1.2 - k * 0.55)
        # Up and out, then arching over.
        pts = []
        for j in range(6):
            t = j / 5
            pts.append(tuple(base + out * reach * t + Vector((0, 0, reach * (0.9 * t - 0.75 * t * t)))))
        branch(pts, 0.09, bark)
        for j in range(1, 6):
            p = Vector(pts[j])
            side = out.cross(Vector((0, 0, 1))) * rng.uniform(-0.6, 0.6)
            twig = droop(p, out + side + Vector((0, 0, 0.2)), rng.uniform(1.2, 2.2), 6)
            branch(twig, 0.02, bark)
    # A few sprays at the crown.
    for _ in range(5):
        a = rng.uniform(0, 2 * math.pi)
        droop(trunk[-1], (math.cos(a), math.sin(a), 0.6), rng.uniform(0.7, 1.2), 4)

    mesh = bpy.data.meshes.new("leaves")
    leaves.to_mesh(mesh)
    obj = bpy.data.objects.new("leaves", mesh)
    bpy.context.scene.collection.objects.link(obj)
    mesh.materials.append(leaf_material())


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
    "tree1": (lambda: tree(1), 320, 320),
    "tree2": (lambda: tree(2), 320, 320),
    "tree3": (lambda: tree(3), 320, 320),
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
