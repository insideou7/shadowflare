"""Animated characters in ShadowFlare's style, rendered with studio.py.

    python tools/art/creatures.py [--preview] [--blend] <kind> <output folder>

where kind is goblin, hero, fly, ghoul, shaman, golem, spider, bigspider,
swordsman, archer, ogre, demon, dragon, boar, boarrider, skeleton,
barbarian, lizard or person<N> (a townsperson from the
PEOPLE table).

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
from town import mesh  # noqa: E402

SIZE = {"golem": 300, "shaman": 200, "bigspider": 200, "ogre": 220, "demon": 300, "dragon": 400}  # rendered frame size in pixels, 160 unless listed
HEIGHT = {"goblin": 3.8, "hero": 3.5, "fly": 2.4, "ghoul": 3.4, "shaman": 4.6, "golem": 9.5, "spider": 2.4,
          "bigspider": 3.6, "ogre": 4.8, "demon": 6.0, "dragon": 9.0}  # for framing

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
    beat = math.sin(2 * math.pi * t * frames / getattr(rig, "beat", 2))  # one beat every two frames
    rest, depth = getattr(rig, "wing_rest", -0.9), getattr(rig, "wing_depth", 0.5)
    r = {"wing_l": (rest - depth * beat, 0, 0), "wing_r": (-rest + depth * beat, 0, 0)}
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


def segment(rig, joint, a, b, radius, mat):
    """A cylinder from point a to point b, in the joint's own space."""
    a, b = Vector(a), Vector(b)
    obj = shape("cylinder", (radius, radius, (b - a).length / 2), mat)
    obj.rotation_euler = Vector((0, 0, 1)).rotation_difference(b - a).to_euler()
    return rig.attach(obj, joint, tuple((a + b) / 2))


def build_spider(scale=1.0):
    """A huge cave spider: a round red abdomen marked with black, a small
    dark head with yellow fangs, and eight long blue-grey legs that arch
    high above the body, banded red at the joints."""
    leg = studio.material("leg", (0.40, 0.50, 0.62), (0.03, 0.04, 0.06), scale=22, rough=0.7, bump=0.6,
                          band=(0.45, 0.62))
    band = studio.material("band", (0.55, 0.05, 0.04), (0.20, 0.02, 0.02), scale=20, rough=0.8)
    belly = studio.material("belly", (0.78, 0.07, 0.05), (0.04, 0.02, 0.02), scale=8, rough=0.7, bump=0.8,
                            band=(0.42, 0.54))
    head = studio.material("head", (0.09, 0.10, 0.12), (0.02, 0.02, 0.03), scale=16, rough=0.8, bump=0.6)
    fang = studio.material("fang", (0.90, 0.68, 0.12), scale=40, rough=0.4)
    tip = studio.material("tip", (0.70, 0.62, 0.42), scale=40, rough=0.6)
    rig = Rig()
    rig.kind = "spider"
    rig.joint("body", "root", (0, 0, 1.25))
    rig.attach(shape("ball", (0.42, 0.38, 0.30), head), "body", (0.15, 0, 0.0))
    rig.attach(shape("ball", (0.66, 0.60, 0.54), belly), "body", (-0.72, 0, 0.26))
    rig.attach(shape("ball", (0.20, 0.24, 0.16), head), "body", (0.55, 0, -0.02))
    for side in (1, -1):
        rig.attach(shape("cone", (0.06, 0.06, 0.14), fang, rotation=(0, math.radians(160), 0)), "body",
                   (0.70, 0.08 * side, -0.16))
    rig.legs = []
    for k, angle in enumerate((35, 70, 110, 150)):
        for side in (1, -1):
            name = f"hip{k}{'l' if side > 0 else 'r'}"
            a = math.radians(angle) * side
            out_ = Vector((math.cos(a), math.sin(a), 0))
            rig.joint(name, "body", tuple(out_ * 0.30 + Vector((0.12, 0, 0))))
            reach = 1.0 - 0.08 * abs(k - 1.5)
            knee = out_ * 1.15 * reach + Vector((0, 0, 0.95))
            ankle = out_ * 2.40 * reach + Vector((0, 0, 0.45))
            foot = out_ * 2.65 * reach + Vector((0, 0, -1.25))
            segment(rig, name, (0, 0, 0), knee, 0.10, leg)
            segment(rig, name, knee, ankle, 0.095, leg)
            segment(rig, name, ankle, foot, 0.07, leg)
            # Red bands just past each joint.
            for a_, b_, r_ in ((knee, ankle, 0.105), (ankle, foot, 0.08)):
                segment(rig, name, a_ + (b_ - a_) * 0.04, a_ + (b_ - a_) * 0.22, r_, band)
            segment(rig, name, foot + (ankle - foot) * 0.12, foot, 0.07, tip)
            rig.legs.append((name, a, k, side))
    rig.joints["root"].scale = (scale, scale, scale)
    return rig


def spider_pose(rig, animation, t, frames):
    """Legs step in two alternating sets of four; the body rears up to bite
    and flops down with its legs curled when it dies."""
    s = math.sin(2 * math.pi * t)
    k = min(1.0, t * frames / max(1, frames - 1))
    r = {}
    offset = (0, 0, 0)
    body = (0, 0, 0)
    for name, a, n, side in rig.legs:
        phase = 1 if (n + (side > 0)) % 2 else -1
        swing = lift = 0.0
        if animation == "idle":
            lift = -0.03 * s * phase
        elif animation == "walk":
            swing = 0.28 * s * phase * side
            lift = -0.22 * max(0.0, math.cos(2 * math.pi * t) * phase)
        elif animation == "attack":
            if n == 0:
                lift = -0.9 * math.sin(math.pi * k)
        elif animation == "death":
            lift = -1.1 * k
        # A small lift about the axis across the leg raises its foot.
        r[name] = (-lift * math.sin(a), lift * math.cos(a), swing)
    if animation == "idle":
        offset = (0, 0, 0.03 * s)
    elif animation == "walk":
        offset = (0, 0, 0.05 * abs(math.cos(2 * math.pi * t)))
    elif animation == "attack":
        rear = math.sin(math.pi * k)
        body = (0, -0.45 * rear if k < 0.6 else -0.45 * rear + 0.3, 0)
        offset = (0.35 * rear, 0, 0.15 * rear)
    elif animation == "hit":
        jolt = math.sin(math.pi * k)
        body = (0, -0.2 * jolt, 0)
        offset = (-0.25 * jolt, 0, 0.1 * jolt)
    elif animation == "death":
        offset = (0, 0, -0.95 * k * k)
        body = (0, 0.15 * k, 0)
    r["body"] = body
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


def build_ogre():
    """A bald, heavy-set ogre with tan skin, two stubby horns, a ragged loincloth and a huge double-bladed axe
    of rusty iron."""
    skin = studio.material("skin", (0.78, 0.52, 0.28), (0.36, 0.20, 0.08), scale=5, rough=0.5, bump=0.3)
    cloth = studio.material("cloth", (0.18, 0.17, 0.18), (0.05, 0.05, 0.05), scale=18, bump=0.4, stretch=(1, 1, 4))
    iron = studio.material("iron", (0.40, 0.38, 0.40), (0.08, 0.07, 0.08), scale=24, rough=0.35, bump=0.4,
                           spot=0.45, spot_colour=(0.40, 0.06, 0.05))
    horn = studio.material("horn", (0.66, 0.64, 0.60), (0.30, 0.29, 0.27), scale=30, rough=0.5)
    wood = studio.material("haft", (0.24, 0.16, 0.10), scale=25, rough=0.6, stretch=(1, 1, 4))
    rig = Rig(lean=0.28, weapon=-80, stance=0.18)
    rig.joint("hips", "root", (0, 0, 1.0))
    rig.joint("spine", "hips", (0, 0, 0.05))
    rig.joint("head", "spine", (0.24, 0, 1.00))
    rig.joint("arm_l", "spine", (0.02, 0.50, 0.82))
    rig.joint("arm_r", "spine", (0.02, -0.50, 0.82))
    rig.joint("leg_l", "hips", (0, 0.24, 0))
    rig.joint("leg_r", "hips", (0, -0.24, 0))
    rig.attach(shape("ball", (0.40, 0.42, 0.44), skin), "spine", (0.08, 0, 0.38))  # belly
    rig.attach(shape("ball", (0.36, 0.54, 0.30), skin), "spine", (0.0, 0, 0.78))  # chest and shoulders
    rig.attach(shape("cylinder", (0.36, 0.38, 0.12), cloth), "hips", (0.04, 0, 0.0))
    rig.attach(shape("cylinder", (0.37, 0.39, 0.04), iron), "hips", (0.04, 0, 0.10))
    rig.attach(shape("box", (0.04, 0.22, 0.26), cloth, rotation=(0, math.radians(-8), 0)), "hips", (0.34, 0, -0.22))
    rig.attach(shape("ball", (0.19, 0.17, 0.20), skin), "head", (0.05, 0, 0.10))
    rig.attach(shape("ball", (0.10, 0.12, 0.08), skin), "head", (0.17, 0, 0.0))  # jaw
    for side in (1, -1):
        rig.attach(shape("cone", (0.04, 0.04, 0.10), horn, rotation=(math.radians(-25 * side), 0, 0)), "head",
                   (0.02, 0.13 * side, 0.30))
    limb(rig, "arm_l", 1.0, 0.13, skin, "ball", skin, (0.14, 0.13, 0.14))
    limb(rig, "arm_r", 1.0, 0.13, skin, "ball", skin, (0.14, 0.13, 0.14))
    for side in ("arm_l", "arm_r"):
        rig.attach(shape("ball", (0.19, 0.19, 0.24), skin), side, (0, 0, -0.18))
        rig.attach(shape("cylinder", (0.13, 0.13, 0.07), iron), side, (0, 0, -0.80))
    limb(rig, "leg_l", 0.95, 0.15, skin, "box", skin, (0.22, 0.11, 0.07))
    limb(rig, "leg_r", 0.95, 0.15, skin, "box", skin, (0.22, 0.11, 0.07))
    for side in ("leg_l", "leg_r"):
        rig.attach(shape("ball", (0.19, 0.19, 0.28), skin), side, (0.02, 0, -0.26))
        rig.attach(shape("cylinder", (0.15, 0.15, 0.10), cloth), side, (0, 0, -0.82))
    # The axe: a long haft with a crescent blade on either side of its head.
    rig.joint("weapon", "arm_r", (0, 0, -1.0))
    rig.attach(shape("cylinder", (0.05, 0.05, 1.0), wood, rotation=(0, math.radians(90), 0)), "weapon", (0.55, 0, 0))
    for side in (1, -1):
        blade = rig.attach(shape("ball", (0.46, 0.70, 0.035), iron), "weapon", (1.25, 0.55 * side, 0))
        studio.displace(blade, 0.03, 0.2, seed=3)
    rig.attach(shape("cone", (0.06, 0.06, 0.14), iron, rotation=(0, math.radians(90), 0)), "weapon", (1.66, 0, 0))
    rig.arms = "guard"  # the axe held up over his shoulder
    rig.joints["root"].scale = (1.35, 1.35, 1.35)
    return rig


def wing(rig, joint, side, span, bone, skin, base=(0, 0, 0)):
    """A bat's wing: red bones fanning out from a wrist, with a membrane
    stretched between them and back to the body."""
    base = Vector(base)
    shoulder = base
    wrist = base + Vector((-0.1, 0.45 * side, 0.30)) * span
    tips = [base + Vector(v) * span for v in ((-0.05, 0.95 * side, 0.85), (-0.35, 1.15 * side, 0.45),
                                              (-0.65, 1.05 * side, 0.0), (-0.85, 0.70 * side, -0.35))]
    back = base + Vector((-0.55, 0.20 * side, -0.55)) * span
    segment(rig, joint, shoulder, wrist, 0.05 * span, bone)
    for t in tips:
        segment(rig, joint, wrist, t, 0.025 * span, bone)
    points = [shoulder, wrist] + tips + [back]
    # Each finger's membrane sags a little between the bones.
    verts, faces = [tuple(p) for p in points], []
    for i in range(len(tips) - 1):
        mid = (tips[i] + tips[i + 1]) / 2 + (wrist - (tips[i] + tips[i + 1]) / 2) * 0.22
        verts.append(tuple(mid))
        faces += [(1, 2 + i, len(verts) - 1), (1, len(verts) - 1, 3 + i)]
    faces += [(0, 1, len(points) - 2), (0, len(points) - 2, len(points) - 1)]
    obj = mesh(verts, faces, skin, "membrane")
    return rig.attach(obj, joint)


def build_demon():
    """A red, bat-winged demon that flies: a wiry red body, dark legs, a
    long tail hanging down and huge tan wing membranes on red bones."""
    red = studio.material("red", (0.50, 0.06, 0.05), (0.16, 0.02, 0.02), scale=14, rough=0.45, bump=0.6)
    dark = studio.material("dark", (0.20, 0.10, 0.08), (0.06, 0.03, 0.03), scale=20, rough=0.5, bump=0.6)
    skin = studio.material("membrane", (0.86, 0.74, 0.54), (0.42, 0.34, 0.26), scale=3, rough=0.7, bump=0.0)
    skin.use_backface_culling = False
    rig = Rig()
    rig.joint("body", "root", (0, 0, 3.0))
    rig.joint("head", "body", (0.08, 0, 0.55))
    rig.attach(shape("ball", (0.24, 0.30, 0.38), red), "body", (0, 0, 0.18))
    rig.attach(shape("ball", (0.18, 0.22, 0.26), dark), "body", (0, 0, -0.30))
    rig.attach(shape("ball", (0.14, 0.12, 0.16), red), "head", (0.04, 0, 0.06))
    for side in (1, -1):
        rig.attach(shape("cone", (0.03, 0.03, 0.10), dark, rotation=(math.radians(-30 * side), -0.4, 0)), "head",
                   (-0.02, 0.08 * side, 0.20))
        name = f"leg0{'l' if side > 0 else 'r'}"
        rig.joint(name, "body", (0, 0.13 * side, -0.40))
        segment(rig, name, (0, 0, 0), (0.12, 0.03 * side, -0.50), 0.08, dark)
        segment(rig, name, (0.12, 0.03 * side, -0.50), (-0.05, 0.03 * side, -0.95), 0.06, dark)
        arm = f"arm{'l' if side > 0 else 'r'}"
        rig.joint(arm, "body", (0.05, 0.28 * side, 0.30))
        segment(rig, arm, (0, 0, 0), (0.30, 0.10 * side, -0.35), 0.05, red)
        rig.attach(shape("ball", (0.06, 0.06, 0.06), red), arm, (0.32, 0.10 * side, -0.38))
    # The tail hangs down in a lazy curve.
    points = [(-0.10 - 0.06 * i, 0, -0.45 - 0.20 * i + 0.012 * i * i) for i in range(8)]
    for i in range(7):
        segment(rig, "body", points[i], points[i + 1], 0.06 - 0.006 * i, dark)
    for side in (1, -1):
        name = "wing_l" if side > 0 else "wing_r"
        rig.joint(name, "body", (-0.10, 0.18 * side, 0.36))
        wing(rig, name, side, 1.7, red, skin)
    rig.beat, rig.wing_rest, rig.wing_depth = 12, 0.0, 0.45  # slow, deep strokes
    rig.joints["body"].scale = (1.55, 1.55, 1.55)
    rig.joints["root"].scale = (1.1, 1.1, 1.1)
    return rig


def build_dragon():
    """The silver dragon: a great armoured beast on four clawed legs, rows
    of spikes down its back, a long neck and tail, and huge bat wings of
    dark, purple-black skin held high over its back."""
    silver = studio.material("silver", (0.68, 0.66, 0.62), (0.14, 0.13, 0.13), scale=9, detail=8, rough=0.4, bump=0.9,
                             band=(0.35, 0.65))
    scales = studio.material("scales", (0.52, 0.50, 0.48), (0.12, 0.11, 0.11), scale=40, rough=0.6, bump=1.0,
                             band=(0.35, 0.60))
    spike = studio.material("spike", (0.86, 0.84, 0.80), (0.40, 0.39, 0.37), scale=30, rough=0.4)
    skin = studio.material("dragonwing", (0.20, 0.15, 0.22), (0.06, 0.04, 0.07), scale=4, rough=0.7, bump=0.0)
    skin.use_backface_culling = False
    eye = studio.material("eye", (0.95, 0.70, 0.20), scale=60, rough=0.3)
    rig = Rig()
    rig.kind = "dragon"
    rig.joint("body", "root", (0, 0, 3.0))
    plates = rig.attach(shape("ball", (2.3, 1.45, 1.30), silver), "body", (0, 0, 0))
    studio.displace(plates, 0.08, 0.6, seed=4)
    rig.attach(shape("ball", (2.1, 1.25, 1.05), scales), "body", (0, 0, -0.35))
    # Ridged armour bands across the back.
    for k in range(6):
        x = -1.6 + 0.65 * k
        rig.attach(shape("ball", (0.30, 1.0 - 0.06 * abs(k - 2.5), 0.30), silver), "body",
                   (x, 0, 0.95 - 0.04 * abs(k - 2.5)))
    # Neck, head and jaw reach forward; the neck joint lunges to bite.
    rig.joint("neck", "body", (1.9, 0, 0.45))
    points = [(0.55 * i, 0, 0.30 * i - 0.05 * i * i) for i in range(6)]
    for i in range(5):
        segment(rig, "neck", points[i], points[i + 1], 0.62 - 0.05 * i, silver)
        rig.attach(shape("ball", (0.62 - 0.05 * i,) * 3, silver), "neck", points[i + 1])
    rig.joint("head", "neck", points[-1])
    rig.attach(shape("ball", (0.75, 0.45, 0.40), silver), "head", (0.35, 0, 0.05))
    rig.attach(shape("box", (0.55, 0.26, 0.16), silver), "head", (0.95, 0, -0.10))
    rig.attach(shape("box", (0.50, 0.22, 0.10), scales), "head", (0.85, 0, -0.34))  # jaw
    for side in (1, -1):
        rig.attach(shape("ball", (0.07, 0.07, 0.05), eye), "head", (0.75, 0.24 * side, 0.18))
        rig.attach(shape("cone", (0.10, 0.10, 0.55), spike, rotation=(math.radians(-15 * side), math.radians(-60), 0)),
                   "head", (-0.25, 0.28 * side, 0.45))
    # The tail sweeps back and down to a barbed tip.
    rig.joint("tail", "body", (-2.0, 0, 0.10))
    points = [(-0.8 * i, 0.05 * i * i * 0.3, -0.32 * i + 0.02 * i * i) for i in range(9)]
    for i in range(8):
        segment(rig, "tail", points[i], points[i + 1], 0.55 - 0.06 * i, silver)
        rig.attach(shape("ball", (0.55 - 0.06 * i,) * 3, silver), "tail", points[i + 1])
    rig.attach(shape("cone", (0.20, 0.08, 0.45), spike, rotation=(0, math.radians(-90), 0)), "tail",
               (points[-1][0] - 0.3, points[-1][1], points[-1][2]))
    # Spikes down the spine, neck and tail.
    for k in range(7):
        rig.attach(shape("cone", (0.12, 0.12, 0.40 - 0.03 * abs(k - 3)), spike, rotation=(0, math.radians(-25), 0)),
                   "body", (-1.8 + 0.6 * k, 0, 1.15))
    for i in range(1, 5):
        rig.attach(shape("cone", (0.10, 0.10, 0.30), spike, rotation=(0, math.radians(-30), 0)), "tail",
                   (points[i][0], 0, points[i][2] + 0.45 - 0.05 * i))
    for side in (1, -1):
        rig.attach(shape("cone", (0.12, 0.12, 0.45), spike, rotation=(math.radians(-50 * side), 0, 0)), "body",
                   (-1.0, 1.0 * side, 0.55))
        rig.attach(shape("cone", (0.12, 0.12, 0.45), spike, rotation=(math.radians(-50 * side), 0, 0)), "body",
                   (0.3, 1.1 * side, 0.55))
    # Four legs: a heavy thigh, a shin and a clawed foot.
    rig.legs = []
    for front in (1, -1):
        for side in (1, -1):
            name = f"leg{'f' if front > 0 else 'b'}{'l' if side > 0 else 'r'}"
            rig.joint(name, "body", (1.3 * front, 0.95 * side, -0.4))
            rig.attach(shape("ball", (0.65, 0.42, 0.75), silver), name, (0, 0.10 * side, -0.30))
            segment(rig, name, (0.1 * front, 0.12 * side, -0.6), (0.30 * front, 0.15 * side, -2.4), 0.26, scales)
            rig.attach(shape("ball", (0.42, 0.28, 0.14), scales), name, (0.48 * front, 0.15 * side, -2.50))
            for c in (-1, 0, 1):
                rig.attach(shape("cone", (0.06, 0.06, 0.20), spike, rotation=(0, math.radians(100), 0)), name,
                           (0.48 * front + 0.42, 0.15 * side + 0.14 * c, -2.56))
            rig.legs.append((name, front, side))
    for side in (1, -1):
        name = "wing_l" if side > 0 else "wing_r"
        rig.joint(name, "body", (0.5, 0.85 * side, 0.85))
        # A long upper arm lifts the wing well clear of the back.
        segment(rig, name, (0, 0, 0), (-0.3, 0.3 * side, 2.2), 0.24, silver)
        rig.attach(shape("ball", (0.32, 0.32, 0.32), silver), name, (-0.3, 0.3 * side, 2.2))
        wing(rig, name, side, 4.6, silver, skin, base=(-0.3, 0.3 * side, 2.2))
    rig.beat, rig.wing_rest, rig.wing_depth = 12, 0.10, 0.25  # held high over the back
    rig.joints["root"].scale = (0.8, 0.8, 0.8)
    return rig


def dragon_pose(rig, animation, t, frames):
    """Wings beat slowly overhead; the dragon stalks on four legs, snaps
    forward with its neck to bite, and slumps down when it dies."""
    s = math.sin(2 * math.pi * t)
    k = min(1.0, t * frames / max(1, frames - 1))
    beat = math.sin(2 * math.pi * t * frames / rig.beat)
    lift = rig.wing_rest - rig.wing_depth * beat
    r = {"wing_l": (lift, 0, 0), "wing_r": (-lift, 0, 0), "tail": (0, 0, 0.12 * s)}
    offset = (0, 0, 0.04 * s)
    if animation == "walk":
        for name, front, side in rig.legs:
            r[name] = (0, 0.35 * s * front * side, 0)
        offset = (0, 0, 0.08 * abs(math.cos(2 * math.pi * t)))
    elif animation == "attack":
        bite = math.sin(math.pi * k)
        r["neck"] = (0, 0.45 * bite, 0)
        r["head"] = (0, -0.25 * bite, 0)
        r["body"] = (0, 0.08 * bite, 0)
    elif animation == "hit":
        jolt = math.sin(math.pi * k)
        r["neck"] = (0, -0.25 * jolt, 0)
        offset = (-0.25 * jolt, 0, 0)
    elif animation == "death":
        r["wing_l"] = (lift * (1 - k) - 0.9 * k, 0, 0)
        r["wing_r"] = (-(lift * (1 - k) - 0.9 * k), 0, 0)
        r["neck"] = (0, 0.6 * k, 0)
        for name, front, side in rig.legs:
            r[name] = (0.6 * side * k, 0, 0)
        offset = (0, 0, -1.4 * k * k)
    rig.pose(r, offset)


BUILDERS = {"goblin": lambda: build_goblin(), "hero": lambda: build_hero(), "fly": lambda: build_fly(),
            "ghoul": lambda: build_ghoul(), "shaman": lambda: build_shaman(), "golem": lambda: build_golem(),
            "spider": lambda: build_spider(0.58), "bigspider": lambda: build_spider(0.88),
            "ogre": lambda: build_ogre(), "demon": lambda: build_demon(), "dragon": lambda: build_dragon()}


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
        if spec.get("hood_deep"):  # pulled low over the brow
            rig.attach(shape("ball", (0.20, 0.21, 0.12), hood), "head", (0.04, 0, 0.36))
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


BANDITS = {
    "swordsman": dict(width=1.05, skin="dark", hairstyle="hood", hood=(0.26, 0.22, 0.66), top=(0.22, 0.06, 0.36),
                      legs=(0.36, 0.38, 0.46), coat=(0.22, 0.06, 0.36)),
    "archer": dict(width=1.05, skin="dark", hairstyle="hood", hood=(0.26, 0.22, 0.66), top=(0.22, 0.24, 0.30),
                   legs=(0.34, 0.36, 0.42)),
}


def build_bandit(kind):
    """A bandit in a purple hood: the swordsman in a purple coat with steel
    pauldrons and a long sword, the archer in grey mail with a blue bow and
    a quiver on his back. Both crouch, ready to fight."""
    rig = build_person(dict(BANDITS[kind], hood_deep=True))
    rig.lean, rig.stance = 0.22, 0.12
    steel = studio.material("steel", (0.80, 0.80, 0.84), (0.36, 0.36, 0.42), scale=40, rough=0.3, bump=0.1)
    boots = studio.material("tallboots", (0.42, 0.24, 0.10), (0.16, 0.09, 0.04), scale=25, rough=0.6)
    leather = studio.material("grip", (0.30, 0.18, 0.09), scale=25, rough=0.6)
    for side in ("leg_l", "leg_r"):
        rig.attach(shape("cylinder", (0.15, 0.15, 0.24), boots), side, (0, 0, -0.84))
        rig.attach(shape("cylinder", (0.17, 0.17, 0.05), boots), side, (0, 0, -0.60))
    if kind == "swordsman":
        for side in ("arm_l", "arm_r"):
            rig.attach(shape("ball", (0.17, 0.18, 0.13), steel), side, (0, 0, 0.02))
            rig.attach(shape("cylinder", (0.11, 0.11, 0.14), steel), side, (0, 0, -0.62))
        # A white collar at the neck.
        collar = studio.material("collar", (0.92, 0.92, 0.90), scale=30)
        rig.attach(shape("cylinder", (0.17, 0.19, 0.06), collar), "spine", (0, 0, 0.86))
        rig.arms = "guard"
        rig.weapon = math.radians(70)
        rig.attach(shape("cylinder", (0.03, 0.03, 0.12), leather, rotation=(0, math.radians(90), 0)), "weapon",
                   (0.04, 0, 0))
        rig.attach(shape("box", (0.03, 0.15, 0.025), steel), "weapon", (0.17, 0, 0))
        rig.attach(shape("box", (0.55, 0.05, 0.012), steel), "weapon", (0.74, 0, 0))
    else:
        mail = studio.material("mail", (0.40, 0.42, 0.50), (0.06, 0.07, 0.09), scale=60, rough=0.5, bump=0.6,
                               band=(0.40, 0.60))
        rig.attach(shape("ball", (0.26, 0.40, 0.30), mail), "spine", (0.01, 0, 0.58))
        bow = studio.material("bow", (0.12, 0.22, 0.62), (0.04, 0.06, 0.20), scale=30, rough=0.4)
        gold = studio.material("gold", (0.85, 0.62, 0.20), scale=40, rough=0.3)
        string = studio.material("string", (0.90, 0.90, 0.86), scale=40)
        quiver = studio.material("quiver", (0.70, 0.46, 0.22), (0.40, 0.24, 0.10), scale=12, rough=0.6,
                                 stretch=(1, 1, 3))
        # The bow is held upright in the left hand, its belly facing forward.
        rig.joint("bow", "arm_l", (0.05, 0, -0.85))
        rig.arms = "bow"
        radius, points = 1.15, []
        for i in range(9):
            a = math.radians(-55 + 110 * i / 8)
            points.append((radius * math.cos(a) - radius + 0.12, 0, radius * math.sin(a)))
        for i in range(8):
            segment(rig, "bow", points[i], points[i + 1], 0.035 if i in (0, 7) else 0.045, gold if i in (3, 4) else bow)
        segment(rig, "bow", points[0], points[-1], 0.008, string)
        # A quiver slung across the back, arrows showing at the top.
        rig.attach(shape("cylinder", (0.13, 0.13, 0.52), quiver, rotation=(math.radians(25), 0, 0)), "spine",
                   (-0.30, 0, 0.55))
        for dy in (-0.04, 0.04):
            rig.attach(shape("cone", (0.05, 0.02, 0.08), string, rotation=(math.radians(25), 0, 0)), "spine",
                       (-0.30, -0.18 + dy, 0.98))
        rig.weapon = math.radians(-80)
    return rig


BUILDERS["swordsman"] = lambda: build_bandit("swordsman")
BUILDERS["archer"] = lambda: build_bandit("archer")
HEIGHT["swordsman"] = HEIGHT["archer"] = 3.6

def build_boar(rider=False):
    """A huge wild boar: a heavy grey-brown body with a ridge of bristles,
    a long snout with upcurved tusks, thin legs and a tufted tail. With a
    rider, an orc in a horned helm and iron pauldrons sits astride it,
    holding a long halberd."""
    hide = studio.material("hide", (0.40, 0.37, 0.34), (0.12, 0.11, 0.10), scale=50, detail=10, rough=0.8, bump=1.2,
                           band=(0.35, 0.70))
    bristle = studio.material("bristle", (0.22, 0.20, 0.19), (0.06, 0.05, 0.05), scale=60, rough=0.9, bump=0.8)
    tusk = studio.material("tusk", (0.90, 0.86, 0.74), (0.55, 0.50, 0.40), scale=30, rough=0.4)
    hoof = studio.material("hoof", (0.10, 0.09, 0.08), scale=20, rough=0.5)
    snout = studio.material("snout", (0.50, 0.38, 0.36), (0.25, 0.17, 0.16), scale=20, rough=0.6)
    eye = studio.material("eye", (0.95, 0.85, 0.70), scale=60, rough=0.3)
    rig = Rig()
    rig.kind = "boar"
    rig.joint("body", "root", (0, 0, 1.25))
    rig.attach(shape("ball", (1.35, 0.68, 0.72), hide), "body", (0, 0, 0))
    rig.attach(shape("ball", (0.80, 0.72, 0.80), hide), "body", (0.65, 0, 0.12))  # heavy shoulders
    for k in range(9):
        x = 1.1 - 0.28 * k
        rig.attach(shape("cone", (0.10, 0.06, 0.18 + 0.04 * (k < 4)), bristle, rotation=(0, math.radians(-35), 0)),
                   "body", (x, 0, 0.82 - 0.03 * k - 0.1 * (k > 5)))
    rig.joint("head", "body", (1.25, 0, 0.15))
    rig.attach(shape("ball", (0.55, 0.45, 0.48), hide), "head", (0.15, 0, -0.05))
    segment(rig, "head", (0.35, 0, -0.10), (1.05, 0, -0.35), 0.22, hide)
    rig.attach(shape("cylinder", (0.17, 0.19, 0.04), snout, rotation=(0, math.radians(70), 0)), "head", (1.07, 0, -0.36))
    for side in (1, -1):
        segment(rig, "head", (0.85, 0.16 * side, -0.42), (1.05, 0.30 * side, -0.18), 0.04, tusk)
        rig.attach(shape("ball", (0.05, 0.05, 0.05), eye), "head", (0.55, 0.26 * side, 0.08))
        rig.attach(shape("cone", (0.10, 0.06, 0.16), hide, rotation=(math.radians(-30 * side), math.radians(-20), 0)),
                   "head", (0.10, 0.30 * side, 0.40))
    rig.joint("tail", "body", (-1.30, 0, 0.25))
    segment(rig, "tail", (0, 0, 0), (-0.35, 0, -0.45), 0.04, hide)
    rig.attach(shape("ball", (0.07, 0.07, 0.12), bristle), "tail", (-0.38, 0, -0.52))
    rig.legs = []
    for front in (1, -1):
        for side in (1, -1):
            name = f"leg{'f' if front > 0 else 'b'}{'l' if side > 0 else 'r'}"
            rig.joint(name, "body", (0.75 * front, 0.38 * side, -0.35))
            rig.attach(shape("ball", (0.30, 0.20, 0.38), hide), name, (0, 0, -0.10))
            segment(rig, name, (0, 0, -0.30), (0.04 * front, 0, -0.85), 0.09, hide)
            rig.attach(shape("cylinder", (0.10, 0.10, 0.07), hoof), name, (0.06 * front, 0, -0.88))
            rig.legs.append((name, front, side))
    if rider:
        skin = studio.material("orcskin", (0.66, 0.46, 0.26), (0.30, 0.18, 0.08), scale=5, rough=0.5, bump=0.3)
        iron = studio.material("darkiron", (0.36, 0.36, 0.38), (0.06, 0.06, 0.07), scale=24, rough=0.3, bump=0.5)
        leather = studio.material("leather", (0.36, 0.18, 0.10), (0.14, 0.07, 0.03), scale=20, rough=0.6)
        horn = studio.material("horn", (0.86, 0.82, 0.72), (0.45, 0.42, 0.36), scale=30, rough=0.5)
        wood = studio.material("haft", (0.40, 0.20, 0.10), scale=25, rough=0.6, stretch=(1, 1, 4))
        steel = studio.material("steel", (0.84, 0.84, 0.88), (0.36, 0.36, 0.42), scale=40, rough=0.3, bump=0.1)
        rig.joint("rider", "body", (0.05, 0, 0.62))
        rig.attach(shape("cylinder", (0.55, 0.62, 0.06), leather), "rider", (0, 0, -0.05))  # saddle cloth
        rig.attach(shape("ball", (0.30, 0.36, 0.42), skin), "rider", (0.0, 0, 0.45))
        rig.attach(shape("ball", (0.28, 0.40, 0.26), leather), "rider", (0.0, 0, 0.25))
        for side in (1, -1):
            rig.attach(shape("ball", (0.20, 0.17, 0.30), skin), "rider", (0.15, 0.32 * side, 0.0))  # thighs astride
            rig.attach(shape("ball", (0.17, 0.20, 0.10), iron), "rider", (0.0, 0.40 * side, 0.76))  # pauldrons
            rig.attach(shape("cylinder", (0.12, 0.12, 0.06), leather), "rider", (0.0, 0.40 * side, 0.62))
            for j in (-1, 0, 1):
                rig.attach(shape("cone", (0.04, 0.04, 0.14), iron, rotation=(math.radians(-60 * side), 0.4 * j, 0)), "rider",
                           (0.12 * j, 0.52 * side, 0.84))
        rig.joint("rhead", "rider", (0.08, 0, 0.95))
        rig.attach(shape("ball", (0.20, 0.18, 0.21), skin), "rhead", (0.04, 0, 0.08))
        rig.attach(shape("ball", (0.21, 0.20, 0.13), iron), "rhead", (0.0, 0, 0.18))
        for side in (1, -1):
            rig.attach(shape("cone", (0.05, 0.05, 0.16), horn, rotation=(math.radians(-35 * side), 0, 0)), "rhead",
                       (0.0, 0.17 * side, 0.32))
        for side, name in ((1, "rarm_l"), (-1, "rarm_r")):
            rig.joint(name, "rider", (0.0, 0.38 * side, 0.72))
            limb(rig, name, 0.65, 0.09, skin, "ball", skin, (0.10, 0.10, 0.10))
        # The halberd: a long haft with an axe blade, a hook and a spike.
        rig.joint("weapon", "rarm_r", (0, 0, -0.65))
        rig.attach(shape("cylinder", (0.04, 0.04, 1.4), wood, rotation=(0, math.radians(90), 0)), "weapon", (0.5, 0, 0))
        rig.attach(shape("box", (0.18, 0.02, 0.26), steel), "weapon", (1.65, 0, 0.18))
        rig.attach(shape("box", (0.10, 0.02, 0.14), steel), "weapon", (1.65, 0, -0.14))
        rig.attach(shape("cone", (0.06, 0.03, 0.30), steel, rotation=(0, math.radians(90), 0)), "weapon", (2.12, 0, 0))
        rig.weapon = math.radians(-40)
        rig.joints["rider"].scale = (1.35, 1.35, 1.35)
    rig.joints["root"].scale = (1.45, 1.45, 1.45)
    return rig


def boar_pose(rig, animation, t, frames):
    """The boar trots on four legs, lowers its head and rushes to gore,
    flinches back when hit and rolls onto its side to die. A rider sways
    with it and swings the halberd down on the attack."""
    s = math.sin(2 * math.pi * t)
    k = min(1.0, t * frames / max(1, frames - 1))
    r = {"tail": (0.2 * s, 0, 0), "head": (0, 0.04 * s, 0)}
    offset = (0, 0, 0.02 * s)
    swing = 0.0
    if animation == "walk":
        for name, front, side in rig.legs:
            r[name] = (0, 0.45 * s * front * side, 0)
        offset = (0, 0, 0.06 * abs(math.cos(2 * math.pi * t)))
    elif animation == "attack":
        gore = math.sin(math.pi * k)
        r["head"] = (0, 0.35 * gore - 0.45 * gore * (k > 0.5), 0)
        r["body"] = (0, 0.10 * gore, 0)
        offset = (0.35 * gore, 0, 0)
        swing = gore
    elif animation == "hit":
        jolt = math.sin(math.pi * k)
        r["head"] = (0, -0.3 * jolt, 0)
        offset = (-0.25 * jolt, 0, 0)
    elif animation == "death":
        r["body"] = (math.radians(80) * k * k, 0, 0)
        for name, front, side in rig.legs:
            r[name] = (0, 0.3 * front * k, 0)
        r["head"] = (0, 0.3 * k, 0)
        offset = (0, 0, -0.45 * k * k)
    if "rider" in rig.joints:
        r["rider"] = (0, 0.05 * s, 0)
        r["rarm_l"] = (0.3, -0.7, 0)
        r["rarm_r"] = (-0.2, -1.2 + 2.2 * swing * (k > 0.4) - 1.0 * swing * (k <= 0.4), 0)
        r["weapon"] = (0, rig.weapon, 0)
        if animation == "death":
            r["rider"] = (0, -1.2 * k, 0)
    rig.pose(r, offset)


def build_skeleton():
    """A walking skeleton of yellowed bone with a rusty curved sword and a
    small round wooden shield."""
    bone = studio.material("bone", (0.78, 0.76, 0.62), (0.34, 0.32, 0.24), scale=20, rough=0.6, bump=0.6)
    dark = studio.material("socket", (0.05, 0.04, 0.03), scale=10)
    rust = studio.material("rustblade", (0.86, 0.46, 0.16), (0.40, 0.16, 0.05), scale=30, rough=0.4, bump=0.4)
    wood = studio.material("shieldwood", (0.42, 0.22, 0.10), (0.18, 0.09, 0.04), scale=12, rough=0.6, stretch=(1, 6, 1))
    iron = studio.material("iron", (0.50, 0.50, 0.52), (0.16, 0.16, 0.18), scale=24, rough=0.4)
    rig = Rig(lean=0.12, weapon=60, stance=0.12)
    rig.joint("hips", "root", (0, 0, 1.0))
    rig.joint("spine", "hips", (0, 0, 0.05))
    rig.joint("head", "spine", (0.10, 0, 0.98))
    rig.joint("arm_l", "spine", (0.0, 0.36, 0.80))
    rig.joint("arm_r", "spine", (0.0, -0.36, 0.80))
    rig.joint("leg_l", "hips", (0, 0.15, 0))
    rig.joint("leg_r", "hips", (0, -0.15, 0))
    rig.attach(shape("ball", (0.20, 0.30, 0.10), bone), "hips", (0, 0, 0.02))  # pelvis
    segment(rig, "spine", (0, 0, 0), (0, 0, 0.90), 0.05, bone)
    for k in range(5):
        z = 0.42 + 0.09 * k
        w = 0.24 - 0.02 * abs(k - 2)
        ring = shape("ball", (0.17, w, 0.025), bone)
        rig.attach(ring, "spine", (0.03, 0, z))
    rig.attach(shape("cylinder", (0.03, 0.36, 0.03), bone, rotation=(0, 0, 0)), "spine", (0, 0, 0.82))  # collarbones
    rig.attach(shape("ball", (0.22, 0.19, 0.23), bone), "head", (0.03, 0, 0.14))
    rig.attach(shape("ball", (0.09, 0.10, 0.06), bone), "head", (0.13, 0, -0.03))  # jaw
    for side in (1, -1):
        rig.attach(shape("ball", (0.04, 0.045, 0.045), dark), "head", (0.18, 0.07 * side, 0.14))
    for side in ("arm_l", "arm_r"):
        segment(rig, side, (0, 0, 0), (0, 0, -0.75), 0.055, bone)
        rig.attach(shape("ball", (0.05, 0.05, 0.05), bone), side, (0, 0, -0.38))
        rig.attach(shape("ball", (0.06, 0.04, 0.07), bone), side, (0, 0, -0.78))
    for side in ("leg_l", "leg_r"):
        segment(rig, side, (0, 0, 0), (0, 0, -0.95), 0.065, bone)
        rig.attach(shape("ball", (0.06, 0.06, 0.06), bone), side, (0, 0, -0.48))
        rig.attach(shape("box", (0.11, 0.05, 0.03), bone), side, (0.06, 0, -0.97))
    rig.joint("weapon", "arm_r", (0, 0, -0.78))
    rig.attach(shape("cylinder", (0.025, 0.025, 0.09), wood, rotation=(0, math.radians(90), 0)), "weapon", (0.03, 0, 0))
    rig.attach(shape("box", (0.02, 0.10, 0.02), iron), "weapon", (0.13, 0, 0))
    blade = []
    for i in range(6):
        a = (0.15 + 0.15 * i, 0, 0.012 * i * i)
        b = (0.15 + 0.15 * (i + 1), 0, 0.012 * (i + 1) ** 2)
        segment(rig, "weapon", a, b, 0.06 - 0.005 * i, rust)
    rig.joint("shield", "arm_l", (0.12, 0.08, -0.55))
    rig.attach(shape("cylinder", (0.38, 0.38, 0.04), wood, rotation=(math.radians(90), 0, 0)), "shield", (0, 0.04, 0))
    rig.attach(shape("cylinder", (0.40, 0.40, 0.025), iron, rotation=(math.radians(90), 0, 0)), "shield", (0, 0.02, 0))
    rig.attach(shape("ball", (0.09, 0.05, 0.09), iron), "shield", (0, 0.08, 0))
    # Scraps of blackened armour: shoulder plates and a belt.
    armour = studio.material("blackarmour", (0.17, 0.16, 0.16), (0.04, 0.04, 0.04), scale=20, rough=0.4)
    for side in ("arm_l", "arm_r"):
        rig.attach(shape("ball", (0.13, 0.12, 0.08), armour), side, (0, 0, 0.0))
    rig.attach(shape("cylinder", (0.20, 0.27, 0.06), armour), "hips", (0, 0, 0.10))
    rig.arms = "guard"
    rig.joints["root"].scale = (1.3, 1.3, 1.3)
    return rig


def build_barbarian():
    """A huge bare-chested barbarian: tanned skin, a horned steel helm, a
    dark loincloth, heavy boots, a great hooked sword and a round shield
    of polished steel."""
    skin = studio.material("skin", (0.86, 0.62, 0.40), (0.42, 0.26, 0.14), scale=5, rough=0.45, bump=0.3)
    cloth = studio.material("cloth", (0.26, 0.12, 0.08), (0.08, 0.04, 0.03), scale=18, bump=0.4, stretch=(1, 1, 4))
    steel = studio.material("steel", (0.80, 0.80, 0.84), (0.26, 0.26, 0.30), scale=24, rough=0.3, bump=0.3)
    horn = studio.material("horn", (0.90, 0.88, 0.82), (0.50, 0.48, 0.44), scale=30, rough=0.4)
    boots = studio.material("boots", (0.32, 0.16, 0.08), (0.10, 0.05, 0.02), scale=20, rough=0.6)
    grip = studio.material("grip", (0.50, 0.30, 0.12), scale=25, rough=0.6)
    rig = Rig(lean=0.12, weapon=60, stance=0.18)
    rig.joint("hips", "root", (0, 0, 1.05))
    rig.joint("spine", "hips", (0, 0, 0.05))
    rig.joint("head", "spine", (0.10, 0, 1.10))
    rig.joint("arm_l", "spine", (0.0, 0.58, 0.88))
    rig.joint("arm_r", "spine", (0.0, -0.58, 0.88))
    rig.joint("leg_l", "hips", (0, 0.22, 0))
    rig.joint("leg_r", "hips", (0, -0.22, 0))
    rig.attach(shape("ball", (0.30, 0.38, 0.42), skin), "spine", (0.02, 0, 0.38))  # waist
    rig.attach(shape("ball", (0.36, 0.60, 0.36), skin), "spine", (0.02, 0, 0.82))  # chest and shoulders
    for side in (1, -1):
        rig.attach(shape("ball", (0.16, 0.22, 0.14), skin), "spine", (0.26, 0.20 * side, 0.82))  # pecs
    rig.attach(shape("cylinder", (0.34, 0.36, 0.12), cloth), "hips", (0, 0, 0.0))
    rig.attach(shape("box", (0.04, 0.24, 0.30), cloth), "hips", (0.30, 0, -0.26))
    rig.attach(shape("ball", (0.19, 0.17, 0.21), skin), "head", (0.06, 0, 0.10))
    rig.attach(shape("ball", (0.21, 0.20, 0.13), steel), "head", (0.04, 0, 0.20))
    for side in (1, -1):
        segment(rig, "head", (0.0, 0.18 * side, 0.26), (0.0, 0.34 * side, 0.46), 0.05, horn)
        segment(rig, "head", (0.0, 0.34 * side, 0.46), (-0.02, 0.32 * side, 0.66), 0.03, horn)
    for side in ("arm_l", "arm_r"):
        limb(rig, side, 1.0, 0.14, skin, "ball", skin, (0.14, 0.13, 0.14))
        rig.attach(shape("ball", (0.21, 0.20, 0.26), skin), side, (0, 0, -0.18))
        rig.attach(shape("cylinder", (0.15, 0.15, 0.08), cloth), side, (0, 0, -0.80))
    for side in ("leg_l", "leg_r"):
        limb(rig, side, 0.98, 0.16, skin, "box", boots, (0.24, 0.13, 0.08))
        rig.attach(shape("ball", (0.20, 0.20, 0.30), skin), side, (0.02, 0, -0.26))
        rig.attach(shape("cylinder", (0.18, 0.18, 0.22), boots), side, (0, 0, -0.80))
    rig.joint("weapon", "arm_r", (0, 0, -1.0))
    rig.attach(shape("cylinder", (0.04, 0.04, 0.16), grip, rotation=(0, math.radians(90), 0)), "weapon", (0.05, 0, 0))
    rig.attach(shape("box", (0.04, 0.22, 0.04), steel), "weapon", (0.22, 0, 0))
    rig.attach(shape("box", (0.90, 0.03, 0.14), steel), "weapon", (1.14, 0, 0))
    rig.attach(shape("box", (0.22, 0.03, 0.14), steel, rotation=(0, math.radians(35), 0)), "weapon", (2.02, 0, 0.14))
    for k in range(3):
        rig.attach(shape("cone", (0.05, 0.02, 0.10), steel), "weapon", (0.7 + 0.4 * k, 0, 0.18))
    rig.joint("shield", "arm_l", (0.1, 0.12, -0.6))
    rig.attach(shape("cylinder", (0.62, 0.62, 0.06), steel, rotation=(math.radians(90), 0, 0)), "shield", (0, 0.06, 0))
    rig.attach(shape("ball", (0.12, 0.08, 0.12), steel), "shield", (0, 0.13, 0))
    for k in range(8):
        a = 2 * math.pi * k / 8
        rig.attach(shape("cone", (0.04, 0.04, 0.08), horn, rotation=(math.radians(-90), 0, 0)), "shield",
                   (0.42 * math.cos(a), 0.12, 0.42 * math.sin(a)))
    rig.arms = "guard"
    rig.joints["root"].scale = (1.85, 1.85, 1.85)
    return rig


def build_lizard():
    """A lizard warrior in blackened spiked plate: a scaly reptile head
    with swept-back horns, a heavy tail, clawed feet, and a long staff
    with a curved scythe blade."""
    plate = studio.material("plate", (0.40, 0.40, 0.44), (0.03, 0.03, 0.035), scale=20, detail=8, rough=0.25, bump=0.8,
                            band=(0.45, 0.70))
    scales = studio.material("lizardscale", (0.22, 0.22, 0.24), (0.04, 0.04, 0.05), scale=50, rough=0.6, bump=1.0)
    spike = studio.material("spike", (0.82, 0.82, 0.84), (0.38, 0.38, 0.42), scale=30, rough=0.3)
    red = studio.material("red", (0.60, 0.08, 0.06), (0.25, 0.03, 0.02), scale=20, rough=0.4)
    wood = studio.material("haft", (0.20, 0.18, 0.16), scale=25, rough=0.5, stretch=(1, 1, 4))
    eye = studio.material("eye", (0.95, 0.70, 0.20), scale=60, rough=0.3)
    rig = Rig(lean=0.25, weapon=-30, stance=0.22)
    rig.joint("hips", "root", (0, 0, 1.15))
    rig.joint("spine", "hips", (0, 0, 0.05))
    rig.joint("head", "spine", (0.30, 0, 1.15))
    rig.joint("arm_l", "spine", (0.0, 0.58, 0.92))
    rig.joint("arm_r", "spine", (0.0, -0.58, 0.92))
    rig.joint("leg_l", "hips", (0, 0.26, 0))
    rig.joint("leg_r", "hips", (0, -0.26, 0))
    rig.attach(shape("ball", (0.38, 0.44, 0.50), plate), "spine", (0.02, 0, 0.50))
    rig.attach(shape("ball", (0.40, 0.58, 0.30), plate), "spine", (0.0, 0, 0.92))
    rig.attach(shape("cylinder", (0.36, 0.40, 0.06), red), "hips", (0, 0, 0.12))
    rig.attach(shape("flare", (0.40, 0.44, 0.24), plate), "hips", (0, 0, -0.12))
    for k in range(6):
        rig.attach(shape("cone", (0.08, 0.06, 0.32 - 0.02 * k), spike, rotation=(0, math.radians(-40 - 6 * k), 0)),
                   "spine", (-0.30 - 0.04 * k, 0, 1.15 - 0.15 * k))
    for side in (1, -1):
        rig.attach(shape("ball", (0.30, 0.28, 0.20), plate), "spine", (0.0, 0.62 * side, 1.0))
        for j in range(3):
            rig.attach(shape("cone", (0.06, 0.06, 0.26), spike, rotation=(math.radians(-50 * side), math.radians(-20 + 20 * j), 0)),
                       "spine", (-0.15 + 0.15 * j, 0.74 * side, 1.12))
    # Head: a long snout, jaw, eyes and horns swept back.
    rig.attach(shape("ball", (0.26, 0.22, 0.22), scales), "head", (0.05, 0, 0.12))
    segment(rig, "head", (0.15, 0, 0.10), (0.55, 0, 0.02), 0.13, scales)
    rig.attach(shape("box", (0.20, 0.10, 0.04), scales), "head", (0.40, 0, -0.08))
    for side in (1, -1):
        rig.attach(shape("ball", (0.04, 0.04, 0.035), eye), "head", (0.25, 0.14 * side, 0.20))
        segment(rig, "head", (-0.05, 0.12 * side, 0.25), (-0.45, 0.20 * side, 0.55), 0.05, spike)
        segment(rig, "head", (0.0, 0.18 * side, 0.12), (-0.40, 0.30 * side, 0.20), 0.035, spike)
    # A heavy tail sweeping down behind.
    rig.joint("tail", "hips", (-0.30, 0, -0.10))
    pts = [(-0.35 * i, 0, -0.18 * i + 0.012 * i * i) for i in range(6)]
    for i in range(5):
        segment(rig, "tail", pts[i], pts[i + 1], 0.20 - 0.035 * i, scales)
        rig.attach(shape("ball", (0.20 - 0.035 * i,) * 3, scales), "tail", pts[i + 1])
        rig.attach(shape("cone", (0.05, 0.04, 0.14), spike, rotation=(0, math.radians(-50), 0)), "tail",
                   (pts[i][0], 0, pts[i][2] + 0.20 - 0.03 * i))
    for side in ("arm_l", "arm_r"):
        limb(rig, side, 1.0, 0.14, scales, "ball", scales, (0.15, 0.13, 0.15))
        rig.attach(shape("cylinder", (0.17, 0.17, 0.26), plate), side, (0, 0, -0.68))
        for j in range(3):
            rig.attach(shape("cone", (0.05, 0.05, 0.20), spike, rotation=(0, math.radians(-90), 0)), side,
                       (-0.18, 0, -0.50 - 0.12 * j))
        rig.attach(shape("cone", (0.06, 0.06, 0.22), spike, rotation=(0, math.radians(-120), 0)), side, (-0.12, 0, -0.05))
    for side in ("leg_l", "leg_r"):
        limb(rig, side, 1.05, 0.17, scales, "box", scales, (0.26, 0.14, 0.07))
        rig.attach(shape("ball", (0.22, 0.22, 0.30), plate), side, (0.03, 0, -0.30))
        rig.attach(shape("cylinder", (0.19, 0.19, 0.24), plate), side, (0, 0, -0.80))
        for c in (-1, 0, 1):
            rig.attach(shape("cone", (0.04, 0.04, 0.12), spike, rotation=(0, math.radians(100), 0)), side,
                       (0.30, 0.08 * c, -1.08))
    # The scythe-staff: a long dark haft, a red-bound grip and a curved blade.
    rig.joint("weapon", "arm_r", (0, 0, -1.0))
    rig.attach(shape("cylinder", (0.05, 0.05, 1.3), wood, rotation=(0, math.radians(90), 0)), "weapon", (0.3, 0, 0))
    rig.attach(shape("cylinder", (0.12, 0.12, 0.22), plate, rotation=(0, math.radians(90), 0)), "weapon", (1.45, 0, 0))
    rig.attach(shape("cylinder", (0.06, 0.06, 0.10), red, rotation=(0, math.radians(90), 0)), "weapon", (0.0, 0, 0))
    rig.attach(shape("ball", (0.18, 0.18, 0.18), plate), "weapon", (1.62, 0, 0))
    pts = [(1.62 + 0.16 * i - 0.03 * i * i, 0, 0.15 + 0.22 * i) for i in range(8)]
    pts = [(1.62 + 0.30 * math.sin(a) - 0.0, 0, 0.15 + 0.95 * (1 - math.cos(a)) * 0 + 1.0 * math.sin(a * 0.9)) for a in
           [i * 0.42 for i in range(8)]]
    for i in range(7):
        segment(rig, "weapon", pts[i], pts[i + 1], 0.07 - 0.008 * i, spike)
    rig.arms = "guard"
    rig.joints["root"].scale = (2.0, 2.0, 2.0)
    return rig


BUILDERS.update({"boar": lambda: build_boar(), "boarrider": lambda: build_boar(rider=True),
                 "skeleton": build_skeleton, "barbarian": build_barbarian, "lizard": build_lizard})
HEIGHT.update({"boar": 3.6, "boarrider": 6.0, "skeleton": 3.6, "barbarian": 6.8, "lizard": 8.0})
SIZE.update({"boar": 240, "boarrider": 280, "barbarian": 300, "lizard": 340})


def pose(rig, animation, t, frames):
    """Rotations (radians, about X, Y, Z) per joint at time t in [0, 1)."""
    s = math.sin(2 * math.pi * t)
    c = math.cos(2 * math.pi * t)
    lean = rig.lean
    if "wing_l" in rig.joints and getattr(rig, "kind", None) != "dragon":
        fly_pose(rig, animation, t, frames)
        return
    if getattr(rig, "kind", None) == "dragon":
        dragon_pose(rig, animation, t, frames)
        return
    if getattr(rig, "kind", None) == "spider":
        spider_pose(rig, animation, t, frames)
        return
    if getattr(rig, "kind", None) == "boar":
        boar_pose(rig, animation, t, frames)
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
        elif arms in ("guard", "bow"):  # crouched, weapon held out in front
            forward = -1.1 if arms == "guard" else -1.45
            r["arm_r"] = (-0.25, forward - 0.04 * s, 0) if arms == "guard" else (-0.6, -1.2 - 0.03 * s, 0.4)
            r["arm_l"] = (0.35, -0.5 - 0.03 * s, 0) if arms == "guard" else (0.2, forward - 0.03 * s, 0)
            r["leg_l"] = (0.15, -0.45, 0)
            r["leg_r"] = (-0.15, 0.35, 0)
            offset = (0, 0, -0.08 + 0.015 * s)
        elif arms == "front":  # hands clasped in front
            r["arm_l"] = (-0.35, -0.55 - 0.03 * s, 0)
            r["arm_r"] = (0.35, -0.55 - 0.03 * s, 0)
        if arms not in ("guard", "bow"):
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
