"""Animated characters in ShadowFlare's style, rendered with studio.py.

    python tools/art/creatures.py [--preview] [--blend] <kind> <output folder>

where kind is goblin, hero, fly, ghoul, shaman, golem or person<N> (a
townsperson from the PEOPLE table).

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

SIZE = {"golem": 300, "shaman": 200}  # rendered frame size in pixels, 160 unless listed
HEIGHT = {"goblin": 3.8, "hero": 3.5, "fly": 2.4, "ghoul": 3.4, "shaman": 4.6, "golem": 9.5}  # for framing

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

    def __init__(self, lean=0.05, weapon=-70, stance=0.0):
        self.stance = stance  # how far apart the legs stand
        self.lean = lean  # how far forward the spine hunches
        self.weapon = math.radians(weapon)  # angle the weapon is held at
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
    elif kind == "flare":  # a skirt or coat: wide at the hem, narrower at the top
        bpy.ops.mesh.primitive_cone_add(vertices=20, radius1=1, radius2=0.6, depth=2)
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
    """A squat, barrel-bodied goblin: yellow-green skin, a crest of grey
    spikes, a strap across the chest and a big two-handed axe."""
    skin = studio.material("skin", (0.62, 0.70, 0.27), (0.42, 0.50, 0.16), scale=4, bump=0.15)
    cloth = studio.material("cloth", (0.16, 0.14, 0.12), (0.06, 0.05, 0.05), scale=20, bump=0.3)
    strap = studio.material("strap", (0.62, 0.32, 0.16), (0.30, 0.13, 0.06), scale=25)
    iron = studio.material("iron", (0.72, 0.72, 0.74), (0.30, 0.30, 0.33), scale=30, rough=0.35, bump=0.3)
    wood = studio.material("handle", (0.66, 0.36, 0.16), (0.32, 0.16, 0.07), scale=25)
    rig = Rig(lean=0.2)
    rig.joint("hips", "root", (0, 0, 0.74))
    rig.joint("spine", "hips", (0, 0, 0.05))
    rig.joint("head", "spine", (0.20, 0, 0.96))
    rig.joint("arm_l", "spine", (0.05, 0.44, 0.70))
    rig.joint("arm_r", "spine", (0.05, -0.44, 0.70))
    rig.joint("leg_l", "hips", (0, 0.17, 0))
    rig.joint("leg_r", "hips", (0, -0.17, 0))
    # A big round barrel of a body, hunched shoulders, dark shorts with a studded belt.
    rig.attach(shape("ball", (0.42, 0.39, 0.52), skin), "spine", (0.02, 0, 0.42))
    rig.attach(shape("ball", (0.30, 0.50, 0.22), skin), "spine", (-0.02, 0, 0.72))
    rig.attach(shape("cylinder", (0.34, 0.34, 0.16), cloth), "hips", (0, 0, 0.0))
    for k in range(8):
        a = 2 * math.pi * k / 8
        rig.attach(shape("cone", (0.04, 0.04, 0.06), iron, rotation=(0, math.radians(90), a)), "hips",
                   (0.35 * math.cos(a), 0.35 * math.sin(a), 0.08))
    # A strap from the left shoulder across to the right hip.
    rig.attach(shape("cylinder", (0.45, 0.45, 0.045), strap, rotation=(math.radians(38), 0, 0)), "spine", (0.02, 0, 0.42))
    # A small head sunk into the shoulders, with a crest of grey spikes.
    rig.attach(shape("ball", (0.25, 0.23, 0.24), skin), "head", (0.04, 0, 0.10))
    rig.attach(shape("ball", (0.07, 0.07, 0.06), skin), "head", (0.28, 0, 0.06))
    for k in range(6):
        t = k / 5 - 0.5
        rig.attach(shape("cone", (0.06, 0.035, 0.16), iron, rotation=(0, math.radians(-30 + 60 * t - 20), 0)), "head",
                   (0.05 - 0.4 * t, 0, 0.32 - abs(t) * 0.15))
    for side in (1, -1):
        rig.attach(shape("cone", (0.05, 0.08, 0.16), skin, rotation=(math.radians(-80 * side), 0, 0)), "head",
                   (0.0, 0.27 * side, 0.14))
    limb(rig, "arm_l", 0.70, 0.12, skin, "ball", skin, (0.13, 0.13, 0.13))
    limb(rig, "arm_r", 0.70, 0.12, skin, "ball", skin, (0.13, 0.13, 0.13))
    limb(rig, "leg_l", 0.70, 0.13, skin, "box", skin, (0.20, 0.11, 0.06))
    limb(rig, "leg_r", 0.70, 0.13, skin, "box", skin, (0.20, 0.11, 0.06))
    for side in ("leg_l", "leg_r"):
        rig.attach(shape("ball", (0.11, 0.11, 0.08), iron), side, (0.06, 0, -0.38))
    # A long-handled axe with a broad double head.
    rig.joint("weapon", "arm_r", (0, 0, -0.70))
    rig.attach(shape("cylinder", (0.045, 0.045, 0.75), wood, rotation=(0, math.radians(90), 0)), "weapon", (0.35, 0, 0))
    for side in (1, -1):
        rig.attach(shape("box", (0.13, 0.03, 0.26), iron), "weapon", (0.98, 0, 0.20 * side))
    rig.attach(shape("cone", (0.04, 0.04, 0.10), iron, rotation=(0, math.radians(90), 0)), "weapon", (1.16, 0, 0))
    rig.joints["root"].scale = (1.6, 1.6, 1.6)
    return rig


def build_hero():
    skin = studio.material("skin", (0.80, 0.60, 0.45), (0.50, 0.33, 0.22), scale=12, bump=0.1)
    hair = studio.material("hair", (0.22, 0.14, 0.08), scale=40, bump=0.5)
    tunic = studio.material("tunic", (0.30, 0.36, 0.48), (0.12, 0.14, 0.20), scale=18, bump=0.3)
    trousers = studio.material("trousers", (0.22, 0.18, 0.15), (0.08, 0.07, 0.06), scale=18)
    leather = studio.material("leather", (0.36, 0.22, 0.12), (0.15, 0.09, 0.05), scale=25, rough=0.6)
    steel = studio.material("steel", (0.78, 0.78, 0.80), (0.40, 0.40, 0.44), scale=40, rough=0.3, bump=0.1)
    rig = Rig()
    rig.joint("hips", "root", (0, 0, 1.25))
    rig.joint("spine", "hips", (0, 0, 0.05))
    rig.joint("head", "spine", (0, 0, 0.92))
    rig.joint("arm_l", "spine", (0, 0.46, 0.80))
    rig.joint("arm_r", "spine", (0, -0.46, 0.80))
    rig.joint("leg_l", "hips", (0, 0.15, 0))
    rig.joint("leg_r", "hips", (0, -0.15, 0))
    # A broad chest tapering to the waist.
    rig.attach(shape("ball", (0.27, 0.46, 0.34), tunic), "spine", (0, 0, 0.62))
    rig.attach(shape("cylinder", (0.22, 0.29, 0.28), tunic), "spine", (0, 0, 0.26))
    rig.attach(shape("cylinder", (0.30, 0.30, 0.07), leather), "hips", (0, 0, 0.02))
    rig.attach(shape("cylinder", (0.07, 0.07, 0.08), skin), "head", (0, 0, 0.0))
    rig.attach(shape("ball", (0.19, 0.17, 0.22), skin), "head", (0.02, 0, 0.22))
    rig.attach(shape("ball", (0.20, 0.18, 0.15), hair), "head", (-0.03, 0, 0.32))
    limb(rig, "arm_l", 0.95, 0.11, skin, "ball", skin, (0.11, 0.11, 0.11))
    limb(rig, "arm_r", 0.95, 0.11, skin, "ball", skin, (0.11, 0.11, 0.11))
    limb(rig, "leg_l", 1.20, 0.135, trousers, "box", leather, (0.20, 0.09, 0.07))
    limb(rig, "leg_r", 1.20, 0.135, trousers, "box", leather, (0.20, 0.09, 0.07))
    # Shoulder guards, boots and a belt buckle.
    # Short sleeves over the upper arms.
    for side in ("arm_l", "arm_r"):
        rig.attach(shape("cylinder", (0.14, 0.14, 0.17), tunic), side, (0, 0, -0.12))
    for side in ("leg_l", "leg_r"):
        rig.attach(shape("cylinder", (0.15, 0.15, 0.2), leather), side, (0, 0, -1.0))
    rig.attach(shape("box", (0.03, 0.07, 0.06), steel), "hips", (0.30, 0, 0.02))
    # A short sword in the right hand.
    rig.joint("weapon", "arm_r", (0, 0, -0.95))
    rig.attach(shape("cylinder", (0.03, 0.03, 0.12), leather, rotation=(0, math.radians(90), 0)), "weapon", (0.05, 0, 0))
    rig.attach(shape("box", (0.03, 0.16, 0.025), steel), "weapon", (0.18, 0, 0))
    rig.attach(shape("box", (0.48, 0.05, 0.012), steel), "weapon", (0.68, 0, 0))
    rig.joints["root"].scale = (1.22, 1.22, 1.22)
    return rig


def veil(name, colour, alpha):
    """A see-through material for wings."""
    mat = studio.material(name, colour, scale=30, bump=0.0, rough=0.4)
    mat.node_tree.nodes["Principled BSDF"].inputs["Alpha"].default_value = alpha
    return mat


def build_fly():
    """A giant black fly that hovers at waist height: a round head with red
    eyes, a hairy thorax, a striped abdomen, dangling legs and two pairs of
    smoky, see-through wings."""
    shell = studio.material("shell", (0.12, 0.12, 0.10), (0.03, 0.03, 0.03), scale=30, rough=0.4, bump=0.4)
    stripe = studio.material("stripe", (0.30, 0.10, 0.08), (0.10, 0.03, 0.03), scale=25, rough=0.5)
    eye = studio.material("eye", (0.80, 0.18, 0.12), (0.40, 0.05, 0.04), scale=60, rough=0.2)
    wing = veil("wing", (0.26, 0.28, 0.22), 0.65)
    rig = Rig()
    rig.joint("body", "root", (0, 0, 1.5))
    rig.joint("head", "body", (0.30, 0, 0.02))
    rig.attach(shape("ball", (0.22, 0.19, 0.18), shell), "body", (0, 0, 0))
    rig.attach(shape("ball", (0.13, 0.15, 0.13), shell), "head", (0.05, 0, 0))
    for side in (1, -1):
        rig.attach(shape("ball", (0.07, 0.07, 0.08), eye), "head", (0.10, 0.09 * side, 0.03))
    # The abdomen hangs back and down in three rings.
    for k in range(3):
        mat = stripe if k == 1 else shell
        rig.attach(shape("ball", (0.17 - 0.03 * k, 0.15 - 0.03 * k, 0.13 - 0.02 * k), mat), "body",
                   (-0.24 - 0.15 * k, 0, -0.06 - 0.06 * k))
    for k in range(3):
        for side in (1, -1):
            name = f"leg{k}{'l' if side > 0 else 'r'}"
            rig.joint(name, "body", (0.10 - 0.10 * k, 0.10 * side, -0.10))
            rig.attach(shape("cylinder", (0.018, 0.018, 0.22), shell, rotation=(math.radians(25 * side), 0, 0)),
                       name, (0, 0.05 * side, -0.20))
    for side in (1, -1):
        name = "wing_l" if side > 0 else "wing_r"
        rig.joint(name, "body", (0.0, 0.12 * side, 0.14))
        rig.attach(shape("ball", (0.22, 0.48, 0.012), wing, rotation=(0, 0, math.radians(-25 * side))),
                   name, (-0.12, 0.42 * side, 0))
        rig.attach(shape("ball", (0.16, 0.34, 0.012), wing, rotation=(0, 0, math.radians(-50 * side))),
                   name, (-0.30, 0.26 * side, -0.02))
    return rig


def fly_pose(rig, animation, t, frames):
    """Wings beat on every frame; the body bobs, darts in to bite, and drops
    to the ground when it dies."""
    beat = math.sin(2 * math.pi * t * frames / 2)  # one beat every two frames
    r = {"wing_l": (-0.9 - 0.5 * beat, 0, 0), "wing_r": (0.9 + 0.5 * beat, 0, 0)}
    k = min(1.0, t * frames / max(1, frames - 1))
    offset = (0, 0, 0.10 * math.sin(2 * math.pi * t))
    for leg in ("leg0l", "leg1l", "leg2l", "leg0r", "leg1r", "leg2r"):
        r[leg] = (0, 0.2 + 0.1 * math.sin(2 * math.pi * t), 0)
    if animation == "attack":
        lunge = math.sin(math.pi * k)
        offset = (0.6 * lunge, 0, -0.3 * lunge)
        r["body"] = (0, 0.4 * lunge, 0)
    elif animation == "hit":
        offset = (-0.25 * math.sin(math.pi * k), 0, 0.1)
    elif animation == "death":
        r["wing_l"] = (0.2 + 0.5 * k, 0, 0)
        r["wing_r"] = (-0.2 - 0.5 * k, 0, 0)
        r["body"] = (math.radians(150) * k, 0, 0)
        offset = (0, 0, -1.35 * k * k)
    rig.pose(r, offset)


def build_ghoul():
    """A hunched, blue-green brute with heavy shoulders, long arms, a bald
    head with small yellow eyes and a ragged grey loincloth."""
    skin = studio.material("skin", (0.15, 0.46, 0.36), (0.03, 0.15, 0.11), scale=3, rough=0.5, bump=0.3, band=(0.2, 0.8))
    rag = studio.material("rag", (0.34, 0.34, 0.36), (0.10, 0.10, 0.12), scale=18, bump=0.4, stretch=(1, 1, 4))
    eye = studio.material("eye", (0.95, 0.85, 0.25), scale=60, rough=0.3)
    rig = Rig(lean=0.45, stance=0.2)
    rig.joint("hips", "root", (0, 0, 1.05))
    rig.joint("spine", "hips", (0, 0, 0.05))
    rig.joint("head", "spine", (0.28, 0, 0.94))
    rig.joint("arm_l", "spine", (0.05, 0.42, 0.74))
    rig.joint("arm_r", "spine", (0.05, -0.42, 0.74))
    rig.joint("leg_l", "hips", (0, 0.24, 0))
    rig.joint("leg_r", "hips", (0, -0.24, 0))
    rig.attach(shape("ball", (0.30, 0.34, 0.42), skin), "spine", (0, 0, 0.42))
    rig.attach(shape("ball", (0.30, 0.48, 0.24), skin), "spine", (0.0, 0, 0.72))
    rig.attach(shape("cylinder", (0.26, 0.28, 0.12), rag), "hips", (0, 0, 0.02))
    for side in (1, -1):
        rig.attach(shape("box", (0.04, 0.16, 0.24), rag, rotation=(0, math.radians(-6 * side), 0)), "hips",
                   (0.22, 0.10 * side, -0.24))
    rig.attach(shape("box", (0.04, 0.24, 0.20), rag), "hips", (-0.24, 0, -0.20))
    rig.attach(shape("ball", (0.17, 0.15, 0.17), skin), "head", (0.05, 0, 0.08))
    rig.attach(shape("ball", (0.10, 0.10, 0.07), skin), "head", (0.17, 0, 0.0))
    for side in (1, -1):
        rig.attach(shape("ball", (0.025, 0.025, 0.02), eye), "head", (0.19, 0.06 * side, 0.12))
    limb(rig, "arm_l", 0.95, 0.11, skin, "ball", skin, (0.13, 0.12, 0.13))
    limb(rig, "arm_r", 0.95, 0.11, skin, "ball", skin, (0.13, 0.12, 0.13))
    for side in ("arm_l", "arm_r"):
        rig.attach(shape("ball", (0.15, 0.15, 0.20), skin), side, (0, 0, -0.18))
    limb(rig, "leg_l", 0.95, 0.12, skin, "box", skin, (0.18, 0.09, 0.06))
    limb(rig, "leg_r", 0.95, 0.12, skin, "box", skin, (0.18, 0.09, 0.06))
    for side in ("leg_l", "leg_r"):
        rig.attach(shape("ball", (0.15, 0.15, 0.24), skin), side, (0.02, 0, -0.25))
    rig.joint("weapon", "arm_r", (0, 0, -0.95))
    rig.joints["root"].scale = (1.4, 1.4, 1.4)
    return rig


def build_shaman():
    """A green orc shaman: horned helmet, a ragged red cloak and a tall
    staff topped with a skull between two dark blades."""
    skin = studio.material("skin", (0.42, 0.72, 0.18), (0.10, 0.30, 0.04), scale=6, rough=0.5, bump=0.2)
    cloak = studio.material("cloak", (0.45, 0.08, 0.04), (0.14, 0.02, 0.01), scale=10, bump=0.4, stretch=(1, 1, 3))
    horn = studio.material("horn", (0.45, 0.42, 0.55), (0.14, 0.12, 0.18), scale=30, rough=0.4)
    wood = studio.material("staff", (0.70, 0.36, 0.12), (0.28, 0.10, 0.04), scale=12, stretch=(1, 1, 6), spot=0.15,
                           spot_colour=(0.95, 0.70, 0.15))
    bone = studio.material("bone", (0.86, 0.84, 0.76), (0.52, 0.50, 0.44), scale=30)
    blade = studio.material("blade", (0.20, 0.18, 0.26), (0.06, 0.05, 0.08), scale=30, rough=0.35)
    rig = Rig(lean=0.25, weapon=-45)
    rig.joint("hips", "root", (0, 0, 0.95))
    rig.joint("spine", "hips", (0, 0, 0.05))
    rig.joint("head", "spine", (0.16, 0, 0.92))
    rig.joint("arm_l", "spine", (0.02, 0.44, 0.76))
    rig.joint("arm_r", "spine", (0.02, -0.44, 0.76))
    rig.joint("leg_l", "hips", (0, 0.17, 0))
    rig.joint("leg_r", "hips", (0, -0.17, 0))
    rig.attach(shape("ball", (0.30, 0.38, 0.44), skin), "spine", (0.02, 0, 0.44))
    rig.attach(shape("ball", (0.28, 0.50, 0.22), skin), "spine", (0, 0, 0.74))
    rig.attach(shape("cylinder", (0.30, 0.31, 0.14), cloak), "hips", (0, 0, 0))
    # The cloak: a curved sheet from the shoulders to the ground behind.
    rig.attach(shape("box", (0.05, 0.40, 0.75), cloak, rotation=(0, math.radians(-12), 0)), "spine", (-0.36, 0, 0.10))
    for k in range(5):
        y = -0.32 + 0.16 * k
        rig.attach(shape("cone", (0.05, 0.09, 0.16), cloak, rotation=(0, math.radians(168), 0)), "spine",
                   (-0.46, y, -0.72 - 0.06 * (k % 2)))
    rig.attach(shape("ball", (0.19, 0.17, 0.19), skin), "head", (0.05, 0, 0.10))
    rig.attach(shape("ball", (0.09, 0.09, 0.06), skin), "head", (0.20, 0, 0.04))
    rig.attach(shape("ball", (0.20, 0.19, 0.11), horn), "head", (0.02, 0, 0.24))
    for side in (1, -1):
        rig.attach(shape("cone", (0.05, 0.05, 0.26), horn, rotation=(math.radians(-60 * side), math.radians(-20), 0)),
                   "head", (0.0, 0.26 * side, 0.36))
    limb(rig, "arm_l", 0.85, 0.11, skin, "ball", skin, (0.12, 0.12, 0.12))
    limb(rig, "arm_r", 0.85, 0.11, skin, "ball", skin, (0.12, 0.12, 0.12))
    limb(rig, "leg_l", 0.85, 0.13, skin, "box", skin, (0.19, 0.10, 0.06))
    limb(rig, "leg_r", 0.85, 0.13, skin, "box", skin, (0.19, 0.10, 0.06))
    # The staff, held upright: a long shaft, a skull and two curved blades.
    rig.joint("weapon", "arm_r", (0, 0, -0.85))
    rig.attach(shape("cylinder", (0.045, 0.045, 1.45), wood, rotation=(0, math.radians(90), 0)), "weapon", (0.85, 0, 0))
    rig.attach(shape("ball", (0.15, 0.12, 0.14), bone), "weapon", (2.36, 0, 0))
    for side in (1, -1):
        rig.attach(shape("cone", (0.06, 0.30, 0.42), blade, rotation=(0, math.radians(90), math.radians(15 * side))),
                   "weapon", (2.22, 0.28 * side, 0))
    rig.joints["root"].scale = (1.45, 1.45, 1.45)
    return rig


def build_golem():
    """A huge clay golem: a pot belly, stumpy legs with broad toes, long
    arms and fists of cracked, glowing lava rock."""
    clay = studio.material("clay", (0.88, 0.60, 0.24), (0.40, 0.20, 0.04), scale=22, detail=10, rough=0.45, bump=1.0)
    lava = studio.material("lava", (0.25, 0.06, 0.04), (0.08, 0.02, 0.02), scale=30, bump=0.9, spot=0.5,
                           spot_colour=(1.0, 0.18, 0.05))
    rig = Rig(lean=0.08, stance=0.12)
    rig.joint("hips", "root", (0, 0, 1.0))
    rig.joint("spine", "hips", (0, 0, 0.05))
    rig.joint("head", "spine", (0.22, 0, 1.40))
    rig.joint("arm_l", "spine", (0.0, 0.70, 1.30))
    rig.joint("arm_r", "spine", (0.0, -0.70, 1.30))
    rig.joint("leg_l", "hips", (0, 0.30, 0))
    rig.joint("leg_r", "hips", (0, -0.30, 0))
    belly = rig.attach(shape("ball", (0.55, 0.58, 0.66), clay), "spine", (0.12, 0, 0.55))
    studio.displace(belly, 0.06, 0.4, seed=1)
    rig.attach(shape("ball", (0.42, 0.62, 0.42), clay), "spine", (-0.04, 0, 1.10))
    rig.attach(shape("ball", (0.22, 0.20, 0.26), clay), "head", (0.04, 0, 0.12))
    limb(rig, "arm_l", 1.25, 0.15, clay)
    limb(rig, "arm_r", 1.25, 0.15, clay)
    for side in ("arm_l", "arm_r"):
        rig.attach(shape("ball", (0.21, 0.21, 0.30), clay), side, (0, 0, -0.25))
        fist = rig.attach(shape("ball", (0.34, 0.34, 0.34), lava), side, (0, 0, -1.40))
        studio.displace(fist, 0.08, 0.15, seed=2)
    limb(rig, "leg_l", 0.95, 0.20, clay, "box", clay, (0.38, 0.26, 0.10))
    limb(rig, "leg_r", 0.95, 0.20, clay, "box", clay, (0.38, 0.26, 0.10))
    for side in ("leg_l", "leg_r"):
        rig.attach(shape("ball", (0.26, 0.26, 0.36), clay), side, (0.02, 0, -0.30))
    rig.joint("weapon", "arm_r", (0, 0, -1.25))
    rig.joints["root"].scale = (2.1, 2.1, 2.1)
    return rig


BUILDERS = {"goblin": lambda: build_goblin(), "hero": lambda: build_hero(), "fly": lambda: build_fly(),
            "ghoul": lambda: build_ghoul(), "shaman": lambda: build_shaman(), "golem": lambda: build_golem()}


# Townspeople: one body built from a short description, so a whole village
# can be dressed by editing the table below. Colours are sRGB 0-1.
SKIN = {"fair": (0.86, 0.66, 0.52), "tan": (0.72, 0.50, 0.34), "pale": (0.90, 0.76, 0.66), "dark": (0.50, 0.33, 0.22)}
HAIR = {"brown": (0.30, 0.18, 0.08), "black": (0.10, 0.08, 0.07), "blond": (0.85, 0.68, 0.30), "red": (0.62, 0.24, 0.08),
        "grey": (0.62, 0.60, 0.58), "white": (0.88, 0.88, 0.86), "ginger": (0.88, 0.48, 0.16)}

# kind: (description, build) where build sets body proportions and clothes:
#   height (1 = an adult man), width, belly, skin, hair, hairstyle (short,
#   long, bun, bald, hood, cap), top, legs (trousers colour) or skirt
#   (dress colour, to the ground), coat (long open coat), apron, armour
#   (plate over the chest and shoulders), boots.
PEOPLE = {
    1: ("girl in a blue dress", dict(height=0.72, width=0.8, skin="pale", hair="ginger", hairstyle="bun",
                                     top=(0.36, 0.30, 0.80), skirt=(0.36, 0.30, 0.80), apron=(0.92, 0.92, 0.90), short_skirt=True)),
    2: ("dancer in a pale dress", dict(arms="front", height=0.92, width=0.8, skin="pale", hair="blond", hairstyle="bun",
                                       top=(0.86, 0.66, 0.62), skirt=(0.86, 0.66, 0.62))),
    6: ("smith in a leather apron", dict(width=1.15, skin="tan", hair="grey", hairstyle="short", top=(0.40, 0.36, 0.30),
                                         legs=(0.22, 0.18, 0.15), apron=(0.30, 0.20, 0.12))),
    8: ("merchant in an ochre coat", dict(width=1.15, belly=0.15, skin="tan", hairstyle="bald", top=(0.72, 0.72, 0.56),
                                          legs=(0.40, 0.36, 0.22), coat=(0.62, 0.46, 0.12))),
    9: ("maid with a white bonnet", dict(height=0.85, width=0.8, skin="pale", hair="white", hairstyle="cap",
                                         top=(0.30, 0.30, 0.34), skirt=(0.84, 0.84, 0.82), short_skirt=True)),
    10: ("knight in blue-black plate", dict(width=1.25, skin="fair", hair="blond", hairstyle="short", top=(0.20, 0.22, 0.30),
                                             legs=(0.20, 0.22, 0.30), armour=(0.30, 0.34, 0.46))),
    11: ("baker in white", dict(skin="fair", hairstyle="bald", top=(0.86, 0.82, 0.74), legs=(0.70, 0.64, 0.56),
                               apron=(0.92, 0.90, 0.86))),
    13: ("guard in dark iron", dict(width=1.2, skin="dark", hair="black", hairstyle="short", top=(0.24, 0.24, 0.26),
                                     legs=(0.18, 0.18, 0.20), armour=(0.32, 0.32, 0.34))),
    14: ("hunter in a brown jerkin", dict(width=1.05, skin="tan", hair="red", hairstyle="long", top=(0.42, 0.24, 0.12),
                                           legs=(0.24, 0.26, 0.24), coat=(0.40, 0.22, 0.10))),
    15: ("old man in a teal hood", dict(arms="out", height=0.9, skin="pale", hairstyle="hood", top=(0.20, 0.50, 0.48),
                                         legs=(0.18, 0.40, 0.40), hood=(0.20, 0.50, 0.48))),
    16: ("young man in green", dict(arms="out", skin="fair", hair="ginger", hairstyle="short", top=(0.36, 0.48, 0.32),
                                     legs=(0.40, 0.48, 0.36), coat=(0.32, 0.44, 0.30))),
    18: ("priest in grey robes", dict(arms="out", width=1.2, belly=0.2, skin="pale", hairstyle="mitre", top=(0.50, 0.50, 0.56),
                                       skirt=(0.50, 0.50, 0.56), mitre=(0.72, 0.72, 0.62))),
    19: ("woman in a green gown", dict(arms="front", height=0.95, skin="fair", hair="ginger", hairstyle="bun", top=(0.10, 0.40, 0.12),
                                        skirt=(0.10, 0.40, 0.12))),
    20: ("farmer in a waistcoat", dict(arms="out", skin="tan", hair="brown", hairstyle="cap", top=(0.56, 0.50, 0.42), legs=(0.30, 0.24, 0.18),
                                        coat=(0.40, 0.34, 0.28))),
    21: ("stout labourer", dict(arms="out", width=1.25, belly=0.2, skin="tan", hair="ginger", hairstyle="short", top=(0.56, 0.48, 0.42),
                                legs=(0.32, 0.24, 0.18))),
    22: ("innkeeper with an apron", dict(arms="out", width=1.15, belly=0.15, skin="fair", hair="brown", hairstyle="short",
                                          top=(0.82, 0.76, 0.50), legs=(0.30, 0.22, 0.14), apron=(0.40, 0.30, 0.18))),
    23: ("girl in a blue jacket", dict(height=0.85, width=0.8, skin="pale", hair="ginger", hairstyle="long",
                                       top=(0.30, 0.40, 0.62), skirt=(0.16, 0.18, 0.40), short_skirt=True)),
    24: ("old woman in a long skirt", dict(arms="front", height=0.9, skin="pale", hair="grey", hairstyle="bun", top=(0.32, 0.46, 0.40),
                                            skirt=(0.36, 0.30, 0.18))),
    25: ("woodcutter", dict(width=1.1, skin="tan", hair="black", hairstyle="short", top=(0.70, 0.66, 0.58),
                            legs=(0.30, 0.22, 0.16))),
    26: ("small boy", dict(height=0.55, width=0.8, skin="pale", hair="blond", hairstyle="short", top=(0.20, 0.30, 0.56),
                           legs=(0.80, 0.80, 0.80))),
    27: ("scholar in a grey suit", dict(arms="front", height=1.05, skin="fair", hair="brown", hairstyle="short", top=(0.62, 0.62, 0.64),
                                         legs=(0.58, 0.58, 0.60), coat=(0.66, 0.66, 0.68))),
    28: ("fortune teller", dict(arms="front", height=0.85, width=0.85, skin="pale", hairstyle="hood", top=(0.70, 0.56, 0.20),
                                skirt=(0.62, 0.44, 0.14), hood=(0.20, 0.52, 0.30))),
    29: ("cook in orange", dict(width=1.2, belly=0.2, skin="tan", hairstyle="cap", top=(0.80, 0.46, 0.18),
                                legs=(0.40, 0.30, 0.20), cap=(0.40, 0.42, 0.36))),
    30: ("young man in blue", dict(arms="front", skin="fair", hair="blond", hairstyle="short", top=(0.30, 0.38, 0.56),
                                   legs=(0.16, 0.18, 0.30))),
}


def build_person(spec):
    h = spec.get("height", 1.0)
    w = spec.get("width", 1.0)
    belly = spec.get("belly", 0.0)
    skin = studio.material("skin", SKIN[spec.get("skin", "fair")], scale=12, bump=0.1)
    top = studio.material("top", spec["top"], scale=16, bump=0.3)
    leather = studio.material("boots", (0.30, 0.20, 0.12), (0.12, 0.08, 0.05), scale=25, rough=0.6)
    rig = Rig(lean=0.03)
    rig.joint("hips", "root", (0, 0, 1.15))
    rig.joint("spine", "hips", (0, 0, 0.05))
    rig.joint("head", "spine", (0, 0, 0.88))
    rig.joint("arm_l", "spine", (0, 0.40 * w, 0.76))
    rig.joint("arm_r", "spine", (0, -0.40 * w, 0.76))
    rig.joint("leg_l", "hips", (0, 0.14 * w, 0))
    rig.joint("leg_r", "hips", (0, -0.14 * w, 0))
    rig.joint("weapon", "arm_r", (0, 0, -0.9))
    rig.attach(shape("ball", (0.24 * w, 0.38 * w, 0.32), top), "spine", (0, 0, 0.58))
    rig.attach(shape("cylinder", ((0.21 + belly) * w, 0.27 * w, 0.27), top), "spine", (belly * 0.5, 0, 0.26))
    if belly:
        rig.attach(shape("ball", ((0.24 + belly) * w, 0.30 * w, 0.26), top), "spine", (belly * 0.6, 0, 0.24))
    # Head, neck and hair.
    rig.attach(shape("cylinder", (0.07, 0.07, 0.08), skin), "head", (0, 0, 0))
    rig.attach(shape("ball", (0.20, 0.18, 0.23), skin), "head", (0.02, 0, 0.22))
    style = spec.get("hairstyle", "short")
    hair = studio.material("hair", HAIR[spec.get("hair", "brown")], scale=40, bump=0.5)
    if style in ("short", "long", "bun"):
        rig.attach(shape("ball", (0.21, 0.19, 0.15), hair), "head", (-0.04, 0, 0.33))
    if style == "long":
        rig.attach(shape("box", (0.06, 0.15, 0.20), hair), "head", (-0.14, 0, 0.12))
    if style == "bun":
        rig.attach(shape("ball", (0.09, 0.09, 0.09), hair), "head", (-0.12, 0, 0.40))
    if style == "cap":
        cap = studio.material("cap", spec.get("cap", (0.90, 0.90, 0.88)), scale=20)
        rig.attach(shape("ball", (0.20, 0.19, 0.12), cap), "head", (-0.02, 0, 0.32))
    if style == "hood":
        hood = studio.material("hood", spec.get("hood", spec["top"]), scale=14, bump=0.4)
        rig.attach(shape("ball", (0.22, 0.20, 0.25), hood), "head", (-0.04, 0, 0.24))
    if style == "mitre":
        mitre = studio.material("mitre", spec.get("mitre", (0.8, 0.8, 0.7)), scale=20)
        rig.attach(shape("cone", (0.13, 0.13, 0.30), mitre), "head", (-0.02, 0, 0.55))
    # Arms in sleeves.
    limb(rig, "arm_l", 0.85 * h, 0.10, top, "ball", skin, (0.09, 0.09, 0.09))
    limb(rig, "arm_r", 0.85 * h, 0.10, top, "ball", skin, (0.09, 0.09, 0.09))
    # Legs, or a dress down to the ground (or the knee).
    if "skirt" in spec:
        dress = studio.material("dress", spec["skirt"], scale=12, bump=0.3, stretch=(1, 1, 3))
        short = spec.get("short_skirt", False)
        length = 0.55 if short else 1.10
        rig.attach(shape("flare", (0.42 * w, 0.42 * w, length / 2), dress), "hips", (0, 0, 0.12 - length / 2))
        if short:
            stockings = studio.material("legs", (0.86, 0.84, 0.80), scale=20)
            limb(rig, "leg_l", 1.10, 0.08, stockings, "box", leather, (0.15, 0.07, 0.06))
            limb(rig, "leg_r", 1.10, 0.08, stockings, "box", leather, (0.15, 0.07, 0.06))
        else:
            limb(rig, "leg_l", 1.10, 0.07, dress, "box", leather, (0.13, 0.07, 0.05))
            limb(rig, "leg_r", 1.10, 0.07, dress, "box", leather, (0.13, 0.07, 0.05))
    else:
        legs = studio.material("legs", spec.get("legs", (0.3, 0.25, 0.2)), scale=18)
        limb(rig, "leg_l", 1.10, 0.12, legs, "box", leather, (0.18, 0.08, 0.07))
        limb(rig, "leg_r", 1.10, 0.12, legs, "box", leather, (0.18, 0.08, 0.07))
        for side in ("leg_l", "leg_r"):
            rig.attach(shape("cylinder", (0.13, 0.13, 0.16), leather), side, (0, 0, -0.95))
    if "coat" in spec:
        coat = studio.material("coat", spec["coat"], scale=12, bump=0.3, stretch=(1, 1, 3))
        # A long coat: shoulders to the knees, flaring a little, with the
        # shirt showing down the front.
        rig.attach(shape("flare", ((0.32 + belly) * w, 0.40 * w, 0.62), coat), "spine", (belly * 0.3, 0, 0.18))
        rig.attach(shape("box", (0.03, 0.10 * w, 0.42), top), "spine", ((0.30 + belly * 1.2) * w, 0, 0.30))
        for side in ("arm_l", "arm_r"):
            rig.attach(shape("cylinder", (0.12, 0.12, 0.40 * h), coat), side, (0, 0, -0.38 * h))
    if "apron" in spec:
        apron = studio.material("apron", spec["apron"], scale=18)
        rig.attach(shape("box", (0.03, 0.24 * w, 0.40), apron), "spine",
                   ((0.24 + belly * 1.1) * w, 0, 0.05))
    if "armour" in spec:
        plate = studio.material("plate", spec["armour"], scale=30, rough=0.35, bump=0.2)
        rig.attach(shape("ball", (0.28 * w, 0.40 * w, 0.30), plate), "spine", (0.02, 0, 0.60))
        for side in ("arm_l", "arm_r"):
            rig.attach(shape("ball", (0.17, 0.17, 0.14), plate), side, (0, 0, 0.0))
        for side in ("leg_l", "leg_r"):
            rig.attach(shape("cylinder", (0.14, 0.14, 0.25), plate), side, (0, 0, -0.75))
    rig.arms = spec.get("arms")
    s = 1.22 * h
    rig.joints["root"].scale = (s, s, s)
    return rig


for _n, (_desc, _spec) in PEOPLE.items():
    BUILDERS[f"person{_n}"] = (lambda spec: lambda: build_person(spec))(_spec)
    HEIGHT[f"person{_n}"] = 3.5


def pose(rig, animation, t, frames):
    """Rotations (radians, about X, Y, Z) per joint at time t in [0, 1)."""
    s = math.sin(2 * math.pi * t)
    c = math.cos(2 * math.pi * t)
    lean = rig.lean
    if "wing_l" in rig.joints:
        fly_pose(rig, animation, t, frames)
        return
    r = {"spine": (0, lean, 0), "head": (0, -lean, 0), "weapon": (0, rig.weapon, 0)}
    offset = (0, 0, 0)
    if animation == "idle":
        r["spine"] = (0, lean + 0.03 * s, 0)
        r["arm_l"] = (0.08, -0.10 - 0.04 * s, 0)
        r["arm_r"] = (-0.08, -0.25 - 0.04 * s, 0)
        arms = getattr(rig, "arms", None)
        if arms == "out":  # hands away from the body, ready to talk
            r["arm_l"] = (0.45, -0.15 - 0.04 * s, 0)
            r["arm_r"] = (-0.45, -0.15 - 0.04 * s, 0)
        elif arms == "front":  # hands clasped in front
            r["arm_l"] = (-0.35, -0.55 - 0.03 * s, 0)
            r["arm_r"] = (0.35, -0.55 - 0.03 * s, 0)
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
    if rig.stance and animation != "death":
        for leg, side in (("leg_l", 1), ("leg_r", -1)):
            x, y, z = r.get(leg, (0, 0, 0))
            r[leg] = (x + rig.stance * side, y, z)
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
    blend = "--blend" in sys.argv
    args = [a for a in sys.argv if a not in ("--preview", "--blend")]
    kind, out = args[-2], Path(args[-1])
    out.mkdir(parents=True, exist_ok=True)
    scene = studio.reset(samples=24)
    rig = BUILDERS[kind]()
    height = HEIGHT[kind]
    size = SIZE.get(kind, 160)
    studio.camera(scene, size, size, target=(0, 0, height * 0.42))
    ax, ay = studio.anchor(scene)
    if blend:
        # The model in its rest pose; frames are posed from code.
        studio.save_blend(out / f"{kind}.blend")
    lines = [f"anchor {ax:.0f} {ay:.0f}"]
    for name, (frames, loop) in ANIMATIONS.items():
        if kind.startswith("person") and name not in ("idle", "walk"):
            continue  # townspeople only stand about and stroll
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
