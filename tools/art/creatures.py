"""Animated characters in ShadowFlare's style, rendered with studio.py.

    python tools/art/creatures.py <goblin|hero> <output folder>

(run with a Python that has the `bpy` module). Characters are built from
simple shapes hung on joints and posed in code, then rendered from the
eight directions the engine uses for every frame of every animation.
Writes <animation>_<direction>_<frame>.png and frames.txt (anchor point and
the animation list).
"""

import math
import sys
from pathlib import Path

import bpy
from mathutils import Euler, Vector

sys.path.insert(0, str(Path(__file__).resolve().parent))
import studio  # noqa: E402

SIZE = 128  # rendered frame size in pixels

# Frames (one per game tick, 20 a second) and whether each animation loops.
ANIMATIONS = {
    "idle": (12, True),
    "walk": (10, True),
    "attack": (8, False),
    "hit": (4, False),
    "death": (10, False),
}


class Rig:
    """Joints are empties; meshes hang off them and move with them."""

    def __init__(self):
        self.joints = {}
        self.rest = {}
        self.root = self.joint("root", None, (0, 0, 0))

    def joint(self, name, parent, location):
        empty = bpy.data.objects.new(name, None)
        bpy.context.scene.collection.objects.link(empty)
        if parent:
            empty.parent = self.joints[parent]
        empty.location = location
        self.joints[name] = empty
        self.rest[name] = Vector(location)
        return name

    def attach(self, obj, joint, offset=(0, 0, 0)):
        obj.parent = self.joints[joint]
        obj.location = offset
        return obj

    def pose(self, rotations, root_offset=(0, 0, 0)):
        for name, empty in self.joints.items():
            empty.rotation_euler = Euler(rotations.get(name, (0, 0, 0)))
            empty.location = self.rest[name]
        self.joints["root"].location = Vector(root_offset)


def shape(kind, size, mat, rotation=(0, 0, 0), subdivisions=2):
    if kind == "ball":
        bpy.ops.mesh.primitive_uv_sphere_add(segments=16, ring_count=10, radius=1)
    elif kind == "box":
        bpy.ops.mesh.primitive_cube_add(size=2)
    elif kind == "cylinder":
        bpy.ops.mesh.primitive_cylinder_add(vertices=12, radius=1, depth=2)
    elif kind == "cone":
        bpy.ops.mesh.primitive_cone_add(vertices=12, radius1=1, depth=2)
    obj = bpy.context.object
    obj.scale = size
    obj.rotation_euler = rotation
    if kind != "box":
        studio.smooth(obj)
    bevel = obj.modifiers.new("soft", "SUBSURF")
    bevel.levels = 1 if kind == "box" else 0
    return studio.assign(obj, mat)


def limb(rig, joint, length, radius, mat, end=None, end_mat=None, end_size=None):
    """A limb hanging down from its joint, with an optional hand or foot."""
    rig.attach(shape("cylinder", (radius, radius, length / 2), mat), joint, (0, 0, -length / 2))
    if end:
        rig.attach(shape(end, end_size, end_mat), joint, (end_size[0] * 0.4 if end == "box" else 0, 0, -length))


def build_goblin():
    skin = studio.material("skin", (0.42, 0.58, 0.20), (0.18, 0.28, 0.08), scale=12, bump=0.4)
    cloth = studio.material("cloth", (0.40, 0.30, 0.18), (0.18, 0.12, 0.07), scale=20, bump=0.3)
    iron = studio.material("iron", (0.55, 0.55, 0.55), (0.25, 0.25, 0.27), scale=30, rough=0.4, bump=0.2)
    wood = studio.material("handle", (0.40, 0.28, 0.16), scale=25)
    rig = Rig()
    rig.joint("hips", "root", (0, 0, 0.85))
    rig.joint("spine", "hips", (0, 0, 0.05))
    rig.joint("head", "spine", (0.12, 0, 0.75))
    rig.joint("arm_l", "spine", (0.05, 0.36, 0.62))
    rig.joint("arm_r", "spine", (0.05, -0.36, 0.62))
    rig.joint("leg_l", "hips", (0, 0.16, 0))
    rig.joint("leg_r", "hips", (0, -0.16, 0))
    # Body: a pot belly leaning forward, and a loincloth.
    rig.attach(shape("ball", (0.36, 0.34, 0.45), skin), "spine", (0.05, 0, 0.35))
    rig.attach(shape("cylinder", (0.33, 0.33, 0.14), cloth), "hips", (0, 0, 0.0))
    # Head: big ears, long nose.
    rig.attach(shape("ball", (0.30, 0.28, 0.27), skin), "head", (0.05, 0, 0.18))
    rig.attach(shape("cone", (0.08, 0.08, 0.16), skin, rotation=(0, math.radians(90), 0)), "head", (0.38, 0, 0.14))
    for side in (1, -1):
        rig.attach(shape("cone", (0.07, 0.11, 0.24), skin, rotation=(math.radians(-80 * side), 0, 0)), "head",
                   (0.0, 0.36 * side, 0.26))
        rig.attach(shape("ball", (0.045, 0.045, 0.045), iron), "head", (0.29, 0.10 * side, 0.24))
    limb(rig, "arm_l", 0.75, 0.08, skin, "ball", skin, (0.11, 0.11, 0.11))
    limb(rig, "arm_r", 0.75, 0.08, skin, "ball", skin, (0.11, 0.11, 0.11))
    limb(rig, "leg_l", 0.80, 0.10, skin, "box", skin, (0.18, 0.09, 0.05))
    limb(rig, "leg_r", 0.80, 0.10, skin, "box", skin, (0.18, 0.09, 0.05))
    # An axe in the right hand, pointing forward.
    rig.joint("weapon", "arm_r", (0, 0, -0.75))
    rig.attach(shape("cylinder", (0.035, 0.035, 0.55), wood, rotation=(0, math.radians(90), 0)), "weapon", (0.3, 0, 0))
    rig.attach(shape("box", (0.13, 0.025, 0.2), iron), "weapon", (0.72, 0, 0.12))
    return rig


def build_hero():
    skin = studio.material("skin", (0.80, 0.60, 0.45), (0.50, 0.33, 0.22), scale=12, bump=0.1)
    hair = studio.material("hair", (0.22, 0.14, 0.08), scale=40, bump=0.5)
    tunic = studio.material("tunic", (0.32, 0.38, 0.50), (0.14, 0.16, 0.22), scale=18, bump=0.3)
    trousers = studio.material("trousers", (0.30, 0.24, 0.18), (0.12, 0.10, 0.07), scale=18)
    leather = studio.material("leather", (0.36, 0.22, 0.12), (0.15, 0.09, 0.05), scale=25, rough=0.6)
    steel = studio.material("steel", (0.78, 0.78, 0.80), (0.40, 0.40, 0.44), scale=40, rough=0.3, bump=0.1)
    rig = Rig()
    rig.joint("hips", "root", (0, 0, 1.25))
    rig.joint("spine", "hips", (0, 0, 0.05))
    rig.joint("head", "spine", (0, 0, 0.92))
    rig.joint("arm_l", "spine", (0, 0.40, 0.80))
    rig.joint("arm_r", "spine", (0, -0.40, 0.80))
    rig.joint("leg_l", "hips", (0, 0.15, 0))
    rig.joint("leg_r", "hips", (0, -0.15, 0))
    rig.attach(shape("box", (0.22, 0.34, 0.40), tunic), "spine", (0, 0, 0.45))
    rig.attach(shape("cylinder", (0.29, 0.29, 0.07), leather), "hips", (0, 0, 0.02))
    rig.attach(shape("cylinder", (0.07, 0.07, 0.08), skin), "head", (0, 0, 0.0))
    rig.attach(shape("ball", (0.22, 0.20, 0.25), skin), "head", (0.02, 0, 0.22))
    rig.attach(shape("ball", (0.23, 0.21, 0.17), hair), "head", (-0.03, 0, 0.32))
    limb(rig, "arm_l", 0.95, 0.075, tunic, "ball", skin, (0.09, 0.09, 0.09))
    limb(rig, "arm_r", 0.95, 0.075, tunic, "ball", skin, (0.09, 0.09, 0.09))
    limb(rig, "leg_l", 1.20, 0.10, trousers, "box", leather, (0.20, 0.09, 0.07))
    limb(rig, "leg_r", 1.20, 0.10, trousers, "box", leather, (0.20, 0.09, 0.07))
    # A short sword in the right hand.
    rig.joint("weapon", "arm_r", (0, 0, -0.95))
    rig.attach(shape("cylinder", (0.03, 0.03, 0.12), leather, rotation=(0, math.radians(90), 0)), "weapon", (0.05, 0, 0))
    rig.attach(shape("box", (0.03, 0.16, 0.025), steel), "weapon", (0.18, 0, 0))
    rig.attach(shape("box", (0.48, 0.05, 0.012), steel), "weapon", (0.68, 0, 0))
    return rig


def pose(rig, animation, t, frames):
    """Rotations (radians, about X, Y, Z) per joint at time t in [0, 1)."""
    s = math.sin(2 * math.pi * t)
    c = math.cos(2 * math.pi * t)
    lean = 0.25 if "head" in rig.joints and rig.rest["hips"].z < 1.0 else 0.05  # goblins hunch
    r = {"spine": (0, lean, 0), "head": (0, -lean, 0), "weapon": (0, math.radians(-70), 0)}
    offset = (0, 0, 0)
    if animation == "idle":
        r["spine"] = (0, lean + 0.03 * s, 0)
        r["arm_l"] = (0.08, -0.10 - 0.04 * s, 0)
        r["arm_r"] = (-0.08, -0.25 - 0.04 * s, 0)
        offset = (0, 0, 0.015 * s)
    elif animation == "walk":
        swing = 0.65
        r["leg_l"] = (0, -swing * s, 0)
        r["leg_r"] = (0, swing * s, 0)
        r["arm_l"] = (0.08, swing * 0.7 * s, 0)
        r["arm_r"] = (-0.08, -0.3 - swing * 0.5 * s, 0)
        r["spine"] = (0, lean + 0.12, 0.08 * s)
        offset = (0, 0, 0.05 * abs(c))
    elif animation == "attack":
        # Wind up over the first half, strike on the middle frame, recover.
        k = t * frames / max(1, frames - 1)
        if k < 0.5:
            raise_ = -2.5 * (k / 0.5)
        elif k < 0.65:
            raise_ = -2.5 + 2.9 * ((k - 0.5) / 0.15)
        else:
            raise_ = 0.4 - 0.6 * ((k - 0.65) / 0.35)
        r["arm_r"] = (-0.15, raise_, 0)
        r["arm_l"] = (0.15, 0.3, 0)
        r["spine"] = (0, lean + (0.25 if k >= 0.5 else -0.1), -0.25 if k < 0.5 else 0.2)
        r["leg_l"] = (0, -0.3, 0)
        r["leg_r"] = (0, 0.25, 0)
    elif animation == "hit":
        k = math.sin(math.pi * min(1.0, t * frames / max(1, frames - 1)))
        r["spine"] = (0, lean - 0.45 * k, 0)
        r["head"] = (0, -lean - 0.3 * k, 0)
        r["arm_l"] = (0.4 * k, -0.6 * k, 0)
        r["arm_r"] = (-0.4 * k, -0.6 * k, 0)
        offset = (-0.12 * k, 0, 0)
    elif animation == "death":
        k = min(1.0, t * frames / max(1, frames - 1))
        fall = k * k
        r["root"] = (0, -math.radians(88) * fall, 0)
        r["arm_l"] = (0.6 * k, -1.2 * k, 0)
        r["arm_r"] = (-0.6 * k, -1.4 * k, 0)
        r["leg_l"] = (0, -0.3 * k, 0)
        offset = (-0.3 * fall, 0, 0.12 * fall)
    rig.pose(r, offset)
    if animation == "death":
        rig.joints["root"].rotation_euler = Euler(r["root"])


def facing(direction):
    """Blender Z rotation that makes a model facing +X look towards one of
    the engine's directions (0 = screen down, then counter-clockwise)."""
    sx, sy = math.sin(math.radians(45 * direction)), math.cos(math.radians(45 * direction))
    x = (sx / studio.PROJ[0] + sy / studio.PROJ[1]) / 2
    y = (sy / studio.PROJ[1] - sx / studio.PROJ[0]) / 2
    return math.atan2(-y, x)


def main():
    preview = "--preview" in sys.argv
    args = [a for a in sys.argv if a != "--preview"]
    kind, out = args[-2], Path(args[-1])
    out.mkdir(parents=True, exist_ok=True)
    scene = studio.reset(samples=24)
    rig = build_goblin() if kind == "goblin" else build_hero()
    height = 2.4 if kind == "goblin" else 2.9
    studio.camera(scene, SIZE, SIZE, target=(0, 0, height * 0.42))
    ax, ay = studio.anchor(scene)
    lines = [f"anchor {ax:.0f} {ay:.0f}"]
    for name, (frames, loop) in ANIMATIONS.items():
        lines.append(f"animation {name} {frames} {'loop' if loop else 'once'}")
        for d in range(8):
            for f in ([frames // 2] if preview else range(frames)):
                pose(rig, name, f / frames, frames)
                spin = rig.joints["root"].rotation_euler
                rig.joints["root"].rotation_euler = Euler((spin.x, spin.y, facing(d)))
                studio.render(scene, str(out / f"{name}_{d}_{f}.png"))
        print(name, "done", flush=True)
    (out / "frames.txt").write_text("\n".join(lines) + "\n")


if __name__ == "__main__":
    main()
