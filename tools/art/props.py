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
WOOD = dict(colour=(0.86, 0.60, 0.30), dark=(0.13, 0.08, 0.04), scale=14, detail=3, bump=1.2, stretch=(4, 4, 0.08), band=(0.40, 0.58))


BARK = dict(colour=(0.58, 0.40, 0.42), dark=(0.16, 0.09, 0.10), scale=14, bump=0.6)
TRUNK = dict(colour=(0.34, 0.22, 0.24), dark=(0.10, 0.06, 0.07), scale=14, bump=0.6)
LEAF_LIGHT = (0.48, 0.60, 0.08)
LEAF_DARK = (0.04, 0.06, 0.01)


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
    """A weeping, birch-like tree in the original's manner: a thin dark
    trunk that forks into long arching limbs, hanging twigs and a haze of
    tiny leaves with the branches and ground showing through."""
    import bmesh
    rng = random.Random(seed)
    bark = studio.material("bark", **BARK)
    height = rng.uniform(10.0, 11.0)
    fork = height * rng.uniform(0.36, 0.42)
    lean = Vector((rng.uniform(-0.25, 0.25), rng.uniform(-0.25, 0.25), 0))
    trunk = [tuple(lean * (i / 6) ** 2 + Vector((0, 0, height * i / 6))) for i in range(7)]
    branch(trunk, 0.26, studio.material("trunk", **TRUNK))
    leaves = bmesh.new()
    shade = leaves.faces.layers.float.new("shade")

    def leaf(at, light):
        s = rng.uniform(0.035, 0.06)
        a, b = rng.uniform(0, math.pi), rng.uniform(0, 2 * math.pi)
        u = Vector((math.cos(b), math.sin(b), 0)) * s
        v = Vector((-math.sin(b) * math.cos(a), math.cos(b) * math.cos(a), math.sin(a))) * s * 0.6
        c = Vector(at)
        face = leaves.faces.new([leaves.verts.new(c + u), leaves.verts.new(c + v),
                                 leaves.verts.new(c - u), leaves.verts.new(c - v)])
        # Mostly dark leaves with bright ones speckled through, more on top.
        face[shade] = min(1.0, rng.random() ** 3 + light * 0.25)

    def hang(start, direction, length, n, spread=0.07, per=14):
        """A twig hanging from start, curving down, with leaves along it."""
        p, d = Vector(start), Vector(direction).normalized()
        pts = [tuple(p)]
        for i in range(n):
            d = (d + Vector((0, 0, -0.45))).normalized()
            p = p + d * (length / n)
            pts.append(tuple(p))
            for _ in range(per):
                leaf(p + Vector((rng.gauss(0, spread), rng.gauss(0, spread), rng.gauss(0, spread * 1.5))),
                     (i == 0) * 1.0)
        branch(pts, 0.012, bark)
        return pts

    def limb(base, direction, length, rise, radius, depth):
        """A limb that rises and arches over, with twigs hanging along it."""
        base, direction = Vector(base), Vector(direction).normalized()
        pts = []
        for j in range(7):
            t = j / 6
            pts.append(base + direction * length * t + Vector((0, 0, length * (rise * t - (rise + 0.5) * t * t))))
        branch([tuple(q) for q in pts], radius, bark)
        side = direction.cross(Vector((0, 0, 1)))
        for j in range(1, 7):
            for _ in range(1 if depth else 2):
                d = direction + side * rng.uniform(-1.0, 1.0) + Vector((0, 0, 0.3))
                hang(pts[j], d, rng.uniform(0.4, 0.9) * (0.6 + 0.4 * j / 6), 5)
            if depth and j in (2, 4):
                a = rng.choice((-1, 1)) * rng.uniform(0.5, 0.9)
                sub = direction * math.cos(a) + side * math.sin(a)
                limb(pts[j], sub, length * 0.45, rise * 0.8, radius * 0.5, depth - 1)
        return pts

    # Limbs spiral up the trunk from low down to the top, shorter towards
    # the top for a conical outline, each reaching out and drooping.
    n = rng.randint(16, 19)
    for i in range(n):
        angle = i * 2.4 + rng.uniform(-0.3, 0.3)
        z = fork + (height * 0.97 - fork) * (i + rng.uniform(0.1, 0.9)) / n
        k = (z - fork) / (height - fork)
        at = lean * (z / height) ** 2 + Vector((0, 0, z))
        limb(at, (math.cos(angle), math.sin(angle), 0), rng.uniform(2.8, 3.3) * (1.0 - 0.85 * k) + 0.2,
             rng.uniform(0.45, 0.75), 0.12, 1)
    for _ in range(4):
        a = rng.uniform(0, 2 * math.pi)
        hang(trunk[-1], (math.cos(a), math.sin(a), 0.8), rng.uniform(0.6, 1.0), 5)

    mesh = bpy.data.meshes.new("leaves")
    leaves.to_mesh(mesh)
    obj = bpy.data.objects.new("leaves", mesh)
    bpy.context.scene.collection.objects.link(obj)
    # The original's trees cast almost no shadow; let the leaves skip it.
    obj.visible_shadow = False
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
    """One tile's length of rough logs, along game x or y, set unevenly:
    each leans a little, stands a little forward or back, and has its
    own height."""
    rng = random.Random(seed)
    # Less fill light, so each log's shaded side reads against the next.
    bpy.context.scene.world.node_tree.nodes["Background"].inputs[1].default_value = 0.55
    mat = studio.material("logs", **WOOD)
    end_mat = studio.material("cut", colour=(0.60, 0.50, 0.36), dark=(0.30, 0.24, 0.16), scale=20, bump=0.4)
    length = 3.2
    n = 6
    for i in range(n):
        h = rng.uniform(3.2, 4.4)
        r = rng.uniform(0.23, 0.27)
        t = (i + 0.5) / n * length - length / 2 + rng.uniform(-0.04, 0.04)
        off = rng.uniform(-0.12, 0.12)
        x, y = (off, -t) if along_y else (t, off)
        tilt = (rng.uniform(-0.035, 0.035), rng.uniform(-0.035, 0.035), rng.uniform(0, 6))
        bpy.ops.mesh.primitive_cylinder_add(vertices=10, radius=r, depth=h, location=(0, 0, h / 2))
        log = bpy.context.object
        studio.smooth(studio.assign(log, mat))
        # A rough, slightly domed top.
        bpy.ops.mesh.primitive_uv_sphere_add(segments=10, ring_count=6, radius=r * 0.97, location=(0, 0, h))
        top = bpy.context.object
        top.scale = (1, 1, 0.3)
        studio.displace(top, 0.04, 0.1, seed * 50 + i + 25)
        studio.assign(top, end_mat)
        # Lean the log and its top together about the foot.
        for obj in (log, top):
            obj.rotation_euler = tilt
            obj.location = mathutils_rotate(obj.location, tilt) + Vector((x, y, 0))


def mathutils_rotate(v, euler):
    from mathutils import Euler
    v = Vector(v)
    v.rotate(Euler(euler))
    return v


PROPS = {
    "tree1": (lambda: tree(1), 320, 320),
    "tree2": (lambda: tree(2), 320, 320),
    "tree3": (lambda: tree(3), 320, 320),
    "rock1": (lambda: rock(1), 128, 128),
    "rock2": (lambda: rock(2), 128, 128),
    "fence_x": (lambda: palisade(1, False), 256, 256),
    "fence_y": (lambda: palisade(2, True), 256, 256),
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
