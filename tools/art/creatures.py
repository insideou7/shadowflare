"""Animated characters in ShadowFlare's style, rendered with studio.py.

    python tools/art/creatures.py [--preview] [--blend] [--only idle,walk] <kind> <output folder>

where kind is goblin, hero, heroine, fly, ghoul, shaman, golem, spider, bigspider,
swordsman, archer, ogre, demon, dragon, boar, boarrider, skeleton,
barbarian, lizard, scorpion, bigscorpion, fire, raptor, dog, smalldog or person<N> (a townsperson from the
PEOPLE table).

(run with a Python that has the `bpy` module). Characters are built from
simple shapes hung on joints and posed in code (the hero, the heroine and
the goblin are MakeHuman bodies driven by the same joints, and the skeleton's
bones are laid along a hidden MakeHuman body's: see mpfb_body.py, which needs
$MPFB_SRC), then rendered from the
eight directions the engine uses for every frame of every animation.
Writes <animation>_<direction>_<frame>.png and frames.txt (anchor point, the
animation list and the frame each blow lands on).

The hero is drawn in layers, like the original's player: the body into
the output folder and each piece of equipment in GEAR into layerNN/
(NN = the animation layer that items.txt's layer= names), each with its
own frames.txt that also gives every frame a draw priority. `--layers
0,5,12` renders only those layers (0 is the body).
"""

import math
import sys
from pathlib import Path

import bpy
from mathutils import Euler, Matrix, Quaternion, Vector

sys.path.insert(0, str(Path(__file__).resolve().parent))
import studio  # noqa: E402
from town import mesh  # noqa: E402

SIZE = {"goblin": 200, "hero": 200, "heroine": 200, "golem": 300, "shaman": 200, "bigspider": 200, "ogre": 220, "demon": 300, "dragon": 400}  # rendered frame size in pixels, 160 unless listed
HEIGHT = {"goblin": 3.8, "hero": 4.0, "heroine": 3.5, "fly": 2.4, "ghoul": 3.4, "shaman": 4.6, "golem": 9.5, "spider": 2.4,
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
        # joint: (lower joint, z where it starts). A limb with a knee or an
        # elbow: what is attached below that point hangs off the lower joint.
        self.splits = {}
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

    def split(self, joint, lower, z):
        """Gives a limb a knee or an elbow `z` below its joint."""
        self.joint(lower, joint, (0, 0, z))
        self.splits[joint] = (lower, z)

    def attach(self, obj, joint, offset=(0, 0, 0)):
        if joint in self.splits:
            lower, z = self.splits[joint]
            half = obj.scale.z if obj.name.startswith("Cylinder") and not any(obj.rotation_euler) else 0
            top, bottom = offset[2] + half, offset[2] - half
            if half and top - 0.05 > z > bottom + 0.05:
                # A straight piece across the bend: cut in two, each a
                # little longer so the joint shows no gap when it bends.
                overlap = min(obj.scale.x, obj.scale.y) * 0.6
                below = obj.copy()
                bpy.context.scene.collection.objects.link(below)
                obj.scale.z = (top - z + overlap) / 2
                below.scale.z = (z + overlap - bottom) / 2
                self.attach(below, lower, (offset[0], offset[1], (bottom + z + overlap) / 2 - z))
                offset = (offset[0], offset[1], (top + z - overlap) / 2)
            elif offset[2] < z:
                joint, offset = lower, (offset[0], offset[1], offset[2] - z)
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


# The player's attack is ten frames like the original's, with the blow landing
# on frame 7 (HERO_STRIKE): see HERO_ATTACK.
HERO_ANIMATIONS = dict(ANIMATIONS, attack=(10, False))
HERO_STRIKE = {"attack": 7}

# The original's player stands about 15% taller on screen than our first
# hero did (measured over the eight directions of its stance and walk).
SCALE_MH = 1.15


def build_hero():
    """The hero on a MakeHuman body (mpfb_body.py): a muscular man,
    bare-chested, in dark trousers, a belt and boots, with short brown hair.
    Its joints are the ones every rig has, so the pose code and equipment
    work as before."""
    import mpfb_body
    skin = studio.material("skin", (0.86, 0.72, 0.58), (0.72, 0.56, 0.44), scale=30, bump=0.05)
    hair = studio.material("hair", (0.16, 0.11, 0.07), (0.06, 0.04, 0.03), scale=60, bump=0.5, stretch=(1, 1, 4))
    trousers = studio.material("trousers", (0.13, 0.13, 0.14), (0.04, 0.04, 0.05), scale=18, bump=0.3)
    leather = studio.material("leather", (0.30, 0.20, 0.13), (0.12, 0.08, 0.05), scale=25, rough=0.6)
    height = 3.2 * SCALE_MH
    body, armature = mpfb_body.human(height, dict(gender=1.0, age=0.5, muscle=1.0, weight=0.7, height=0.6,
                                                   proportions=0.8, caucasian=1.0, african=0.0, asian=0.0))
    mpfb_body.bake_rest(body, armature)
    arms = ("upperarm", "lowerarm", "hand", "thumb", "index", "middle", "ring", "pinky")

    def pick(centre, bones, normal):
        z = centre.z / height
        if not any(b.startswith(arms) for b in bones):
            if z < 0.20:
                return leather  # boots
            if z < 0.555:
                return trousers
            if z < 0.585:
                return leather  # belt
        if all(b == "head" for b in bones) and (z > 0.955 or (z > 0.90 and normal.y > 0.35)):
            return hair
        return skin

    mpfb_body.paint(body, pick)
    rig = Rig()
    mpfb_body.bind(rig, body, armature, 1.22 * SCALE_MH)
    # Boots over the feet, so no toes show.
    mpfb_body.shell(body, lambda centre, part, normal: (leather, 0.035) if centre.z / height < 0.20 else None,
                    "boots")
    rig.kind = "hero"
    rig.animations = HERO_ANIMATIONS
    rig.strikes = HERO_STRIKE
    rig.body = body
    rig.height = height
    return rig


def build_goblin():
    """The goblin on a MakeHuman body (mpfb_body.py): short, thick-set and
    knotted with muscle, green-skinned with big pointed ears and a jutting
    jaw. Its hunched back is armoured in a studded black iron shell with
    big round pauldrons and a row of spikes, it wears an iron cap with a
    crest of spikes, bracers and a loincloth, and it carries a morning star:
    a wooden haft with a spiked iron head. It fights in a deep crouch, the
    morning star held low across its body."""
    import mpfb_body
    skin = studio.material("goblin skin", (0.50, 0.64, 0.22), (0.24, 0.34, 0.10), scale=30, bump=0.15)
    shell = studio.material("goblin shell", (0.15, 0.14, 0.14), (0.03, 0.03, 0.03), scale=25, rough=0.4, bump=0.5,
                            grime=0.4)
    cloth = studio.material("loincloth", (0.16, 0.13, 0.11), (0.05, 0.04, 0.03), scale=18, bump=0.3)
    iron = studio.material("iron", (0.78, 0.78, 0.80), (0.28, 0.28, 0.31), scale=30, rough=0.3, bump=0.3, grime=0.4)
    wood = studio.material("haft", (0.62, 0.34, 0.14), (0.28, 0.13, 0.05), scale=10, stretch=(12, 1, 1))
    height = 3.0
    ears = []
    for side in ("l", "r"):
        ears += [(f"ears/{side}-ear-shape-pointed.target.gz", 1.0), (f"ears/{side}-ear-scale-incr.target.gz", 1.6),
                 (f"ears/{side}-ear-wing-incr.target.gz", 1.0), (f"ears/{side}-ear-rot-backward.target.gz", 0.8),
                 (f"ears/{side}-ear-trans-up.target.gz", 0.6)]
    body, armature = mpfb_body.human(height, dict(gender=1.0, age=0.4, muscle=1.0, weight=0.8, height=0.0,
                                                   proportions=0.0, caucasian=1.0, african=0.0, asian=0.0),
                                     ears + [("chin/chin-prognathism-incr.target.gz", 1.0),
                                             ("chin/chin-width-incr.target.gz", 0.6),
                                             ("nose/nose-scale-vert-incr.target.gz", 0.8),
                                             ("nose/nose-point-down.target.gz", 0.6),
                                             ("eyebrows/eyebrows-angle-down.target.gz", 1.0),
                                             ("head/head-scale-horiz-incr.target.gz", 0.4)])
    mpfb_body.bake_rest(body, armature, arm_out=0.30, elbow_forward=0.25)
    mpfb_body.paint(body, lambda centre, bones, normal: skin)
    rig = Rig()
    scale = 1.4
    mpfb_body.bind(rig, body, armature, scale)
    rig.kind = "goblin"
    rig.height = height

    def pick(centre, part, normal):
        z = centre.z / height
        if part == "torso":
            return (shell, 0.10 if normal.y > -0.2 else 0.07) if z >= 0.555 else (cloth, 0.07)
        if part == "upperarm" and z > 0.70:
            return shell, 0.06
        if part == "thigh" and z > 0.42:
            return cloth, 0.06
        if part == "forearm" and z < 0.50:
            return iron, 0.04
        if part == "shin" and z < 0.22:
            return shell, 0.035
        if part == "head" and (z > 0.95 or (z > 0.92 and normal.y > 0.3)):
            return iron, 0.035
        return None

    mpfb_body.shell(body, pick, "goblin gear")
    # Round iron pauldrons, studs over the back, a row of spikes down it
    # and a crest of spikes on the cap.
    for side in ("arm_l", "arm_r"):
        rig.attach(shape("ball", (0.19, 0.19, 0.13), iron), side, (0, 0, 0.02))
        rig.attach(shape("ball", (0.16, 0.17, 0.10), shell), side, (0, 0, -0.10))
        rig.attach(shape("cone", (0.05, 0.05, 0.14), iron), side, (-0.02, 0, 0.16))
    for k in range(4):
        for side in (1, -1):
            rig.attach(shape("ball", (0.035, 0.035, 0.035), iron), "spine",
                       (-0.24 + 0.02 * k, side * (0.13 + 0.02 * k), 0.30 + 0.10 * k))
    for k in range(5):
        rig.attach(shape("cone", (0.06, 0.06, 0.20), iron, rotation=(0, math.radians(-110), 0)), "spine",
                   (-0.28, 0, 0.20 + 0.10 * k))
    neck = rig.joints["head"].matrix_world.translation.z
    top = (height - neck) / scale
    for k in range(5):
        t = k / 4 - 0.5
        rig.attach(shape("cone", (0.035, 0.03, 0.12), iron, rotation=(0, math.radians(-25 + 70 * t), 0)), "head",
                   (0.02 - 0.22 * t, 0, top - 0.02 - abs(t) * 0.10))
    # The morning star: a wooden haft with a spike at its butt and a
    # spiked iron ball at its head.
    g = grip(rig, 0)
    hang(shape("cylinder", (0.055, 0.055, 0.62), wood, rotation=(0, math.radians(90), 0)), g, (0.25, 0, 0))
    hang(shape("cylinder", (0.07, 0.07, 0.04), iron, rotation=(0, math.radians(90), 0)), g, (-0.34, 0, 0))
    hang(shape("cone", (0.05, 0.05, 0.12), iron, rotation=(0, math.radians(-90), 0)), g, (-0.48, 0, 0))
    hang(shape("cylinder", (0.08, 0.08, 0.05), iron, rotation=(0, math.radians(90), 0)), g, (0.82, 0, 0))
    head = Vector((1.06, 0, 0))
    hang(shape("ball", (0.22, 0.22, 0.22), iron), g, tuple(head))
    n = 18
    for k in range(n):  # spikes spread over the ball
        zz = 1 - 2 * (k + 0.5) / n
        r = math.sqrt(1 - zz * zz)
        a = k * math.pi * (3 - math.sqrt(5))
        d = Vector((r * math.cos(a), r * math.sin(a), zz))
        spike = shape("cone", (0.045, 0.045, 0.11), iron)
        spike.rotation_euler = Vector((0, 0, 1)).rotation_difference(d).to_euler()
        hang(spike, g, tuple(head + d * 0.27))
    # The left hand takes the haft too (see two_hands()): a joint in its palm.
    aw = armature.matrix_world
    palm = (aw @ armature.data.bones["hand_l"].head_local + aw @ armature.data.bones["middle_01_l"].head_local) / 2
    rig.joint("palm_l", "elbow_l", tuple((palm - rig.joints["elbow_l"].matrix_world.translation) / scale))
    rig.two_hands = (("idle", "walk", "attack"), g, Vector((0.32, 0, 0)))
    rig.poses = goblin_poses()
    return rig


def goblin_stand(animation, s, c):
    """The goblin's stance, as the original's: a deep crouch with the feet
    far apart, the back hunched and the head thrust forward, the morning
    star held low across the body in both hands."""
    r = {
        "spine": (0, 0.55 + 0.03 * s, -0.15),
        "head": (0, -0.50, 0.15),
        "arm_l": (-0.25, -0.75, 0.2),
        "elbow_l": (0, -0.75, 0.6),
        "arm_r": (-0.15, -0.35 - 0.04 * s, 0),
        "elbow_r": (0, -0.85, 0),
        "weapon": (0, math.radians(20), 0.9),
        "leg_l": (0.45, -0.75, 0.25),
        "knee_l": (0, 1.10, 0),
        "leg_r": (-0.45, -0.05, -0.25),
        "knee_r": (0, 0.95 + 0.06 * s, 0),
    }
    if animation == "walk":
        swing = 0.55
        r["spine"] = (0, 0.50, 0.08 * s)
        r["leg_l"] = (0.25, -0.45 - swing * s, 0.15)
        r["leg_r"] = (-0.25, -0.45 + swing * s, -0.15)
        r["knee_l"] = (0, 0.75 + 0.45 * max(0.0, s), 0)
        r["knee_r"] = (0, 0.75 + 0.45 * max(0.0, -s), 0)
    return r


def goblin_poses():
    """The goblin's animations, for pose()."""
    stand = goblin_stand("idle", 0.0, 1.0)
    up = hero_key(stand, spine=(0, 0.05, 0.35), head=(0, -0.05, -0.2), arm_r=(-0.3, -2.7, 0), elbow_r=(0, -0.6, 0),
                  weapon=(0, math.radians(60), 0), arm_l=(0.1, -2.5, 0), elbow_l=(0, -0.6, 0.3),
                  leg_l=(0.35, -0.45, 0.2), knee_l=(0, 0.6, 0), leg_r=(-0.35, 0.15, -0.2), knee_r=(0, 0.5, 0))
    down = hero_key(stand, spine=(0, 0.95, -0.2), head=(0, -0.85, 0.1), arm_r=(-0.1, -1.0, 0), elbow_r=(0, -0.2, 0),
                    weapon=(0, math.radians(70), 0), arm_l=(0.0, -1.0, 0.2), elbow_l=(0, -0.3, 0.3),
                    leg_l=(0.35, -1.25, 0.2), knee_l=(0, 1.5, 0), leg_r=(-0.35, 0.55, -0.2), knee_r=(0, 0.4, 0))
    return {
        "idle": lambda s, c, k: goblin_stand("idle", s, c),
        "walk": lambda s, c, k: goblin_stand("walk", s, c),
        # Up over the head, then down onto the ground in front.
        "attack": [(0.0, stand), (0.55, up), (0.72, down), (0.86, down), (1.0, stand)],
        "hit": [(0.0, stand),
                (0.35, hero_key(stand, spine=(0, 0.0, 0.25), head=(0, -0.2, 0), arm_l=(0.5, -1.5, 0),
                                arm_r=(-0.5, 0.3, 0), knee_l=(0, 0.6, 0), knee_r=(0, 0.5, 0))),
                (1.0, stand)],
        # Knocked flat on its back, arms flung out, the weapon dropped.
        "death": [(0.0, stand),
                  (0.35, hero_key(stand, root=(0, -0.6, 0), spine=(0, -0.3, 0), head=(0, -0.4, 0),
                                  arm_l=(0.8, -1.8, 0), arm_r=(-0.8, -1.8, 0), knee_l=(0, 0.6, 0), knee_r=(0, 0.6, 0))),
                  (0.75, hero_key(stand, root=(0, -1.50, 0), spine=(0, -0.1, 0), head=(0, 0.3, 0),
                                  arm_l=(1.3, -2.4, 0), elbow_l=(0, 0, 0), arm_r=(-1.3, -2.6, 0), elbow_r=(0, 0, 0),
                                  weapon=(0, 1.2, 0), leg_l=(0.35, -0.6, 0), knee_l=(0, 0.5, 0),
                                  leg_r=(-0.3, -0.2, 0), knee_r=(0, 0.3, 0))),
                  (1.0, hero_key(stand, root=(0, -1.55, 0), spine=(0, 0.0, 0), head=(0, 0.35, 0.3),
                                 arm_l=(1.4, -2.5, 0), elbow_l=(0, 0, 0), arm_r=(-1.3, -2.7, 0), elbow_r=(0, 0, 0),
                                 weapon=(0, 1.4, 0), leg_l=(0.35, -0.4, 0), knee_l=(0, 0.3, 0),
                                 leg_r=(-0.3, -0.1, 0), knee_r=(0, 0.2, 0)))],
    }


def build_hero_shapes():
    """The first hero, made of simple shapes. The item pictures
    (items.py) are still drawn on it."""
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
    for side in ("l", "r"):
        rig.split(f"arm_{side}", f"elbow_{side}", -0.475)
        rig.split(f"leg_{side}", f"knee_{side}", -0.60)
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
    # The right hand holds weapons by this joint. The body itself is
    # unarmed: what the hero wears and wields is built by build_gear and
    # rendered as layers of their own.
    rig.joint("weapon", "elbow_r", (0, 0, -0.475))
    rig.joints["root"].scale = (1.22, 1.22, 1.22)
    rig.kind = "hero"
    return rig


def build_heroine():
    """The heroine on a MakeHuman body (mpfb_body.py): slim and athletic,
    with short red-brown hair, a dark leather bodysuit cut high at the
    hips and laced sandals up the shins. The same joints and poses as the
    hero, so the same equipment fits her."""
    import mpfb_body
    skin = studio.material("skin", (0.90, 0.74, 0.62), (0.76, 0.58, 0.47), scale=30, bump=0.05)
    hair = studio.material("hair", (0.66, 0.24, 0.14), (0.34, 0.10, 0.06), scale=60, bump=0.5, stretch=(1, 1, 4))
    suit = studio.material("bodysuit", (0.32, 0.12, 0.10), (0.12, 0.04, 0.03), scale=25, rough=0.6, bump=0.3)
    leather = studio.material("leather", (0.36, 0.22, 0.14), (0.14, 0.08, 0.05), scale=25, rough=0.6)
    height = 3.15
    body, armature = mpfb_body.human(height, dict(gender=0.0, age=0.5, muscle=0.6, weight=0.4, height=0.5,
                                                   proportions=0.8, caucasian=1.0, african=0.0, asian=0.0))
    mpfb_body.bake_rest(body, armature)

    def pick(centre, bones, normal):
        z = centre.z / height
        part = mpfb_body.part(bones)
        if part == "torso" and 0.47 < z < 0.80:
            return suit
        if part == "thigh" and z > 0.50 and abs(centre.x) < 0.11 * height:
            return suit  # cut high at the hips
        if part in ("foot", "shin") and z < 0.16:
            return leather
        if part == "head" and (z > 0.96 or (z > 0.87 and normal.y > 0.3)):
            return hair
        return skin

    mpfb_body.paint(body, pick)
    rig = Rig()
    mpfb_body.bind(rig, body, armature, 1.22 * SCALE_MH * 0.855)

    def gear(centre, part, normal):
        z = centre.z / height
        if part == "head" and (z > 0.96 or (z > 0.86 and normal.y > 0.25) or (z > 0.91 and abs(normal.x) > 0.6 and normal.y > -0.1)):
            return hair, 0.035  # a short, full cut
        if part in ("foot", "shin") and z < 0.16:
            return leather, 0.02  # sandal straps over the feet and shins
        return None

    mpfb_body.shell(body, gear, "hair and sandals")
    rig.kind = "hero"
    rig.heroine = True
    rig.animations = HERO_ANIMATIONS
    rig.strikes = HERO_STRIKE
    rig.body = body
    rig.height = height
    return rig


# The hero's equipment, one animation layer each, numbered as in the
# original's Player/Male/Animation00.Caf (and items.txt's layer=): 5-8
# body armour, 9-11 shields, 12-21 weapons. Armour wraps the body parts it
# covers a little larger than they are, so that with the body held out of
# the render it still hides everything under it.
_gear_materials = {}


def gear_material(name):
    try:
        any(m.name for m in _gear_materials.values())
    except ReferenceError:  # made for a scene that studio.reset() has since cleared
        _gear_materials.clear()
    if not _gear_materials:
        m = _gear_materials
        m["cloth"] = studio.material("gear cloth", (0.46, 0.48, 0.46), (0.22, 0.24, 0.23), scale=22, bump=0.4)
        m["quilt"] = studio.material("gear quilt", (0.42, 0.44, 0.42), (0.18, 0.19, 0.18), scale=8, bump=0.6,
                                     stretch=(1, 1, 6))
        m["green"] = studio.material("gear trousers", (0.20, 0.27, 0.22), (0.08, 0.11, 0.09), scale=18)
        m["leather"] = studio.material("gear leather", (0.38, 0.24, 0.13), (0.16, 0.10, 0.05), scale=25, rough=0.6)
        m["dark"] = studio.material("gear dark leather", (0.20, 0.14, 0.10), (0.08, 0.05, 0.04), scale=25, rough=0.6)
        m["plate"] = studio.material("gear plate", (0.66, 0.67, 0.70), (0.24, 0.24, 0.27), scale=14, rough=0.32,
                                     bump=0.2, grime=0.45)
        m["chain"] = studio.material("gear chain", (0.40, 0.40, 0.42), (0.12, 0.12, 0.13), scale=140, rough=0.5,
                                     bump=0.9)
        m["steel"] = studio.material("gear steel", (0.85, 0.86, 0.88), (0.45, 0.46, 0.50), scale=40, rough=0.25,
                                     bump=0.05)
        m["brass"] = studio.material("gear brass", (0.78, 0.60, 0.28), (0.40, 0.28, 0.10), scale=30, rough=0.35)
        m["wood"] = studio.material("gear wood", (0.50, 0.32, 0.16), (0.24, 0.14, 0.06), scale=10, stretch=(12, 12, 1))
        m["oak"] = studio.material("gear oak", (0.46, 0.30, 0.15), (0.22, 0.13, 0.05), scale=6, stretch=(1, 12, 1))
        crystal = studio.material("gear crystal", (0.55, 0.80, 1.0), (0.20, 0.45, 0.85), scale=8, rough=0.15, bump=0)
        bsdf = crystal.node_tree.nodes["Principled BSDF"]
        bsdf.inputs["Emission Color"].default_value = (0.25, 0.55, 1.0, 1)
        bsdf.inputs["Emission Strength"].default_value = 1.5
        m["crystal"] = crystal
    return _gear_materials[name]


def grip(rig, angle=0.0):
    """An empty in the right hand that a weapon hangs off, tilted by
    `angle` degrees from the way the rig holds weapons."""
    empty = bpy.data.objects.new("grip", None)
    bpy.context.scene.collection.objects.link(empty)
    empty.parent = rig.joints["weapon"]
    empty.rotation_euler = (0, math.radians(angle), 0)
    return empty


def hang(obj, parent, offset):
    obj.parent = parent
    obj.location = offset
    return obj


def body_armour(rig, parts):
    """Armour cut from a MakeHuman body (mpfb_body.shell): parts maps a
    body part (or "belt", "hips" for the band below it) to (material,
    thickness in Blender units)."""
    import mpfb_body
    h = rig.height

    def pick(centre, part, normal):
        z = centre.z / h
        if part == "torso" and z < 0.585:
            part = "belt" if z >= 0.555 else "hips"
        return parts.get(part)

    mpfb_body.shell(rig.body, pick, "armour")


def gear_cloth(rig):
    """Cloth Armour: a quilted grey jacket with leather shoulders and belt,
    long sleeves, green trousers and tall boots."""
    m = gear_material
    if hasattr(rig, "body"):
        body_armour(rig, {"torso": (m("quilt"), 0.05), "upperarm": (m("quilt"), 0.045),
                          "forearm": (m("cloth"), 0.04), "belt": (m("leather"), 0.07),
                          "hips": (m("green"), 0.04), "thigh": (m("green"), 0.04),
                          "shin": (m("dark"), 0.05), "foot": (m("dark"), 0.05)})
        for side in ("arm_l", "arm_r"):
            rig.attach(shape("ball", (0.15, 0.15, 0.10), m("leather")), side, (0, 0, 0.0))
        return
    rig.attach(shape("ball", (0.31, 0.50, 0.38), m("quilt")), "spine", (0, 0, 0.62))
    rig.attach(shape("cylinder", (0.26, 0.33, 0.31), m("quilt")), "spine", (0, 0, 0.26))
    rig.attach(shape("flare", (0.37, 0.39, 0.18), m("cloth")), "hips", (0, 0, -0.14))
    rig.attach(shape("cylinder", (0.34, 0.34, 0.08), m("leather")), "hips", (0, 0, 0.02))
    rig.attach(shape("box", (0.03, 0.08, 0.07), m("brass")), "hips", (0.34, 0, 0.02))
    rig.attach(shape("cylinder", (0.14, 0.15, 0.07), m("cloth")), "head", (0, 0, -0.03))
    for side in ("arm_l", "arm_r"):
        rig.attach(shape("ball", (0.18, 0.18, 0.12), m("leather")), side, (0, 0, 0.02))
        rig.attach(shape("cylinder", (0.16, 0.16, 0.42), m("cloth")), side, (0, 0, -0.42))
        rig.attach(shape("cylinder", (0.165, 0.165, 0.1), m("leather")), side, (0, 0, -0.76))
    for side in ("leg_l", "leg_r"):
        rig.attach(shape("cylinder", (0.155, 0.155, 0.42), m("green")), side, (0, 0, -0.42))
        rig.attach(shape("cylinder", (0.17, 0.17, 0.24), m("dark")), side, (0, 0, -0.98))
        rig.attach(shape("box", (0.22, 0.105, 0.085), m("dark")), side, (0.08, 0, -1.2))


def gear_plate(rig):
    """Plate Mail: a steel cuirass and big pauldrons over mail, vambraces,
    tassets, knee cops, greaves and sabatons."""
    m = gear_material
    if hasattr(rig, "body"):
        body_armour(rig, {"torso": (m("plate"), 0.07), "neck": (m("chain"), 0.04),
                          "upperarm": (m("chain"), 0.045), "forearm": (m("plate"), 0.06),
                          "hand": (m("dark"), 0.025), "belt": (m("dark"), 0.08), "hips": (m("plate"), 0.08),
                          "thigh": (m("chain"), 0.05), "shin": (m("plate"), 0.06), "foot": (m("plate"), 0.05)})
        for side in ("arm_l", "arm_r"):
            rig.attach(shape("ball", (0.17, 0.18, 0.13), m("plate")), side, (0, 0, 0.02))
        for side in ("leg_l", "leg_r"):
            rig.attach(shape("ball", (0.12, 0.12, 0.10), m("plate")), side, (0.08, 0, rig.rest[side.replace("leg", "knee")].z))
        return
    rig.attach(shape("ball", (0.32, 0.51, 0.39), m("plate")), "spine", (0, 0, 0.62))
    rig.attach(shape("ball", (0.16, 0.30, 0.24), m("plate")), "spine", (0.17, 0, 0.60))
    rig.attach(shape("cylinder", (0.26, 0.33, 0.31), m("chain")), "spine", (0, 0, 0.26))
    for k, z in enumerate((0.40, 0.25, 0.10)):
        rig.attach(shape("cylinder", (0.29 - 0.01 * k, 0.36 - 0.01 * k, 0.065), m("plate")), "spine", (0, 0, z))
    rig.attach(shape("flare", (0.39, 0.41, 0.20), m("plate")), "hips", (0, 0, -0.16))
    rig.attach(shape("cylinder", (0.34, 0.34, 0.08), m("dark")), "hips", (0, 0, 0.02))
    rig.attach(shape("cylinder", (0.12, 0.13, 0.07), m("chain")), "head", (0, 0, -0.03))
    for side in ("arm_l", "arm_r"):
        rig.attach(shape("ball", (0.21, 0.21, 0.16), m("plate")), side, (0, 0, 0.03))
        rig.attach(shape("ball", (0.19, 0.19, 0.13), m("plate")), side, (0, 0, -0.10))
        rig.attach(shape("cylinder", (0.16, 0.16, 0.25), m("chain")), side, (0, 0, -0.30))
        rig.attach(shape("cylinder", (0.15, 0.15, 0.2), m("plate")), side, (0, 0, -0.65))
        rig.attach(shape("ball", (0.14, 0.14, 0.12), m("plate")), side, (0, 0, -0.45))
    for side in ("leg_l", "leg_r"):
        rig.attach(shape("cylinder", (0.16, 0.16, 0.28), m("plate")), side, (0, 0, -0.28))
        rig.attach(shape("ball", (0.17, 0.17, 0.13), m("plate")), side, (0.04, 0, -0.60))
        rig.attach(shape("cylinder", (0.165, 0.165, 0.24), m("plate")), side, (0, 0, -0.90))
        rig.attach(shape("box", (0.23, 0.11, 0.09), m("plate")), side, (0.08, 0, -1.2))


def gear_round_shield(rig):
    """Round Shield: an oak disc with a steel rim, a boss and brass studs,
    strapped to the left forearm and held out in front."""
    m = gear_material
    facing_out = (0, math.radians(90), 0)
    rig.attach(shape("cylinder", (0.52, 0.52, 0.03), m("steel"), rotation=facing_out), "arm_l", (0.21, 0.02, -0.62))
    rig.attach(shape("cylinder", (0.48, 0.48, 0.035), m("oak"), rotation=facing_out), "arm_l", (0.235, 0.02, -0.62))
    rig.attach(shape("ball", (0.08, 0.12, 0.12), m("steel")), "arm_l", (0.27, 0.02, -0.62))
    for k in range(8):
        a = 2 * math.pi * k / 8
        rig.attach(shape("ball", (0.03, 0.03, 0.03), m("brass")), "arm_l",
                   (0.27, 0.02 + 0.38 * math.cos(a), -0.62 + 0.38 * math.sin(a)))


def blade_weapon(rig, grip_length, guard, blade_length, blade_width, pommel, angle=0.0):
    """A sword along the grip's +X: grip, cross-guard, blade and point."""
    m = gear_material
    g = grip(rig, angle)
    hang(shape("cylinder", (0.034, 0.034, grip_length / 2), m("leather"), rotation=(0, math.radians(90), 0)), g,
         (0.04, 0, 0))
    end = 0.04 + grip_length / 2
    hang(shape("ball", (pommel, pommel, pommel), m("brass")), g, (0.04 - grip_length / 2 - pommel * 0.6, 0, 0))
    hang(shape("box", (0.035, guard, 0.03), m("brass")), g, (end + 0.03, 0, 0))
    blade = hang(shape("box", (blade_length / 2, blade_width, 0.02), m("steel")), g,
                 (end + 0.06 + blade_length / 2, 0, 0))
    blade.modifiers.clear()  # straight edges, meeting the point
    hang(shape("cone", (0.02, blade_width * 1.02, blade_width * 2.2), m("steel"), rotation=(0, math.radians(90), 0)),
         g, (end + 0.06 + blade_length + blade_width * 2.2, 0, 0))
    # A ridge down the middle of the blade, catching the light.
    ridge = hang(shape("box", (blade_length / 2, blade_width * 0.3, blade_width * 0.3), m("steel"),
                       rotation=(math.radians(45), 0, 0)), g, (end + 0.06 + blade_length / 2, 0, 0))
    ridge.modifiers.clear()


def gear_short_sword(rig):
    blade_weapon(rig, 0.22, 0.15, 0.78, 0.06, 0.045)


def gear_long_sword(rig):
    blade_weapon(rig, 0.28, 0.21, 1.15, 0.065, 0.05)


def gear_two_hand_sword(rig):
    """A great two-handed sword with a long grip and a wide blade."""
    blade_weapon(rig, 0.46, 0.32, 1.60, 0.11, 0.07, angle=15)


def gear_staff(rig):
    """Magic Staff: a long wooden staff with a blue crystal held in prongs."""
    m = gear_material
    g = grip(rig, -10)
    hang(shape("cylinder", (0.05, 0.05, 1.30), m("wood"), rotation=(0, math.radians(90), 0)), g, (0.40, 0, 0))
    hang(shape("cone", (0.05, 0.05, 0.10), m("brass"), rotation=(0, math.radians(-90), 0)), g, (-0.95, 0, 0))
    hang(shape("cylinder", (0.06, 0.06, 0.05), m("brass"), rotation=(0, math.radians(90), 0)), g, (1.66, 0, 0))
    hang(shape("ball", (0.12, 0.12, 0.12), m("crystal")), g, (1.86, 0, 0))
    for k in range(3):
        a = 2 * math.pi * k / 3
        hang(shape("cone", (0.025, 0.025, 0.14), m("brass"),
                   rotation=(math.radians(-25) * math.sin(a), math.radians(90 - 25 * math.cos(a)), 0)), g,
             (1.80, 0.09 * math.sin(a), 0.09 * math.cos(a)))


def gear_bowgun(rig):
    """Wood Bowgun: a small crossbow held by its stock like a pistol, a
    wooden tiller with an iron-bound steel prod across the front, its string
    drawn back to the nut and a bolt laid in the groove."""
    m = gear_material
    g = grip(rig, -15)
    along = (0, math.radians(90), 0)
    # The grip in the hand, then the tiller running forward from it, with
    # a short butt behind.
    hang(shape("cylinder", (0.036, 0.036, 0.12), m("dark"), rotation=along), g, (0.0, 0, 0))
    tiller = hang(shape("box", (0.36, 0.045, 0.05), m("wood")), g, (0.30, 0, 0.07))
    tiller.modifiers.clear()
    hang(shape("box", (0.10, 0.05, 0.07), m("wood")), g, (-0.12, 0, 0.05))
    hang(shape("box", (0.03, 0.05, 0.06), m("steel")), g, (0.07, 0, 0.12))  # the nut
    # The prod: two arms swept back from an iron stirrup at the front.
    hang(shape("box", (0.04, 0.06, 0.06), m("steel")), g, (0.64, 0, 0.08))
    span, sweep = 0.34, math.radians(14)
    for side in (1, -1):
        hang(shape("cylinder", (0.022, 0.022, span / 2), m("steel"),
                         rotation=(math.radians(90), 0, side * sweep)), g,
                   (0.62 - math.sin(sweep) * span / 2, side * span * math.cos(sweep) / 2, 0.08))
        tip = (0.62 - math.sin(sweep) * span, side * span * math.cos(sweep), 0.08)
        hang(shape("ball", (0.03, 0.03, 0.03), m("brass")), g, tip)
        # The string from the arm's tip back to the nut.
        dx, dy = 0.08 - tip[0], -tip[1]
        length = math.hypot(dx, dy)
        hang(shape("cylinder", (0.007, 0.007, length / 2), m("dark"),
                   rotation=(0, math.radians(90), math.atan2(dy, dx))), g,
             (tip[0] + dx / 2, tip[1] + dy / 2, 0.10))
    # The bolt in its groove, an iron head past the prod.
    hang(shape("cylinder", (0.012, 0.012, 0.30), m("oak"), rotation=along), g, (0.38, 0, 0.13))
    hang(shape("cone", (0.025, 0.025, 0.06), m("steel"), rotation=along), g, (0.72, 0, 0.13))


# layer: (name, builder, armour). Armour casts no shadow of its own: the
# body's shadow, drawn on layer 0, already covers it.
GEAR = {
    5: ("cloth", gear_cloth, True),
    6: ("plate", gear_plate, True),
    9: ("round_shield", gear_round_shield, False),
    12: ("short_sword", gear_short_sword, False),
    16: ("bowgun", gear_bowgun, False),
    17: ("long_sword", gear_long_sword, False),
    18: ("staff", gear_staff, False),
    19: ("two_hand_sword", gear_two_hand_sword, False),
}


def build_gear(rig, layer):
    """Builds one piece of equipment on the rig; returns its mesh objects."""
    before = set(bpy.data.objects)
    GEAR[layer][1](rig)
    made = [o for o in bpy.data.objects if o not in before and o.type == "MESH"]
    if getattr(rig, "heroine", False):
        # Armour is cut for the hero's broad chest: narrow what goes on the
        # heroine's body to her frame.
        for o in made:
            if o.parent in (rig.joints["spine"], rig.joints["hips"]):
                o.scale = (o.scale[0], o.scale[1] * 0.84, o.scale[2])
    return made


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
    if "tail" in rig.joints:  # a scorpion lashes its sting forward to strike
        lash = math.sin(math.pi * k) if animation == "attack" else 0.04 * s
        r["tail"] = (0, 0.6 * lash, 0)
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
    return bat_wing(rig, joint, side, span, bone, skin, (-0.1, 0.45, 0.30),
                    ((-0.05, 0.95, 0.85), (-0.35, 1.15, 0.45), (-0.65, 1.05, 0.0), (-0.85, 0.70, -0.35)),
                    (-0.55, 0.20, -0.55), base)


def bat_wing(rig, joint, side, span, bone, skin, wrist, tips, back, base=(0, 0, 0)):
    """wing() with its own shape: the wrist, the finger tips and where the
    membrane meets the body again, as left-wing points in units of span
    from the shoulder (y is mirrored for the right wing)."""
    base = Vector(base)
    shoulder = base

    def at(v):
        return base + Vector((v[0], v[1] * side, v[2])) * span

    wrist = at(wrist)
    tips = [at(v) for v in tips]
    back = at(back)
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


BUILDERS = {"goblin": lambda: build_goblin(), "hero": lambda: build_hero(), "heroine": lambda: build_heroine(), "fly": lambda: build_fly(),
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
    17: ("woodsman in a green hood", dict(height=1.15, width=1.05, skin="tan", hairstyle="hood", top=(0.40, 0.54, 0.34),
                                          legs=(0.30, 0.38, 0.26), coat=(0.28, 0.38, 0.24), hood=(0.30, 0.40, 0.26))),
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


# Cold Svalt Town's three named people, on MakeHuman bodies (mpfb_body.py)
# like the hero: kind 4 Carls the swordsman, kind 0 Jill with her handcart
# and kind 12 Wizard Izaram.
def plaid(name, colour, line, scale=4.0):
    """Cloth woven in a check: colour crossed by darker bands of `line`
    running round and down the cloth (object space), like a tartan."""
    mat = studio.material(name, colour, scale=18, bump=0.3)
    nodes, links = mat.node_tree.nodes, mat.node_tree.links
    bsdf = nodes["Principled BSDF"]
    base = bsdf.inputs["Base Color"].links[0].from_socket
    coords = nodes.new("ShaderNodeTexCoord")
    sep = nodes.new("ShaderNodeSeparateXYZ")
    links.new(coords.outputs["Object"], sep.inputs[0])
    out = base
    for axis in ("X", "Z"):
        wave = nodes.new("ShaderNodeMath")
        wave.operation = "SINE"
        mul = nodes.new("ShaderNodeMath")
        mul.operation = "MULTIPLY"
        mul.inputs[1].default_value = scale * 2 * math.pi
        if axis == "X":  # round the body: x + y, so it runs on every side
            add = nodes.new("ShaderNodeMath")
            links.new(sep.outputs["X"], add.inputs[0])
            links.new(sep.outputs["Y"], add.inputs[1])
            links.new(add.outputs[0], mul.inputs[0])
        else:
            links.new(sep.outputs["Z"], mul.inputs[0])
        links.new(mul.outputs[0], wave.inputs[0])
        band = nodes.new("ShaderNodeMath")
        band.operation = "GREATER_THAN"
        band.inputs[1].default_value = 0.6
        links.new(wave.outputs[0], band.inputs[0])
        mix = nodes.new("ShaderNodeMix")
        mix.data_type = "RGBA"
        mix.inputs["B"].default_value = (*studio.linear(line), 1)
        links.new(band.outputs[0], mix.inputs["Factor"])
        links.new(out, mix.inputs["A"])
        out = mix.outputs["Result"]
    links.new(out, bsdf.inputs["Base Color"])
    return mat


def townsperson(height, macros, pick, scale, targets=(), arm_out=0.14):
    """A townsperson on a MakeHuman body `height` units tall, painted by
    pick (see mpfb_body.paint) and bound to a Rig with root scale `scale`."""
    import mpfb_body
    body, armature = mpfb_body.human(height, macros, targets)
    mpfb_body.bake_rest(body, armature, arm_out=arm_out)
    mpfb_body.paint(body, pick)
    rig = Rig()
    mpfb_body.bind(rig, body, armature, scale)
    rig.body = body
    rig.height = height
    return rig


def stand(s, **joints):
    """A townsperson standing at ease, breathing (s is the sine of the
    idle's phase); joints override the arms and the rest."""
    r = {
        "spine": (0, 0.03 + 0.015 * s, 0),
        "head": (0, -0.03, 0),
        "arm_l": (0.10, -0.05 - 0.02 * s, 0),
        "elbow_l": (0, -0.20, 0),
        "arm_r": (-0.10, -0.05 - 0.02 * s, 0),
        "elbow_r": (0, -0.20, 0),
        "leg_l": (0.0, 0, 0),
        "knee_l": (0, 0.05, 0),
        "leg_r": (0.0, 0, 0),
        "knee_r": (0, 0.05, 0),
    }
    r.update(joints)
    return r


def stroll(s, c, swing=0.5, arms=True, **joints):
    """A townsperson's walk: legs swinging, knees lifting on the forward
    swing, arms (if arms) swinging against the legs."""
    r = stand(0.0)
    r.update({
        "spine": (0, 0.06, 0.05 * s),
        "leg_l": (0.04, -swing * s, 0),
        "leg_r": (-0.04, swing * s, 0),
        "knee_l": (0, 0.12 + 0.6 * max(0.0, s), 0),
        "knee_r": (0, 0.12 + 0.6 * max(0.0, -s), 0),
    })
    if arms:
        r["arm_l"] = (0.10, swing * 0.6 * s, 0)
        r["arm_r"] = (-0.10, -swing * 0.6 * s, 0)
        r["elbow_l"] = (0, -0.25 - 0.15 * max(0.0, -s), 0)
        r["elbow_r"] = (0, -0.25 - 0.15 * max(0.0, s), 0)
    r.update(joints)
    return r


def build_carls():
    """Carls (people kind 4): a young swordsman with a full mop of blond
    hair, a navy doublet with a white collar, a short royal-blue cape over
    his shoulders worked with gold, dark breeches, tall brown boots and a
    sword at his left hip. He waits with his hands on his hips."""
    import mpfb_body
    skin = studio.material("skin", (0.88, 0.70, 0.56), (0.70, 0.52, 0.40), scale=30, bump=0.05)
    hair = studio.material("blond hair", (0.96, 0.78, 0.30), (0.60, 0.40, 0.10), scale=50, bump=0.6, stretch=(1, 1, 4))
    doublet = studio.material("doublet", (0.14, 0.15, 0.36), (0.05, 0.05, 0.14), scale=22, bump=0.3)
    breeches = studio.material("breeches", (0.10, 0.10, 0.16), (0.03, 0.03, 0.06), scale=18, bump=0.3)
    boots = studio.material("boots", (0.46, 0.28, 0.13), (0.18, 0.10, 0.04), scale=25, rough=0.55)
    cape = studio.material("blue cape", (0.24, 0.30, 0.70), (0.10, 0.12, 0.34), scale=7, bump=0.35,
                           spot=0.40, spot_colour=(0.86, 0.66, 0.22))
    lining = studio.material("collar", (0.92, 0.92, 0.94), (0.62, 0.64, 0.70), scale=30)
    gold = studio.material("gold", (0.90, 0.70, 0.26), (0.46, 0.30, 0.08), scale=30, rough=0.35)
    steel = studio.material("hilt", (0.80, 0.80, 0.84), (0.34, 0.34, 0.38), scale=40, rough=0.3)
    height = 3.85
    arms = ("upperarm", "lowerarm", "hand", "thumb", "index", "middle", "ring", "pinky")

    def pick(centre, bones, normal):
        z = centre.z / height
        part = mpfb_body.part(bones)
        if part in ("hand", "head", "neck"):
            return skin
        if z < 0.30:
            return boots
        if z < 0.555 or (part == "thigh"):
            return breeches
        return doublet

    rig = townsperson(height, dict(gender=1.0, age=0.4, muscle=0.6, weight=0.45, height=0.7, proportions=0.8,
                                   caucasian=1.0, african=0.0, asian=0.0), pick, 1.22 * SCALE_MH)
    body = rig.body

    def gear(centre, part, normal):
        z = centre.z / height
        front = normal.y < -0.45  # the body's front is -Y
        if part == "head" and (z > 0.925 or (z > 0.86 and normal.y > -0.15)
                               or (z > 0.875 and abs(normal.x) > 0.55)):
            return hair, 0.07  # a thick, rounded mop, over the ears
        if part == "neck" and normal.y > -0.2:
            return hair, 0.05
        if part in ("torso", "neck") and z > 0.80 and front:
            return lining, 0.03  # the white collar
        if part == "torso" and z > 0.62 and not front:
            return cape, 0.05
        if part == "thigh" and z > 0.44:
            return breeches, 0.04  # full breeches
        if part in ("shin", "foot") and z < 0.30:
            return boots, 0.03
        return None

    mpfb_body.shell(body, gear, "clothes")
    # The short cape: round his shoulders and down over his upper arms,
    # open at the front over the collar, with a gold-edged hem.
    shoulder = rig.rest["arm_l"].z
    rig.attach(shape("flare", (0.44, 0.62, 0.27), cape), "spine", (-0.05, 0, shoulder - 0.13))
    for dy in (-0.10, 0.10):  # the white lining turned back down the front edges
        rig.attach(shape("box", (0.035, 0.06, 0.26), lining, rotation=(math.radians(-60 * dy), 0, 0)), "spine",
                   (0.34, dy, shoulder - 0.14))
    # A gold belt and the sword at his left hip, hanging back.
    rig.attach(shape("cylinder", (0.22, 0.27, 0.035), gold), "hips", (0.0, 0, 0.18))
    scabbard = studio.material("scabbard", (0.30, 0.16, 0.08), (0.12, 0.06, 0.03), scale=25, rough=0.5)
    rig.attach(shape("cylinder", (0.035, 0.035, 0.42), scabbard, rotation=(0, math.radians(-35), 0)), "hips",
               (-0.10, 0.27, -0.22))
    rig.attach(shape("box", (0.03, 0.11, 0.022), steel), "hips", (0.08, 0.27, 0.10))
    rig.attach(shape("cylinder", (0.022, 0.022, 0.09), scabbard, rotation=(0, math.radians(-35), 0)), "hips",
               (0.11, 0.27, 0.16))
    rig.kind = "carls"
    rig.poses = {
        "idle": lambda s, c, k: stand(s, arm_l=(0.70, -0.02 * s, 0), elbow_l=(-1.4, 0, 0),
                                      arm_r=(-0.70, -0.02 * s, 0), elbow_r=(1.4, 0, 0),
                                      leg_l=(0.0, 0, 0), leg_r=(0.0, 0, 0)),
        "walk": lambda s, c, k: stroll(s, c, swing=0.55),
    }
    return rig


def build_jill():
    """Jill (people kind 0): a pedlar woman pushing a wooden handcart, red-
    brown hair in a knot on top, a green jacket over a long green tartan
    skirt. The cart goes before her: a plank box on two big spoked wheels,
    a small wheel at the front, a bundle of cloth in it and a cross bar at
    her hands. Its wheels turn as she walks."""
    import mpfb_body
    skin = studio.material("skin", (0.92, 0.74, 0.60), (0.74, 0.54, 0.42), scale=30, bump=0.05)
    hair = studio.material("red hair", (0.62, 0.30, 0.14), (0.30, 0.12, 0.05), scale=50, bump=0.5, stretch=(1, 1, 4))
    jacket = studio.material("green jacket", (0.18, 0.42, 0.26), (0.06, 0.18, 0.10), scale=16, bump=0.35)
    skirt = plaid("tartan", (0.24, 0.50, 0.30), (0.07, 0.20, 0.12), scale=3.0)
    shoes = studio.material("shoes", (0.82, 0.70, 0.56), (0.50, 0.40, 0.30), scale=25, rough=0.6)
    height = 3.15

    def pick(centre, bones, normal):
        z = centre.z / height
        part = mpfb_body.part(bones)
        if part in ("hand", "head", "neck"):
            return skin
        if part == "foot":
            return shoes
        if z < 0.56:
            return skirt
        return jacket

    rig = townsperson(height, dict(gender=0.0, age=0.5, muscle=0.5, weight=0.55, height=0.4, proportions=0.6,
                                   caucasian=1.0, african=0.0, asian=0.0), pick, 1.22 * SCALE_MH * 0.88)
    body = rig.body

    def gear(centre, part, normal):
        z = centre.z / height
        if part == "head" and (z > 0.93 or (z > 0.87 and normal.y > 0.2)):
            return hair, 0.035  # pulled back off the face
        if part == "torso" and 0.50 < z < 0.84:
            return jacket, 0.05 if z > 0.56 else 0.08  # the jacket's skirts over the hips
        if part == "upperarm" or (part == "forearm" and z > 0.42):
            return jacket, 0.04
        return None

    mpfb_body.shell(body, gear, "clothes")
    # Her hair in a knot on top of her head.
    top = (height - rig.joints["head"].matrix_world.translation.z) / (1.22 * SCALE_MH * 0.88)
    rig.attach(shape("ball", (0.10, 0.10, 0.09), hair), "head", (-0.04, 0, top - 0.02))
    # The long skirt, hips to ankles, over the legs.
    hip = rig.joints["hips"].matrix_world.translation.z / (1.22 * SCALE_MH * 0.88)
    length = hip + 0.02
    rig.attach(shape("flare", (0.42, 0.40, length / 2), skirt), "hips", (0.02, 0, 0.04 - length / 2))
    # The handcart, on its own joint so the wheels can turn.
    wood = studio.material("cartwood", (0.66, 0.40, 0.18), (0.32, 0.16, 0.06), scale=10, rough=0.7, bump=0.5,
                           stretch=(3, 1, 1))
    dark = studio.material("cart iron", (0.24, 0.22, 0.20), (0.08, 0.07, 0.06), scale=30, rough=0.4)
    cloth = studio.material("cart cloth", (0.66, 0.66, 0.54), (0.38, 0.38, 0.30), scale=14, bump=0.4)
    rig.joint("cart", "root", (0, 0, 0))
    x0, x1 = 0.72, 1.92  # the bed, from the handles to the front
    zb = 0.70
    rig.attach(shape("box", ((x1 - x0) / 2, 0.42, 0.035), wood), "cart", ((x0 + x1) / 2, 0, zb))
    for dy in (-0.42, 0.42):  # plank sides
        for k, dz in enumerate((0.10, 0.27)):
            rig.attach(shape("box", ((x1 - x0) / 2, 0.025, 0.07), wood), "cart", ((x0 + x1) / 2, dy, zb + dz))
    for dx in (x0, x1):
        rig.attach(shape("box", (0.025, 0.42, 0.17), wood), "cart", (dx, 0, zb + 0.17))
    for dx in (x0 + 0.02, x1 - 0.02):
        for dy in (-0.42, 0.42):
            rig.attach(shape("box", (0.03, 0.03, 0.22), dark), "cart", (dx, dy, zb + 0.18))
    # A cloth thrown over the front of the load, hanging down over the end.
    rig.attach(shape("box", (0.30, 0.40, 0.10), cloth), "cart", (x1 - 0.30, 0, zb + 0.25))
    rig.attach(shape("box", (0.03, 0.44, 0.18), cloth, rotation=(0, math.radians(-10), 0)), "cart",
               (x1 + 0.04, 0, zb + 0.22))
    rig.attach(shape("box", (0.24, 0.36, 0.12), wood), "cart", (x0 + 0.32, 0, zb + 0.15))
    # Two handles back to her hands, joined by a cross bar.
    for dy in (-0.32, 0.32):
        rig.attach(shape("box", (0.32, 0.028, 0.028), wood, rotation=(0, math.radians(-14), 0)), "cart",
                   (x0 - 0.28, dy, zb + 0.24))
    rig.attach(shape("cylinder", (0.03, 0.03, 0.40), wood, rotation=(math.radians(90), 0, 0)), "cart",
               (x0 - 0.56, 0, zb + 0.31))
    # Big spoked wheels under the bed, a little behind its middle.
    rad = 0.56
    wx = x0 + 0.45
    for side, dy in (("l", 0.52), ("r", -0.52)):
        rig.joint(f"wheel_{side}", "cart", (wx, dy, rad))
        rig.attach(shape("cylinder", (rad, rad, 0.035), dark, rotation=(math.radians(90), 0, 0)), f"wheel_{side}")
        rig.attach(shape("cylinder", (rad * 0.88, rad * 0.88, 0.05), wood, rotation=(math.radians(90), 0, 0)),
                   f"wheel_{side}")
        for k in range(6):
            a = math.pi * k / 3
            rig.attach(shape("box", (rad * 0.82, 0.03, 0.03), wood, rotation=(0, a, 0)), f"wheel_{side}")
        rig.attach(shape("cylinder", (0.09, 0.09, 0.07), dark, rotation=(math.radians(90), 0, 0)), f"wheel_{side}")
    rig.attach(shape("cylinder", (0.035, 0.035, 0.52), dark, rotation=(math.radians(90), 0, 0)), "cart", (wx, 0, rad))
    # A small wheel under the front.
    rig.joint("wheel_f", "cart", (x1 - 0.12, 0, 0.20))
    rig.attach(shape("cylinder", (0.20, 0.20, 0.06), wood, rotation=(math.radians(90), 0, 0)), "wheel_f")
    rig.attach(shape("cylinder", (0.06, 0.06, 0.09), dark, rotation=(math.radians(90), 0, 0)), "wheel_f")
    rig.attach(shape("box", (0.04, 0.04, 0.20), wood), "cart", (x1 - 0.12, 0, 0.42))
    rig.kind = "jill"
    hands = dict(arm_l=(-0.05, -0.75, 0), elbow_l=(-0.2, -0.55, 0), arm_r=(0.05, -0.75, 0), elbow_r=(0.2, -0.55, 0))

    def walk(s, c, k):
        turn = -2 * math.pi / 6 * k  # one spoke's turn per stride, so the loop is seamless
        r = stroll(s, c, swing=0.45, arms=False, spine=(0, 0.32, 0.03 * s), head=(0, -0.24, 0), **hands)
        r.update(wheel_l=(0, turn, 0), wheel_r=(0, turn, 0), wheel_f=(0, turn * 2.6, 0))
        return r

    rig.poses = {
        "idle": lambda s, c, k: stand(s, spine=(0, 0.30 + 0.015 * s, 0), head=(0, -0.22, 0), **hands),
        "walk": walk,
    }
    return rig


def build_izaram():
    """Wizard Izaram (people kind 12): a tall figure in a long dark olive
    robe with a gold panel down the front, a shoulder cape edged in red and
    a tall pointed hood with a red tip that hides his face; his pale hands
    hang slack beside his hips."""
    import mpfb_body
    skin = studio.material("pale skin", (0.92, 0.88, 0.84), (0.66, 0.62, 0.60), scale=30, bump=0.05)
    shadow = studio.material("hood shadow", (0.10, 0.08, 0.05), (0.03, 0.02, 0.01), scale=30)
    robe = studio.material("dark robe", (0.10, 0.09, 0.03), (0.02, 0.02, 0.0), scale=12, bump=0.4,
                           stretch=(1, 1, 3))
    cape = studio.material("olive cape", (0.44, 0.36, 0.04), (0.08, 0.06, 0.01), scale=10, bump=0.5,
                           spot=0.18, spot_colour=(0.70, 0.52, 0.10))
    gold = studio.material("gold panel", (0.86, 0.60, 0.12), (0.46, 0.28, 0.03), scale=14, bump=0.3,
                           stretch=(1, 1, 3))
    red = studio.material("red trim", (0.80, 0.12, 0.08), (0.40, 0.04, 0.02), scale=30, rough=0.5)
    height = 3.9
    scale = 1.22 * SCALE_MH

    def pick(centre, bones, normal):
        part = mpfb_body.part(bones)
        if part == "hand":
            return skin
        if part in ("head", "neck"):
            return shadow
        return robe

    rig = townsperson(height, dict(gender=1.0, age=0.8, muscle=0.3, weight=0.35, height=0.8, proportions=0.6,
                                   caucasian=1.0, african=0.0, asian=0.0), pick, scale)
    body = rig.body

    def gear(centre, part, normal):
        z = centre.z / height
        if part in ("torso", "upperarm") and z > 0.66:
            return cape, 0.08 if z > 0.74 else 0.10
        if part == "forearm" or (part == "upperarm"):
            return robe, 0.06  # wide sleeves
        if part == "torso":
            return robe, 0.05
        return None

    mpfb_body.shell(body, gear, "robe")
    J = rig.joints
    hip = J["hips"].matrix_world.translation.z / scale
    shoulder = rig.rest["arm_l"].z
    # The long robe from the hips to the ground, and a gold panel down its
    # front, narrow at the waist and wide at the hem.
    length, low, high = hip + 0.04, 0.38, 0.38 * 0.6
    rig.attach(shape("flare", (low, 0.40, length / 2), robe), "hips", (0, 0, 0.02 - length / 2))
    tilt = math.atan((low - high) / length)
    part = length * 0.55  # up from the hem to the knees
    panel = shape("flare", (0.02, 0.20, part / 2), gold, rotation=(0, -tilt, 0))
    rig.attach(panel, "hips", (low - (low - high) * 0.55 / 2 + 0.015, 0, 0.02 - length + part / 2))
    # The cape: round the shoulders down to the elbows, red dots on its hem.
    rig.attach(shape("flare", (0.42, 0.58, 0.26), cape), "spine", (-0.03, 0, shoulder - 0.10))
    for k in range(14):
        a = 2 * math.pi * (k + 0.5) / 14
        rig.attach(shape("ball", (0.035, 0.035, 0.035), red), "spine",
                   (-0.03 + 0.43 * math.cos(a), 0.59 * math.sin(a), shoulder - 0.35))
    # The hood: round over the head, rising to a tall point with a red tip.
    top = (height - J["head"].matrix_world.translation.z) / scale
    rig.attach(shape("ball", (0.24, 0.23, 0.26), cape), "head", (-0.03, 0, top * 0.60))
    rig.attach(shape("cone", (0.21, 0.21, 0.34), cape, rotation=(0, math.radians(-10), 0)), "head",
               (-0.08, 0, top * 0.60 + 0.36))
    rig.attach(shape("ball", (0.06, 0.06, 0.09), red), "head", (-0.12, 0, top * 0.60 + 0.56))
    rig.kind = "izaram"
    rig.poses = {
        "idle": lambda s, c, k: stand(s, arm_l=(0.10, -0.01 * s, 0), elbow_l=(0, -0.08, 0),
                                      arm_r=(-0.10, -0.01 * s, 0), elbow_r=(0, -0.08, 0)),
        "walk": lambda s, c, k: stroll(s, c, swing=0.35, spine=(0, 0.04, 0.03 * s)),
    }
    return rig


BUILDERS.update({"person4": build_carls, "person0": build_jill, "person12": build_izaram})
HEIGHT.update({"person4": 3.6, "person0": 3.0, "person12": 3.8})
SIZE.update({"person0": 200})


# The third area's people, on MakeHuman bodies like Cold Svalt's: kind 3
# the gate guards, 31-33 the churchmen, 34 and 35 the dwarves, 36 and 37
# the two knights and 38 the shrine keeper. Heights on screen follow the
# original's (about 16 pixels a unit).
CAUCASIAN = dict(caucasian=1.0, african=0.0, asian=0.0)
# A dwarf: short legs and arms, a broad body and a big head (MPFB targets;
# the body is then scaled to its height, so the head comes out larger).
DWARF = ([(f"legs/{s}-{p}-scale-vert-decr.target.gz", 1.0) for s in "lr" for p in ("upperleg", "lowerleg")]
         + [(f"arms/{s}-{p}-scale-vert-decr.target.gz", 0.5) for s in "lr" for p in ("upperarm", "lowerarm")]
         + [(f"head/head-scale-{a}-incr.target.gz", 0.7) for a in ("horiz", "vert", "depth")]
         + [("torso/torso-scale-horiz-incr.target.gz", 1.0), ("torso/torso-scale-depth-incr.target.gz", 0.7)])


def sharp(kind, size, mat, rotation=(0, 0, 0)):
    """shape() without the rounding at render time, which would shrink a
    long cone or flare (a cope, a hat) into an egg."""
    obj = shape(kind, size, mat, rotation)
    obj.modifiers["soft"].render_levels = 0
    return obj


def npc(height, macros, pick, targets=(), scale=None, arm_out=0.14):
    """townsperson() with the root scale following the height (Carls's at
    3.85 units) and a few heights in rig units noted on the rig: hip and
    neck above the ground, shoulder above the spine joint and top of the
    head above the head joint."""
    scale = scale or 1.22 * SCALE_MH * height / 3.85
    rig = townsperson(height, dict(CAUCASIAN, **macros), pick, scale, targets, arm_out)
    J = rig.joints
    rig.scale = scale
    rig.hip = J["hips"].matrix_world.translation.z / scale
    rig.shoulder = rig.rest["arm_l"].z
    rig.top = (height - J["head"].matrix_world.translation.z) / scale
    return rig


def hem(rig, joint, mat, rx, ry, z, count=16, size=0.035, x=0.0, back_only=False):
    """A ring of beads or studs round a hem or an edge."""
    for k in range(count):
        a = 2 * math.pi * (k + 0.5) / count
        if back_only and math.cos(a) > 0.2:
            continue
        rig.attach(shape("ball", (size, size, size), mat), joint, (x + rx * math.cos(a), ry * math.sin(a), z))


def build_gate_guard():
    """A gate guard (people kind 3): a mail hood over his head with the
    face open, a dark grey quilted tunic and a short mail cape edged in
    pale cloth, a grey skirt to the knees over dark hose, a brown belt and
    brown boots. He stands with his hands near his belt."""
    import mpfb_body
    skin = studio.material("skin", (0.84, 0.64, 0.50), (0.66, 0.46, 0.34), scale=30, bump=0.05)
    mail = studio.material("mail", (0.36, 0.36, 0.38), (0.12, 0.12, 0.14), scale=70, rough=0.4, bump=0.9)
    tunic = plaid("quilted tunic", (0.30, 0.30, 0.32), (0.18, 0.18, 0.20), scale=7.0)
    hose = studio.material("hose", (0.20, 0.20, 0.22), (0.08, 0.08, 0.09), scale=18)
    boots = studio.material("boots", (0.48, 0.32, 0.16), (0.20, 0.12, 0.05), scale=25, rough=0.55)
    pale = studio.material("pale trim", (0.82, 0.82, 0.74), (0.52, 0.52, 0.46), scale=30)
    belt = studio.material("belt", (0.36, 0.22, 0.10), (0.14, 0.08, 0.03), scale=30, rough=0.5)
    height = 3.75

    def pick(centre, bones, normal):
        z = centre.z / height
        part = mpfb_body.part(bones)
        if part in ("hand", "head", "neck"):
            return skin
        if z < 0.28:
            return boots
        if z < 0.55:
            return hose
        return tunic

    rig = npc(height, dict(gender=1.0, age=0.45, muscle=0.6, weight=0.5, height=0.6, proportions=0.6), pick)
    body = rig.body

    def gear(centre, part, normal):
        z = centre.z / height
        face = normal.y < -0.45 and 0.855 < z < 0.95 and abs(centre.x) < 0.16
        if part in ("head", "neck") and not face:
            return mail, 0.05
        if part == "torso" and z > 0.52:
            return tunic, 0.05
        if part in ("upperarm", "forearm"):
            return tunic, 0.035
        if part in ("shin", "foot") and z < 0.28:
            return boots, 0.03
        return None

    mpfb_body.shell(body, gear, "clothes")
    # The skirt from the waist to the knees, the belt over it, the short
    # mail cape over the shoulders and its pale edge.
    length = rig.hip * 0.62
    rig.attach(sharp("flare", (0.34, 0.36, length / 2), tunic), "hips", (0.0, 0, 0.14 - length / 2))
    rig.attach(shape("cylinder", (0.25, 0.29, 0.04), belt), "hips", (0.0, 0, 0.17))
    rig.attach(sharp("flare", (0.34, 0.46, 0.16), mail), "spine", (-0.03, 0, rig.shoulder - 0.04))
    hem(rig, "spine", pale, 0.34, 0.46, rig.shoulder - 0.19, count=18, size=0.035, x=-0.03)
    hem(rig, "hips", pale, 0.34, 0.36, 0.14 - length, count=18, size=0.03)
    for dy in (-0.12, 0.12):  # pale straps down the chest
        rig.attach(shape("box", (0.03, 0.035, 0.22), pale, rotation=(math.radians(-60 * dy), 0, 0)), "spine",
                   (0.27, dy, rig.shoulder - 0.30))
    rig.kind = "gate guard"
    rig.poses = {
        "idle": lambda s, c, k: stand(s, arm_l=(0.40, -0.02 * s, 0), elbow_l=(-0.9, -0.2, 0),
                                      arm_r=(-0.40, -0.02 * s, 0), elbow_r=(0.9, -0.2, 0)),
        "walk": lambda s, c, k: stroll(s, c, swing=0.5),
    }
    return rig


def build_blue_bishop():
    """The bishop in blue (people kind 31): a stout old man in a wide royal
    blue cope that falls from his shoulders to his shins, edged in gold
    with a gold band across the chest, a blue skull cap, blue hose and gold
    slippers."""
    import mpfb_body
    skin = studio.material("pale skin", (0.94, 0.80, 0.70), (0.74, 0.58, 0.50), scale=30, bump=0.05)
    blue = studio.material("blue cope", (0.14, 0.24, 0.62), (0.04, 0.08, 0.26), scale=9, bump=0.45,
                           spot=0.12, spot_colour=(0.28, 0.40, 0.80))
    hose = studio.material("blue hose", (0.20, 0.34, 0.66), (0.06, 0.12, 0.30), scale=16)
    gold = studio.material("gold trim", (0.92, 0.70, 0.24), (0.50, 0.32, 0.08), scale=30, rough=0.4)
    height = 3.95

    def pick(centre, bones, normal):
        z = centre.z / height
        part = mpfb_body.part(bones)
        if part in ("hand", "head", "neck"):
            return skin
        if part == "foot":
            return gold
        if z < 0.55:
            return hose
        return blue

    rig = npc(height, dict(gender=1.0, age=0.75, muscle=0.3, weight=0.85, height=0.5, proportions=0.5), pick)
    body = rig.body

    def gear(centre, part, normal):
        z = centre.z / height
        if part == "head" and z > 0.935:
            return blue, 0.03  # the skull cap
        if part in ("torso", "upperarm", "forearm"):
            return blue, 0.06
        if part == "thigh":
            return hose, 0.05
        return None

    mpfb_body.shell(body, gear, "clothes")
    # The cope: from the shoulders to the shins, wide over the arms, with a
    # gold hem and a gold band across the chest; a gold rim on the cap.
    length = rig.shoulder + rig.hip - 0.55
    rig.attach(sharp("flare", (0.56, 0.68, length / 2), blue), "spine", (-0.02, 0, rig.shoulder + 0.06 - length / 2))
    rig.attach(shape("ball", (0.36, 0.44, 0.16), blue), "spine", (-0.02, 0, rig.shoulder + 0.02))
    hem(rig, "spine", gold, 0.56, 0.68, rig.shoulder + 0.06 - length, count=28, size=0.04, x=-0.02)
    rig.attach(shape("box", (0.03, 0.05, 0.40), gold, rotation=(math.radians(40), 0, 0)), "spine",
               (0.37, 0.0, rig.shoulder - 0.25))
    for dy in (-0.16, 0.16):  # the cope's gold edges down the front
        rig.attach(shape("box", (0.03, 0.04, length * 0.42), gold), "spine",
                   (0.50, dy, rig.shoulder + 0.06 - length * 0.55))
    rig.attach(shape("cylinder", (0.19, 0.18, 0.025), gold), "head", (0.0, 0, rig.top * 0.62))
    rig.kind = "blue bishop"
    rig.poses = {
        "idle": lambda s, c, k: stand(s, arm_l=(0.30, -0.02 * s, 0), elbow_l=(-0.3, -0.6, 0),
                                      arm_r=(-0.30, -0.02 * s, 0), elbow_r=(0.3, -0.6, 0)),
        "walk": lambda s, c, k: stroll(s, c, swing=0.32, arms=False, arm_l=(0.30, 0, 0), arm_r=(-0.30, 0, 0)),
    }
    return rig


def build_red_priest():
    """The priest in red and gold (people kind 32): a broad red cape over
    his shoulders worked in gold with a deep gold scalloped border, a long
    dark red robe, a grey underrobe at the hem, a white beard and a tall
    red pointed hat with a gold brim."""
    import mpfb_body
    skin = studio.material("skin", (0.88, 0.70, 0.58), (0.68, 0.50, 0.40), scale=30, bump=0.05)
    red = studio.material("red cape", (0.66, 0.12, 0.08), (0.30, 0.04, 0.02), scale=10, bump=0.45,
                          spot=0.16, spot_colour=(0.86, 0.60, 0.20))
    robe = studio.material("dark red robe", (0.44, 0.10, 0.06), (0.18, 0.03, 0.02), scale=12, bump=0.4,
                           stretch=(1, 1, 3))
    under = studio.material("grey underrobe", (0.62, 0.60, 0.58), (0.34, 0.32, 0.30), scale=14, stretch=(1, 1, 3))
    gold = studio.material("gold border", (0.94, 0.72, 0.22), (0.52, 0.32, 0.06), scale=26, rough=0.4, bump=0.4)
    beard = studio.material("white beard", (0.92, 0.90, 0.86), (0.62, 0.60, 0.58), scale=50, bump=0.6,
                            stretch=(1, 1, 4))
    height = 3.55

    def pick(centre, bones, normal):
        part = mpfb_body.part(bones)
        if part in ("hand", "head", "neck"):
            return skin
        return robe

    rig = npc(height, dict(gender=1.0, age=0.85, muscle=0.3, weight=0.7, height=0.5, proportions=0.5), pick)
    body = rig.body

    def gear(centre, part, normal):
        z = centre.z / height
        if part == "head" and (z > 0.93 or (z > 0.86 and normal.y > 0.1)):
            return beard, 0.03  # white hair under the hat
        if part in ("torso", "upperarm", "forearm"):
            return robe, 0.06
        return None

    mpfb_body.shell(body, gear, "clothes")
    # Long robe to the ground with the grey underrobe showing at the hem.
    length = rig.hip + 0.04
    rig.attach(sharp("flare", (0.40, 0.44, length * 0.18), under), "hips", (0, 0, 0.02 - length * 0.82))
    rig.attach(sharp("flare", (0.36, 0.40, length * 0.36), robe), "hips", (0, 0, 0.04 - length * 0.36))
    # The cape: wide over the shoulders and down to the hips, the gold border
    # in points along its hem.
    cape = rig.shoulder + 0.30
    rig.attach(sharp("flare", (0.58, 0.72, cape / 2), red), "spine", (-0.04, 0, rig.shoulder + 0.08 - cape / 2))
    rig.attach(shape("ball", (0.38, 0.46, 0.16), red), "spine", (-0.03, 0, rig.shoulder + 0.04))
    bottom = rig.shoulder + 0.08 - cape
    for k in range(18):
        a = 2 * math.pi * (k + 0.5) / 18
        rig.attach(sharp("cone", (0.09, 0.05, 0.10), gold, rotation=(0, math.pi, a)), "spine",
                   (-0.04 + 0.57 * math.cos(a), 0.71 * math.sin(a), bottom - 0.05))
    hem(rig, "spine", gold, 0.58, 0.72, bottom + 0.02, count=26, size=0.045, x=-0.04)
    hem(rig, "spine", gold, 0.38, 0.46, rig.shoulder + 0.08, count=18, size=0.04, x=-0.03)
    # A full white beard, and the hat.
    rig.attach(sharp("cone", (0.13, 0.15, 0.17), beard, rotation=(0, math.pi, 0)), "head", (0.15, 0, rig.top * 0.18))
    rig.attach(shape("cylinder", (0.27, 0.27, 0.03), gold), "head", (-0.02, 0, rig.top * 0.72))
    rig.attach(sharp("cone", (0.19, 0.19, 0.30), red, rotation=(0, math.radians(-6), 0)), "head",
               (-0.04, 0, rig.top * 0.72 + 0.28))
    rig.attach(shape("ball", (0.045, 0.045, 0.045), gold), "head", (-0.08, 0, rig.top * 0.72 + 0.58))
    rig.kind = "red priest"
    rig.poses = {
        "idle": lambda s, c, k: stand(s, arm_l=(0.25, -0.02 * s, 0), elbow_l=(-0.2, -0.9, 0),
                                      arm_r=(-0.25, -0.02 * s, 0), elbow_r=(0.2, -0.9, 0)),
        "walk": lambda s, c, k: stroll(s, c, swing=0.30, arms=False, arm_l=(0.25, 0, 0), arm_r=(-0.25, 0, 0),
                                       elbow_l=(-0.2, -0.9, 0), elbow_r=(0.2, -0.9, 0)),
    }
    return rig


def build_padded_priest():
    """The priest in a padded robe (people kind 33): a heavy man in a
    long quilted robe of undyed cloth to his shins, deep red sleeves and
    a red cap, sandals on bare feet."""
    import mpfb_body
    skin = studio.material("skin", (0.86, 0.66, 0.52), (0.66, 0.46, 0.36), scale=30, bump=0.05)
    padded = plaid("quilted robe", (0.80, 0.66, 0.54), (0.58, 0.44, 0.34), scale=6.0)
    red = studio.material("red sleeves", (0.56, 0.12, 0.10), (0.24, 0.04, 0.03), scale=16, bump=0.35)
    sandal = studio.material("sandals", (0.40, 0.26, 0.14), (0.16, 0.10, 0.05), scale=25, rough=0.6)
    height = 3.72

    def pick(centre, bones, normal):
        z = centre.z / height
        part = mpfb_body.part(bones)
        if part in ("hand", "head", "neck"):
            return skin
        if part == "foot":
            return sandal
        if part in ("upperarm", "forearm"):
            return red
        if z < 0.2:
            return skin
        return padded

    rig = npc(height, dict(gender=1.0, age=0.6, muscle=0.4, weight=0.95, height=0.5, proportions=0.4), pick)
    body = rig.body

    def gear(centre, part, normal):
        z = centre.z / height
        if part == "head" and z > 0.935:
            return red, 0.035  # the cap
        if part == "torso":
            return padded, 0.08
        if part == "upperarm":
            return padded, 0.07  # short padded sleeves over the red ones
        if part == "forearm":
            return red, 0.04
        if part == "thigh":
            return padded, 0.07
        return None

    mpfb_body.shell(body, gear, "clothes")
    length = rig.hip * 0.80
    rig.attach(sharp("flare", (0.42, 0.44, length / 2), padded), "hips", (0.02, 0, 0.10 - length / 2))
    rig.attach(shape("ball", (0.16, 0.15, 0.10), red), "head", (-0.03, 0, rig.top * 0.80))
    rig.kind = "padded priest"
    rig.poses = {
        "idle": lambda s, c, k: stand(s, arm_l=(0.22, -0.02 * s, 0), elbow_l=(0, -0.25, 0),
                                      arm_r=(-0.22, -0.02 * s, 0), elbow_r=(0, -0.25, 0)),
        "walk": lambda s, c, k: stroll(s, c, swing=0.38, arm_l=(0.22, 0, 0)),
    }
    return rig


def build_green_dwarf():
    """The dwarf in green (people kind 34): short and broad, a grey-green
    tunic and breeches, a pointed cap that flops back, a long sandy beard,
    a brown belt and red-brown boots, standing wide with his hands on his
    hips."""
    import mpfb_body
    skin = studio.material("skin", (0.86, 0.64, 0.50), (0.66, 0.44, 0.32), scale=30, bump=0.05)
    green = studio.material("grey green", (0.40, 0.50, 0.44), (0.16, 0.22, 0.18), scale=14, bump=0.4)
    breeches = studio.material("dark green", (0.28, 0.36, 0.32), (0.10, 0.14, 0.12), scale=16)
    boots = studio.material("red boots", (0.50, 0.18, 0.12), (0.22, 0.06, 0.04), scale=25, rough=0.55)
    beard = studio.material("ginger beard", (0.82, 0.52, 0.28), (0.46, 0.26, 0.12), scale=50, bump=0.6,
                            stretch=(1, 1, 4))
    belt = studio.material("belt", (0.40, 0.26, 0.12), (0.16, 0.10, 0.04), scale=30, rough=0.5)
    height = 2.95

    def pick(centre, bones, normal):
        z = centre.z / height
        part = mpfb_body.part(bones)
        if part in ("hand", "head", "neck"):
            return skin
        if z < 0.22:
            return boots
        if z < 0.46:
            return breeches
        return green

    rig = npc(height, dict(gender=1.0, age=0.7, muscle=0.8, weight=0.9, height=0.2, proportions=0.3), pick,
              targets=DWARF)
    body = rig.body

    def gear(centre, part, normal):
        z = centre.z / height
        if part == "head" and (z > 0.90 or (z > 0.80 and normal.y > 0.2)):
            return green, 0.03  # the cap's band and hair under it
        if part == "torso" and z > 0.42:
            return green, 0.06
        if part in ("upperarm", "forearm"):
            return green, 0.04
        if part == "thigh":
            return breeches, 0.05
        if part in ("shin", "foot") and z < 0.22:
            return boots, 0.04
        return None

    mpfb_body.shell(body, gear, "clothes")
    length = rig.hip * 0.55
    rig.attach(sharp("flare", (0.36, 0.40, length / 2), green), "hips", (0.02, 0, 0.14 - length / 2))
    rig.attach(shape("cylinder", (0.30, 0.34, 0.045), belt), "hips", (0.02, 0, 0.16))
    # A long beard and the pointed cap, its tip bent back.
    rig.attach(sharp("cone", (0.20, 0.22, 0.30), beard, rotation=(0, math.pi + 0.35, 0)), "head",
               (0.24, 0, rig.top * 0.02))
    rig.attach(sharp("cone", (0.25, 0.24, 0.22), green, rotation=(0, math.radians(-35), 0)), "head",
               (-0.06, 0, rig.top * 0.80 + 0.12))
    rig.kind = "green dwarf"
    hips = dict(arm_l=(0.75, -0.02, 0), elbow_l=(-1.4, 0, 0), arm_r=(-0.75, -0.02, 0), elbow_r=(1.4, 0, 0))
    rig.poses = {
        "idle": lambda s, c, k: stand(s, leg_l=(0.30, 0, 0), leg_r=(-0.30, 0, 0), knee_l=(0, 0.15, 0),
                                      knee_r=(0, 0.15, 0), **hips),
        "walk": lambda s, c, k: stroll(s, c, swing=0.55, leg_l=(0.12, -0.55 * s, 0), leg_r=(-0.12, 0.55 * s, 0)),
    }
    return rig


def build_brown_dwarf():
    """The old dwarf in brown (people kind 35): very short and broad, bald
    with white hair round the back of his head and a great white beard, a
    pale fleece over his shoulders, a rust brown tunic, dark breeches and
    deep red boots."""
    import mpfb_body
    skin = studio.material("skin", (0.88, 0.68, 0.54), (0.68, 0.48, 0.36), scale=30, bump=0.05)
    rust = studio.material("rust tunic", (0.54, 0.24, 0.12), (0.24, 0.08, 0.03), scale=14, bump=0.4)
    breeches = studio.material("brown breeches", (0.30, 0.16, 0.10), (0.12, 0.06, 0.03), scale=16)
    boots = studio.material("deep red boots", (0.40, 0.10, 0.08), (0.16, 0.03, 0.02), scale=25, rough=0.55)
    fleece = studio.material("fleece", (0.86, 0.80, 0.66), (0.56, 0.50, 0.38), scale=24, bump=0.9)
    white = studio.material("white hair", (0.94, 0.92, 0.86), (0.64, 0.62, 0.58), scale=50, bump=0.6,
                            stretch=(1, 1, 4))
    belt = studio.material("belt", (0.24, 0.16, 0.10), (0.08, 0.05, 0.03), scale=30, rough=0.5)
    height = 2.65

    def pick(centre, bones, normal):
        z = centre.z / height
        part = mpfb_body.part(bones)
        if part in ("hand", "head", "neck"):
            return skin
        if z < 0.20:
            return boots
        if z < 0.42:
            return breeches
        return rust

    rig = npc(height, dict(gender=1.0, age=0.9, muscle=0.7, weight=1.0, height=0.0, proportions=0.2), pick,
              targets=DWARF + [("torso/torso-scale-horiz-incr.target.gz", 0.6)])
    body = rig.body

    def gear(centre, part, normal):
        z = centre.z / height
        if part == "head" and 0.80 < z < 0.92 and normal.y > 0.0:
            return white, 0.04
        if part == "torso" and z > 0.38:
            return rust, 0.07
        if part in ("upperarm", "forearm"):
            return rust, 0.05
        if part == "thigh":
            return breeches, 0.06
        if part in ("shin", "foot") and z < 0.20:
            return boots, 0.045
        return None

    mpfb_body.shell(body, gear, "clothes")
    length = rig.hip * 0.6
    rig.attach(sharp("flare", (0.40, 0.44, length / 2), rust), "hips", (0.02, 0, 0.14 - length / 2))
    rig.attach(shape("cylinder", (0.34, 0.38, 0.045), belt), "hips", (0.02, 0, 0.17))
    # The fleece round his shoulders, and the beard down to his belt.
    rig.attach(shape("ball", (0.44, 0.54, 0.28), fleece), "spine", (0.0, 0, rig.shoulder + 0.02))
    rig.attach(sharp("cone", (0.19, 0.25, 0.34), white, rotation=(0, math.pi + 0.35, 0)), "head",
               (0.25, 0, -rig.top * 0.30))
    rig.attach(shape("ball", (0.10, 0.20, 0.07), white), "head", (0.20, 0, rig.top * 0.35))  # moustache
    rig.kind = "brown dwarf"
    rig.poses = {
        "idle": lambda s, c, k: stand(s, leg_l=(0.16, 0, 0), leg_r=(-0.16, 0, 0),
                                      arm_l=(0.45, -0.1, 0), elbow_l=(-0.7, -0.5, 0),
                                      arm_r=(-0.45, -0.1, 0), elbow_r=(0.7, -0.5, 0)),
        "walk": lambda s, c, k: stroll(s, c, swing=0.55, leg_l=(0.12, -0.55 * s, 0), leg_r=(-0.12, 0.55 * s, 0),
                                       arm_l=(0.40, 0.3 * s, 0), arm_r=(-0.40, -0.3 * s, 0)),
    }
    return rig


def spiked_pauldron(rig, side, mat, spike, size=0.24):
    """A great rounded shoulder plate with a fan of spikes along its top."""
    s = 1 if side == "l" else -1
    rig.attach(shape("ball", (size, size * 1.05, size * 0.75), mat), f"arm_{side}", (0, s * 0.06, 0.04))
    for k in range(5):
        a = math.radians(-60 + 30 * k)
        rig.attach(sharp("cone", (0.05, 0.05, 0.20), spike, rotation=(s * math.radians(-55), 0, a * 0.6)),
                   f"arm_{side}", (size * 0.85 * math.sin(a), s * (size * 0.9), 0.14 + 0.03 * math.cos(a)))


def build_bronze_knight():
    """The knight in bronze (people kind 36): a big man in full bronze plate
    traced in dark lines, great spiked shoulder plates, a long deep red
    cloak hanging behind him to the ground and short red-brown hair."""
    import mpfb_body
    skin = studio.material("skin", (0.80, 0.58, 0.44), (0.60, 0.40, 0.30), scale=30, bump=0.05)
    bronze = plaid("bronze plate", (0.76, 0.58, 0.32), (0.20, 0.12, 0.06), scale=5.0)
    dark = studio.material("dark plate", (0.36, 0.26, 0.18), (0.14, 0.09, 0.05), scale=24, rough=0.4)
    spike = studio.material("pale spikes", (0.88, 0.80, 0.62), (0.54, 0.46, 0.32), scale=30, rough=0.35)
    cloak = studio.material("red cloak", (0.52, 0.06, 0.06), (0.22, 0.02, 0.02), scale=12, bump=0.4,
                            stretch=(1, 1, 3))
    hair = studio.material("red brown hair", (0.48, 0.20, 0.10), (0.20, 0.07, 0.03), scale=50, bump=0.6)
    height = 4.05

    def pick(centre, bones, normal):
        z = centre.z / height
        part = mpfb_body.part(bones)
        if part in ("head", "neck"):
            return skin
        if part in ("hand", "foot") or z < 0.30:
            return dark
        return bronze

    rig = npc(height, dict(gender=1.0, age=0.5, muscle=0.95, weight=0.6, height=0.8, proportions=0.8), pick)
    body = rig.body

    def gear(centre, part, normal):
        z = centre.z / height
        if part == "head" and (z > 0.93 or (z > 0.87 and normal.y > 0.1)):
            return hair, 0.04
        if part == "torso":
            return bronze, 0.07 if z > 0.55 else 0.06
        if part in ("upperarm", "forearm", "hand"):
            return bronze, 0.05
        if part in ("thigh", "shin"):
            return bronze if 0.24 < z < 0.33 or z > 0.36 else dark, 0.05
        if part == "foot":
            return dark, 0.04
        return None

    mpfb_body.shell(body, gear, "armour")
    # Plate skirt over the hips, the cloak from the shoulders to the ground
    # behind, and the pauldrons.
    rig.attach(sharp("flare", (0.34, 0.38, 0.20), bronze), "hips", (0.0, 0, -0.08))
    rig.attach(shape("cylinder", (0.27, 0.32, 0.04), dark), "hips", (0.0, 0, 0.14))
    length = rig.hip + rig.shoulder - 0.02
    rig.attach(sharp("box", (0.04, 0.46, length / 2), cloak, rotation=(0, math.radians(6), 0)), "spine",
               (-0.32, 0, rig.shoulder - length / 2))
    rig.attach(sharp("flare", (0.36, 0.48, 0.10), cloak), "spine", (-0.06, 0, rig.shoulder + 0.02))
    for side in ("l", "r"):
        spiked_pauldron(rig, side, bronze, spike)
    rig.kind = "bronze knight"
    rig.poses = {
        "idle": lambda s, c, k: stand(s, arm_l=(0.30, -0.02 * s, 0), elbow_l=(0, -0.15, 0),
                                      arm_r=(-0.30, -0.02 * s, 0), elbow_r=(0, -0.15, 0),
                                      leg_l=(0.08, 0, 0), leg_r=(-0.08, 0, 0)),
        "walk": lambda s, c, k: stroll(s, c, swing=0.5, arms=False, arm_l=(0.30, 0.3 * s, 0),
                                       arm_r=(-0.30, -0.3 * s, 0)),
    }
    return rig


def build_steel_knight():
    """The knight in lilac steel (people kind 37): full plate with a blue-
    violet sheen, broad layered shoulder plates, a gilded helm with a dark
    slit, and a long cloak of the same steel-blue cloth behind him."""
    import mpfb_body
    steel = plaid("lilac steel", (0.56, 0.56, 0.80), (0.20, 0.20, 0.36), scale=5.0)
    dark = studio.material("dark steel", (0.30, 0.30, 0.44), (0.12, 0.12, 0.20), scale=24, rough=0.35)
    gold = studio.material("gilded helm", (0.90, 0.74, 0.34), (0.48, 0.34, 0.10), scale=26, rough=0.3)
    slit = studio.material("visor slit", (0.08, 0.06, 0.06), (0.02, 0.02, 0.02), scale=30)
    cloak = studio.material("blue grey cloak", (0.50, 0.50, 0.72), (0.22, 0.22, 0.38), scale=12, bump=0.45,
                            stretch=(1, 1, 3))
    height = 3.85

    def pick(centre, bones, normal):
        z = centre.z / height
        part = mpfb_body.part(bones)
        if part in ("head", "neck"):
            return gold
        if part in ("hand", "foot") or z < 0.28:
            return dark
        return steel

    rig = npc(height, dict(gender=1.0, age=0.45, muscle=0.85, weight=0.6, height=0.8, proportions=0.8), pick)
    body = rig.body

    def gear(centre, part, normal):
        z = centre.z / height
        if part == "head":
            return gold, 0.06
        if part == "neck":
            return dark, 0.06
        if part == "torso":
            return steel, 0.07
        if part in ("upperarm", "forearm"):
            return steel, 0.05
        if part == "hand":
            return dark, 0.03
        if part in ("thigh", "shin"):
            return steel, 0.05
        if part == "foot":
            return dark, 0.04
        return None

    mpfb_body.shell(body, gear, "armour")
    # The helm's crest and visor, layered shoulder plates, a plate skirt and
    # the cloak behind.
    rig.attach(shape("box", (0.03, 0.12, 0.018), slit), "head", (0.19, 0, rig.top * 0.50))
    rig.attach(shape("box", (0.18, 0.025, 0.06), gold), "head", (-0.02, 0, rig.top * 0.98))
    for side, s in (("l", 1), ("r", -1)):
        for k, (r, dz) in enumerate(((0.24, 0.06), (0.20, -0.08), (0.17, -0.20))):
            rig.attach(shape("ball", (r, r * 1.05, r * 0.6), steel if k != 1 else dark), f"arm_{side}",
                       (0, s * (0.05 + 0.02 * k), dz))
    rig.attach(sharp("flare", (0.34, 0.38, 0.22), steel), "hips", (0.0, 0, -0.10))
    rig.attach(shape("cylinder", (0.27, 0.32, 0.04), dark), "hips", (0.0, 0, 0.14))
    length = rig.hip + rig.shoulder - 0.04
    rig.attach(sharp("flare", (0.30, 0.46, length / 2), cloak), "spine", (-0.14, 0, rig.shoulder - length / 2))
    rig.kind = "steel knight"
    rig.poses = {
        "idle": lambda s, c, k: stand(s, arm_l=(0.28, -0.02 * s, 0), elbow_l=(0, -0.2, 0),
                                      arm_r=(-0.28, -0.02 * s, 0), elbow_r=(0, -0.2, 0)),
        "walk": lambda s, c, k: stroll(s, c, swing=0.45, arms=False, arm_l=(0.28, 0.3 * s, 0),
                                       arm_r=(-0.28, -0.3 * s, 0)),
    }
    return rig


def build_shrine_keeper():
    """The shrine keeper (people kind 38): a slight young woman with long
    straight fair hair, a short honey-coloured dress with a fitted bodice,
    pale stockings and brown shoes, her arms held a little out from her
    sides."""
    import mpfb_body
    skin = studio.material("skin", (0.94, 0.78, 0.64), (0.76, 0.58, 0.46), scale=30, bump=0.05)
    hair = studio.material("fair hair", (0.92, 0.78, 0.46), (0.58, 0.44, 0.20), scale=50, bump=0.5,
                           stretch=(1, 1, 5))
    dress = studio.material("honey dress", (0.80, 0.62, 0.34), (0.44, 0.30, 0.12), scale=12, bump=0.35,
                            stretch=(1, 1, 3))
    bodice = studio.material("bodice", (0.62, 0.44, 0.22), (0.30, 0.18, 0.06), scale=18, bump=0.3)
    stockings = studio.material("stockings", (0.90, 0.90, 0.92), (0.66, 0.66, 0.72), scale=20)
    shoes = studio.material("shoes", (0.40, 0.24, 0.12), (0.16, 0.09, 0.04), scale=25, rough=0.55)
    height = 3.42

    def pick(centre, bones, normal):
        z = centre.z / height
        part = mpfb_body.part(bones)
        if part in ("hand", "head", "neck", "upperarm", "forearm"):
            return skin
        if part == "foot":
            return shoes
        if z < 0.50:
            return stockings
        return bodice

    rig = npc(height, dict(gender=0.0, age=0.3, muscle=0.4, weight=0.35, height=0.4, proportions=0.8), pick)
    body = rig.body

    def gear(centre, part, normal):
        z = centre.z / height
        if part == "head" and (z > 0.925 or (z > 0.84 and normal.y > -0.1)
                               or (z > 0.86 and abs(normal.x) > 0.6)):
            return hair, 0.035
        if part == "neck" and normal.y > -0.1:
            return hair, 0.04
        if part == "torso" and z > 0.50:
            return bodice, 0.03
        if part == "upperarm" and z > 0.74:
            return dress, 0.04  # little puffed sleeves
        return None

    mpfb_body.shell(body, gear, "clothes")
    # Long hair down her back, and the short skirt.
    rig.attach(shape("box", (0.05, 0.17, 0.30), hair, rotation=(0, math.radians(8), 0)), "spine",
               (-0.20, 0, rig.shoulder + 0.05))
    length = rig.hip * 0.50
    rig.attach(sharp("flare", (0.36, 0.36, length / 2), dress), "hips", (0.02, 0, 0.14 - length / 2))
    rig.kind = "shrine keeper"
    rig.poses = {
        "idle": lambda s, c, k: stand(s, arm_l=(0.36, -0.02 * s, 0), elbow_l=(0, -0.10, 0),
                                      arm_r=(-0.36, -0.02 * s, 0), elbow_r=(0, -0.10, 0)),
        "walk": lambda s, c, k: stroll(s, c, swing=0.45),
    }
    return rig


BUILDERS.update({"person3": build_gate_guard, "person31": build_blue_bishop, "person32": build_red_priest,
                 "person33": build_padded_priest, "person34": build_green_dwarf, "person35": build_brown_dwarf,
                 "person36": build_bronze_knight, "person37": build_steel_knight, "person38": build_shrine_keeper})
HEIGHT.update({"person3": 3.75, "person31": 3.95, "person32": 4.1, "person33": 3.7, "person34": 3.0,
               "person35": 2.6, "person36": 4.0, "person37": 3.85, "person38": 3.4})


# The fourth area's people: kind 39 the injured demon lying in the Forest
# of Poison, 40 Mrs. Jurg of Berini Village and 41-45 the villagers of
# Ento, whom Hiperion's power turned into demons. On MakeHuman bodies like
# the third area's, with snouts, horns, claws, tails and wings of shapes.
def sole(side):
    """Where a foot's sole is, in its knee joint's space (bind()'s marker)."""
    return bpy.data.objects[f"marker knee_{side}"].location.copy()


def palm(rig, side):
    """Where a hand's palm is, in its elbow joint's space."""
    p = rig.rest["weapon"].copy()
    p.y *= 1 if side == "r" else -1
    return p


def claws(rig, joint, at, mat, count=3, length=0.14, spread=0.45, radius=0.03, pitch=0.0):
    """A fan of claws from `at` in a joint's space, pointing forward (+x)
    and tipped down by pitch."""
    for k in range(count):
        a = spread * (k - (count - 1) / 2)
        way = Vector((math.cos(a) * math.cos(pitch), math.sin(a) * math.cos(pitch), -math.sin(pitch)))
        rig.attach(sharp("cone", (radius, radius, length / 2), mat,
                         rotation=Vector((0, 0, 1)).rotation_difference(way).to_euler()), joint,
                   tuple(Vector(at) + way * length / 2))


def demon_claws(rig, mat, foot=0.16, hand=0.09, toes=3):
    """Claws on both feet (forward from the toes) and both hands (down
    from the fingers)."""
    for side in ("l", "r"):
        claws(rig, f"knee_{side}", sole(side) + Vector((0.12, 0, 0.03)), mat, count=toes, length=foot)
        claws(rig, f"elbow_{side}", palm(rig, side) + Vector((0, 0, -0.06)), mat, length=hand, spread=0.35,
              radius=0.02, pitch=math.radians(80))


def tail(rig, mat, length, radius, droop, spikes=None, spike_size=0.12):
    """A tail on its own joint behind the hips, curving back and down to
    `droop` below the joint, tapering from radius; a row of spikes along
    its top if spikes is a material."""
    rig.joint("tail", "hips", (-0.14, 0, -0.06))
    n = 7
    points = [Vector((-length * i / n, 0, -droop * math.sin(0.5 * math.pi * i / n))) for i in range(n + 1)]
    for i in range(n):
        r = radius * (1 - 0.8 * i / n)
        segment(rig, "tail", points[i], points[i + 1], r, mat)
        rig.attach(shape("ball", (r, r, r), mat), "tail", tuple(points[i + 1]))
        if spikes:
            rig.attach(sharp("cone", (spike_size * 0.35, spike_size * 0.2, spike_size * (1 - 0.6 * i / n)), spikes,
                             rotation=(0, math.radians(-40), 0)), "tail", tuple(points[i] + Vector((0, 0, r * 0.9))))


def build_injured_demon():
    """The injured demon (people kind 39), lying where it fell in the Forest
    of Poison: a dark slate body on its back in a pool of blood, its great
    wings of blue bone and pale membrane spread on the ground, spears and
    arrows stuck in it and broken round it. It stirs as it breathes."""
    import mpfb_body
    skin = studio.material("slate skin", (0.30, 0.30, 0.40), (0.12, 0.12, 0.18), scale=20, bump=0.5)
    bone = studio.material("blue bone", (0.20, 0.22, 0.74), (0.07, 0.07, 0.34), scale=24, rough=0.45)
    membrane = studio.material("pale membrane", (0.88, 0.86, 0.64), (0.54, 0.52, 0.36), scale=5, bump=0.2)
    membrane.use_backface_culling = False
    blood = studio.material("blood", (0.48, 0.04, 0.04), (0.24, 0.01, 0.01), scale=12, rough=0.3)
    haft = studio.material("haft", (0.42, 0.28, 0.16), (0.18, 0.10, 0.05), scale=25, stretch=(1, 1, 4))
    steel = studio.material("spearhead", (0.82, 0.82, 0.84), (0.44, 0.44, 0.48), scale=30, rough=0.3)
    height = 3.8

    def pick(centre, bones, normal):
        return skin

    rig = npc(height, dict(gender=1.0, age=0.5, muscle=0.8, weight=0.4, height=0.7, proportions=0.6), pick)
    # Small horns, and the wings from the shoulder blades.
    for side in (1, -1):
        rig.attach(sharp("cone", (0.04, 0.04, 0.10), bone, rotation=(math.radians(-25 * side), math.radians(-30), 0)),
                   "head", (-0.02, 0.09 * side, rig.top * 0.82))
        name = "bat_l" if side > 0 else "bat_r"
        rig.joint(name, "spine", (0.02, 0.12 * side, rig.shoulder - 0.06))
        wing(rig, name, side, 2.3, bone, membrane)
        rig.joints[name].scale = (0.15, 1, 1)  # pressed flat on the ground
    # The pool of blood under its back (the body's -x is the ground once it
    # lies on its back), spears in its chest and broken shafts about it.
    for y, z, ry, rz in ((0, 0.25, 0.60, 0.95), (0.50, 0.75, 0.36, 0.30), (-0.55, -0.10, 0.32, 0.40),
                         (0.20, -0.60, 0.30, 0.36), (-0.40, 0.95, 0.26, 0.22), (0.68, 0.05, 0.16, 0.24),
                         (-0.15, -0.95, 0.10, 0.30), (0.75, -0.40, 0.08, 0.26)):
        rig.attach(shape("ball", (0.012, ry, rz), blood), "hips", (-0.26, y, z))
    for k in range(24):  # spattered over the wings too
        a = 2.4 * k
        r = 0.45 + 0.08 * (k % 7)
        rig.attach(shape("ball", (0.01, 0.03 + 0.01 * (k % 3), 0.05 + 0.02 * (k % 2)), blood), "hips",
                   (-0.26 if k % 2 else 0.05, 1.2 * r * math.cos(a), 0.30 + 1.5 * r * math.sin(a)))
    for at, way, length in (((0.10, 0.06, 0.45), (1.0, 0.5, 0.6), 1.1), ((0.12, -0.10, 0.20), (1.0, -0.4, -0.3), 0.8),
                            ((0.0, 0.25, -0.30), (1.0, 0.7, 0.1), 0.5)):
        way = Vector(way).normalized()
        segment(rig, "spine", Vector(at), Vector(at) + way * length * 1.2, 0.03, haft)
        rig.attach(sharp("cone", (0.04, 0.03, 0.08), steel, rotation=Vector((0, 0, 1)).rotation_difference(-way).to_euler()),
                   "spine", tuple(Vector(at) - way * 0.04))
    for y, z, a, length in ((0.95, 0.55, 0.4, 1.0), (-0.80, 1.05, -0.9, 0.6), (-0.70, -0.60, 0.2, 0.45)):
        way = Vector((0, math.cos(a), math.sin(a)))
        segment(rig, "hips", Vector((-0.24, y, z)), Vector((-0.24, y, z)) + way * length, 0.03, haft)
        rig.attach(sharp("cone", (0.035, 0.012, 0.07), steel, rotation=Vector((0, 0, 1)).rotation_difference(way).to_euler()),
                   "hips", tuple(Vector((-0.24, y, z)) + way * (length + 0.06)))
    # Its middle, not its feet, on the spot it stands on.
    rig.rest["hips"] = rig.rest["hips"] - Vector((0, 0, rig.hip * 0.95))
    rig.kind = "injured demon"
    # Drawn with no direction (8), like the original: it lies as it fell.
    rig.animations = {"idle": (101, True, 0)}

    def lying(s, c, k):
        twitch = 3 * s - 4 * s ** 3  # three times a breath
        return stand(s, root=(0, -math.pi / 2, 0), spine=(0, 0.05 * s, 0), head=(0.25, 0.2, 0.3),
                     arm_l=(1.25, 0.1, 0), elbow_l=(0, -0.5, 0), arm_r=(-1.0, -0.4, 0), elbow_r=(0, -0.9, 0),
                     leg_l=(0.22, 0.05, 0), leg_r=(-0.12, -0.35, 0), knee_r=(0, 0.5, 0),
                     bat_l=(0.35 + 0.06 * twitch, 0, 0), bat_r=(-0.25, 0, 0))

    rig.poses = {"idle": lying}
    return rig


def build_hooded_woman():
    """Mrs. Jurg (people kind 40): a small woman in a long lilac-grey robe
    with its hood up, her hands together at her chest round a little lamp."""
    import mpfb_body
    skin = studio.material("skin", (0.90, 0.74, 0.62), (0.70, 0.52, 0.42), scale=30, bump=0.05)
    robe = studio.material("lilac robe", (0.72, 0.64, 0.76), (0.38, 0.32, 0.44), scale=10, bump=0.4,
                           stretch=(1, 1, 3))
    lamp = studio.material("lamp glow", (1.0, 0.64, 0.24), (0.90, 0.38, 0.08), scale=30, rough=0.3)
    height = 3.4

    def pick(centre, bones, normal):
        part = mpfb_body.part(bones)
        if part in ("hand", "head", "neck"):
            return skin
        return robe

    rig = npc(height, dict(gender=0.0, age=0.7, muscle=0.3, weight=0.4, height=0.3, proportions=0.5), pick)
    body = rig.body

    def gear(centre, part, normal):
        z = centre.z / height
        face = normal.y < -0.35 and 0.86 < z < 0.95 and abs(centre.x) < 0.17
        if part in ("head", "neck") and not face:
            return robe, 0.06  # the hood
        if part in ("torso", "upperarm"):
            return robe, 0.05
        if part == "forearm":
            return robe, 0.07  # wide sleeves
        return None

    mpfb_body.shell(body, gear, "clothes")
    # The robe to the ground, the hood's peak over her brow and the lamp.
    length = rig.hip + 0.06
    rig.attach(sharp("flare", (0.27, 0.28, length / 2), robe), "hips", (0.0, 0, 0.14 - length / 2))
    rig.attach(shape("ball", (0.19, 0.20, 0.15), robe), "head", (-0.02, 0, rig.top * 0.72))
    rig.attach(shape("ball", (0.05, 0.05, 0.06), lamp), "spine", (0.36, 0, rig.shoulder - 0.40))
    rig.kind = "hooded woman"
    hands = dict(arm_l=(0.05, -0.25, 0), elbow_l=(-0.7, -1.5, 0), arm_r=(-0.05, -0.25, 0), elbow_r=(0.7, -1.5, 0))
    rig.poses = {
        "idle": lambda s, c, k: stand(s, **hands),
        "walk": lambda s, c, k: stroll(s, c, swing=0.35, arms=False, **hands),
    }
    return rig


def build_purple_demon():
    """Giraa and Beng (people kind 41): a gaunt purple demon, hunched, with
    long arms hanging wide and clawed hands and feet, a snout, a brown cap
    with a pale band, a white bib under a brown jerkin, an orange belt and
    a long lilac apron front and back."""
    import mpfb_body
    skin = studio.material("purple skin", (0.70, 0.52, 0.70), (0.40, 0.26, 0.42), scale=18, bump=0.4)
    jerkin = studio.material("brown jerkin", (0.42, 0.22, 0.12), (0.18, 0.08, 0.04), scale=18, bump=0.3)
    bib = studio.material("white bib", (0.92, 0.88, 0.80), (0.62, 0.58, 0.52), scale=20)
    belt = studio.material("orange belt", (0.90, 0.48, 0.12), (0.52, 0.22, 0.04), scale=26, rough=0.5)
    apron = studio.material("lilac apron", (0.68, 0.54, 0.72), (0.36, 0.26, 0.40), scale=10, bump=0.3,
                            stretch=(1, 1, 4))
    cap = studio.material("brown cap", (0.44, 0.24, 0.10), (0.20, 0.10, 0.04), scale=22, bump=0.3)
    band = studio.material("pale band", (0.90, 0.86, 0.74), (0.60, 0.56, 0.46), scale=26)
    claw = studio.material("dark claws", (0.30, 0.20, 0.22), (0.10, 0.06, 0.08), scale=30, rough=0.4)
    height = 4.95

    def pick(centre, bones, normal):
        return skin

    long_arms = [(f"arms/{s}-{p}-{t}-incr.target.gz", 1.0) for s in "lr" for p in ("upperarm", "lowerarm")
                 for t in ("scale-vert", "muscle", "scale-horiz")]
    rig = npc(height, dict(gender=1.0, age=0.5, muscle=1.0, weight=0.8, height=0.9, proportions=0.3), pick,
              targets=long_arms + [(f"hands/{s}-hand-scale-incr.target.gz", 1.0) for s in "lr"])
    body = rig.body

    def gear(centre, part, normal):
        z = centre.z / height
        if part == "torso" and 0.50 < z < 0.76:
            return jerkin, 0.06
        return None

    mpfb_body.shell(body, gear, "clothes")
    rig.attach(shape("box", (0.05, 0.15, 0.24), bib), "spine", (0.27, 0, rig.shoulder - 0.32))
    rig.attach(shape("cylinder", (0.25, 0.28, 0.05), belt), "hips", (0.0, 0, 0.16))
    length = rig.hip * 0.62
    for x in (0.22, -0.22):  # the apron's two long flaps
        rig.attach(shape("box", (0.025, 0.20, length / 2), apron, rotation=(0, math.radians(-8 if x > 0 else 8), 0)),
                   "hips", (x * 1.1, 0, 0.12 - length / 2))
    # Snout, cap and claws.
    rig.attach(shape("ball", (0.15, 0.10, 0.09), skin), "head", (0.18, 0, rig.top * 0.28))
    rig.attach(shape("ball", (0.21, 0.20, 0.16), cap), "head", (-0.02, 0, rig.top * 0.82))
    rig.attach(shape("cylinder", (0.20, 0.19, 0.03), band), "head", (-0.02, 0, rig.top * 0.70))
    demon_claws(rig, claw)
    rig.kind = "purple demon"
    hunch = dict(spine=(0, 0.32, 0), head=(0, -0.10, 0))
    arms = dict(arm_l=(0.50, -0.30, 0), elbow_l=(-0.2, -0.90, 0), arm_r=(-0.50, -0.30, 0), elbow_r=(0.2, -0.90, 0))
    rig.poses = {
        "idle": lambda s, c, k: dict(stand(s, **arms), spine=(0, 0.32 + 0.02 * s, 0), head=(0, -0.10, 0)),
        "walk": lambda s, c, k: stroll(s, c, swing=0.45, arms=False, **hunch,
                                       arm_l=(0.55, -0.35 + 0.25 * s, 0), arm_r=(-0.55, -0.35 - 0.25 * s, 0),
                                       elbow_l=(-0.2, -0.90, 0), elbow_r=(0.2, -0.90, 0)),
    }
    return rig


def build_jackal_demon():
    """Darl and Mars (people kind 42): a muscular demon with a jackal's long
    snout and tall ears, tawny fur, a ragged grey-green loincloth to the
    knees and clawed feet, standing with his arms folded."""
    import mpfb_body
    fur = studio.material("tawny fur", (0.70, 0.52, 0.28), (0.34, 0.22, 0.08), scale=30, bump=0.7, stretch=(1, 1, 3))
    cloth = studio.material("grey green cloth", (0.54, 0.60, 0.52), (0.28, 0.32, 0.26), scale=10, bump=0.4,
                            stretch=(1, 1, 4))
    claw = studio.material("claws", (0.36, 0.26, 0.18), (0.14, 0.09, 0.05), scale=30, rough=0.4)
    nose = studio.material("nose", (0.20, 0.12, 0.08), (0.08, 0.04, 0.03), scale=30, rough=0.3)
    height = 4.5

    def pick(centre, bones, normal):
        return fur

    rig = npc(height, dict(gender=1.0, age=0.4, muscle=1.0, weight=0.75, height=0.8, proportions=0.6), pick,
              targets=[("torso/torso-scale-horiz-incr.target.gz", 0.6), ("torso/torso-vshape-incr.target.gz", 1.0)])
    body = rig.body

    def gear(centre, part, normal):
        z = centre.z / height
        if part == "thigh" and z > 0.36:
            return cloth, 0.05
        return None

    mpfb_body.shell(body, gear, "clothes")
    # The loincloth, ragged at the hem, and a belt of the same cloth.
    length = rig.hip * 0.52
    rig.attach(sharp("flare", (0.34, 0.36, length / 2), cloth), "hips", (0.0, 0, 0.14 - length / 2))
    rig.attach(shape("cylinder", (0.25, 0.28, 0.05), cloth), "hips", (0.0, 0, 0.15))
    for k in range(12):
        a = 2 * math.pi * (k + 0.5) / 12
        rig.attach(sharp("cone", (0.07, 0.03, 0.08 + 0.05 * (k % 3)), cloth, rotation=(0, math.pi, a)), "hips",
                   (0.33 * math.cos(a), 0.35 * math.sin(a), 0.14 - length - 0.04 * (k % 3)))
    # The jackal's head: a long snout, a dark nose and tall ears.
    rig.attach(sharp("cone", (0.10, 0.09, 0.17), fur, rotation=(0, math.radians(100), 0)), "head",
               (0.24, 0, rig.top * 0.30))
    rig.attach(shape("ball", (0.035, 0.035, 0.03), nose), "head", (0.41, 0, rig.top * 0.27))
    for side in (1, -1):
        rig.attach(sharp("cone", (0.07, 0.03, 0.13), fur, rotation=(math.radians(-18 * side), math.radians(-15), 0)),
                   "head", (-0.06, 0.10 * side, rig.top * 0.95))
    demon_claws(rig, claw, hand=0.06)
    rig.kind = "jackal demon"
    folded = dict(arm_l=(0.30, -0.70, 0), elbow_l=(-1.95, 0, 0), arm_r=(-0.30, -0.70, 0), elbow_r=(1.75, 0, 0))
    rig.poses = {
        "idle": lambda s, c, k: stand(s, leg_l=(0.10, 0, 0), leg_r=(-0.10, 0, 0), **folded),
        "walk": lambda s, c, k: stroll(s, c, swing=0.5, arms=False, **folded),
    }
    return rig


def build_golden_demon():
    """Esta and Hols (people kind 43): a muscular golden demon with great
    bat wings on golden bones, silver bracers, shoulder plate, belt and
    greaves, dark brown breeches and little horns."""
    import mpfb_body
    gold = studio.material("golden skin", (0.88, 0.66, 0.16), (0.48, 0.30, 0.04), scale=16, rough=0.4, bump=0.4)
    silver = studio.material("silver", (0.88, 0.88, 0.90), (0.46, 0.46, 0.52), scale=26, rough=0.3, bump=0.3)
    breeches = studio.material("dark breeches", (0.34, 0.22, 0.12), (0.12, 0.07, 0.03), scale=18, bump=0.3)
    membrane = studio.material("wing membrane", (0.62, 0.56, 0.46), (0.34, 0.28, 0.22), scale=5, bump=0.2)
    membrane.use_backface_culling = False
    height = 3.95

    def pick(centre, bones, normal):
        z = centre.z / height
        part = mpfb_body.part(bones)
        if part in ("thigh", "shin") and z > 0.25:
            return breeches
        return gold

    rig = npc(height, dict(gender=1.0, age=0.45, muscle=1.0, weight=0.8, height=0.7, proportions=0.5), pick,
              targets=[("torso/torso-scale-horiz-incr.target.gz", 0.5)])
    body = rig.body

    def gear(centre, part, normal):
        z = centre.z / height
        if part == "forearm" and z < 0.62:
            return silver, 0.04  # bracers
        if part in ("shin", "foot") and z < 0.25:
            return silver, 0.04  # greaves
        if part == "thigh":
            return breeches, 0.04
        return None

    mpfb_body.shell(body, gear, "armour")
    rig.attach(shape("cylinder", (0.25, 0.29, 0.05), silver), "hips", (0.0, 0, 0.15))
    for side in (1, -1):
        rig.attach(shape("box", (0.04, 0.07, 0.10), silver), "hips", (0.24, 0.12 * side, 0.05))
        rig.attach(sharp("cone", (0.035, 0.035, 0.09), gold, rotation=(math.radians(-25 * side), math.radians(-25), 0)),
                   "head", (0.0, 0.09 * side, rig.top * 0.82))
        name = "bat_l" if side > 0 else "bat_r"
        rig.joint(name, "spine", (-0.18, 0.12 * side, rig.shoulder - 0.04))
        # Spread wide beside him, arching over his shoulders and down to
        # his feet, cupped a little forward.
        bat_wing(rig, name, side, 1.8, gold, membrane, (-0.15, 0.40, 0.36),
                 ((-0.05, 0.95, 0.32), (0.20, 1.40, 0.0), (0.42, 1.28, -0.70), (0.45, 0.92, -1.35)),
                 (-0.10, 0.25, -0.75))
    rig.attach(shape("ball", (0.22, 0.20, 0.16), silver), "arm_l", (0, 0.06, 0.04))  # one shoulder plate
    rig.kind = "golden demon"

    def wings(s):
        return dict(bat_l=(0.04 * s, 0, 0.06 * s), bat_r=(-0.04 * s, 0, -0.06 * s))

    rig.poses = {
        "idle": lambda s, c, k: stand(s, arm_l=(0.28, -0.1, 0), elbow_l=(0, -0.3, 0), arm_r=(-0.28, -0.1, 0),
                                      elbow_r=(0, -0.3, 0), **wings(s)),
        "walk": lambda s, c, k: stroll(s, c, swing=0.5, **wings(s)),
    }
    return rig


def build_lizard_demon():
    """Darlon, Deble, Mad and Rocur (people kind 44): a grey lizard demon
    with a crest swept back from its head, long curved spines down its
    back, a spiked tail, dark leggings and great three-toed clawed feet."""
    import mpfb_body
    hide = studio.material("grey hide", (0.66, 0.64, 0.58), (0.32, 0.30, 0.28), scale=40, bump=0.8)
    legs = studio.material("dark leggings", (0.30, 0.30, 0.30), (0.12, 0.12, 0.12), scale=20, bump=0.4)
    spike = studio.material("pale spikes", (0.88, 0.86, 0.76), (0.52, 0.50, 0.42), scale=30, rough=0.4)
    height = 4.85

    def pick(centre, bones, normal):
        return hide

    rig = npc(height, dict(gender=1.0, age=0.5, muscle=1.0, weight=0.9, height=0.8, proportions=0.5), pick,
              targets=[("torso/torso-scale-horiz-incr.target.gz", 0.8), ("torso/torso-scale-depth-incr.target.gz", 0.5)]
              + [(f"arms/{s}-{p}-muscle-incr.target.gz", 1.0) for s in "lr" for p in ("upperarm", "lowerarm")])
    body = rig.body

    def gear(centre, part, normal):
        z = centre.z / height
        if part in ("thigh", "shin") and 0.08 < z < 0.50:
            return legs, 0.04
        return None

    mpfb_body.shell(body, gear, "clothes")
    # A blunt snout, the crest and the spines down the back.
    rig.attach(shape("ball", (0.16, 0.12, 0.10), hide), "head", (0.16, 0, rig.top * 0.30))
    rig.attach(sharp("cone", (0.09, 0.05, 0.30), hide, rotation=(0, math.radians(-40), 0)), "head",
               (-0.16, 0, rig.top * 0.95))
    for k in range(4):
        for side in (1, -1):
            rig.attach(sharp("cone", (0.06, 0.05, 0.28 - 0.04 * k), spike,
                             rotation=(math.radians(-60 * side), math.radians(-60 - 8 * k), 0)), "spine",
                       (-0.30, (0.20 + 0.04 * k) * side, rig.shoulder - 0.05 - 0.17 * k))
    tail(rig, hide, 1.4, 0.13, rig.hip - 0.18, spikes=spike)
    demon_claws(rig, spike, foot=0.22)
    for side in ("l", "r"):  # the dewclaw behind each foot
        claws(rig, f"knee_{side}", sole(side) + Vector((-0.06, 0, 0.03)), spike, count=1, length=0.10,
              pitch=math.radians(180))
    rig.kind = "lizard demon"
    rig.poses = {
        "idle": lambda s, c, k: dict(stand(s, arm_l=(0.30, -0.1, 0), elbow_l=(0, -0.45, 0), arm_r=(-0.30, -0.1, 0),
                                           elbow_r=(0, -0.45, 0), leg_l=(0.12, 0, 0), leg_r=(-0.12, 0, 0)),
                                     tail=(0, 0, 0.12 * s)),
        "walk": lambda s, c, k: dict(stroll(s, c, swing=0.5), tail=(0, 0, 0.25 * s)),
    }
    return rig


def build_horned_demon():
    """Gargan (people kind 45): a huge demon in a long coat of green scales
    banded with gold, curled ram's horns, heavy shoulders and a spiked tail
    behind him."""
    import mpfb_body
    hide = studio.material("green hide", (0.28, 0.44, 0.26), (0.10, 0.20, 0.10), scale=30, bump=0.7)
    scales = studio.material("green scale coat", (0.30, 0.50, 0.28), (0.06, 0.16, 0.06), scale=14, bump=1.0,
                             spot=0.10, spot_colour=(0.70, 0.56, 0.22))
    horn = studio.material("horn", (0.84, 0.72, 0.46), (0.46, 0.36, 0.20), scale=40, rough=0.4, stretch=(4, 1, 1))
    spike = studio.material("pale spikes", (0.90, 0.86, 0.70), (0.54, 0.50, 0.36), scale=30, rough=0.4)
    height = 4.8

    def pick(centre, bones, normal):
        return hide

    rig = npc(height, dict(gender=1.0, age=0.6, muscle=0.9, weight=1.0, height=0.8, proportions=0.4), pick,
              targets=[("torso/torso-scale-horiz-incr.target.gz", 1.0), ("torso/torso-scale-depth-incr.target.gz", 0.8)])
    body = rig.body

    def gear(centre, part, normal):
        if part in ("torso", "upperarm", "forearm", "thigh"):
            return scales, 0.12
        return None

    mpfb_body.shell(body, gear, "coat")
    length = rig.hip * 0.80
    rig.attach(sharp("flare", (0.56, 0.58, length / 2), scales), "hips", (0.0, 0, 0.12 - length / 2))
    for side in (1, -1):
        rig.attach(shape("ball", (0.26, 0.24, 0.22), scales), "arm_l" if side > 0 else "arm_r", (0, 0.03 * side, 0.02))
        # A ram's horn: back from the temple, down round the ear and forward.
        rig.attach(tube([(0.00, 0.10 * side, 0.30), (-0.16, 0.20 * side, 0.36), (-0.24, 0.30 * side, 0.12),
                         (-0.04, 0.34 * side, -0.10), (0.14, 0.30 * side, 0.0)], 0.065, horn), "head")
    rig.attach(shape("ball", (0.14, 0.12, 0.10), hide), "head", (0.16, 0, rig.top * 0.28))
    tail(rig, hide, 1.3, 0.14, rig.hip - 0.18, spikes=spike, spike_size=0.16)
    demon_claws(rig, spike, foot=0.16)
    rig.kind = "horned demon"
    rig.poses = {
        "idle": lambda s, c, k: dict(stand(s, arm_l=(0.38, -0.05, 0), elbow_l=(0, -0.3, 0), arm_r=(-0.38, -0.05, 0),
                                           elbow_r=(0, -0.3, 0)), tail=(0, 0, 0.10 * s)),
        "walk": lambda s, c, k: dict(stroll(s, c, swing=0.4, arms=False, arm_l=(0.38, 0.25 * s, 0),
                                            arm_r=(-0.38, -0.25 * s, 0)), tail=(0, 0, 0.22 * s)),
    }
    return rig


BUILDERS.update({"person39": build_injured_demon, "person40": build_hooded_woman, "person41": build_purple_demon,
                 "person42": build_jackal_demon, "person43": build_golden_demon, "person44": build_lizard_demon,
                 "person45": build_horned_demon})
HEIGHT.update({"person39": 1.0, "person40": 3.3, "person41": 4.4, "person42": 4.4, "person43": 4.2,
               "person44": 4.8, "person45": 4.8})
SIZE.update({"person39": 240, "person43": 240})


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


def lathe(name, a, b, profile, mat, flat=1.0, sides=10):
    """A solid of revolution from world point a to b; profile is (t, radius)
    pairs along it (t from 0 at a to 1 at b). flat squashes it sideways."""
    a, b = Vector(a), Vector(b)
    length = (b - a).length
    verts, faces = [], []
    for t, r in profile:
        for k in range(sides):
            ang = 2 * math.pi * k / sides
            verts.append((r * math.cos(ang), r * flat * math.sin(ang), t * length))
    n = len(profile)
    for i in range(n - 1):
        for k in range(sides):
            faces.append((i * sides + k, i * sides + (k + 1) % sides, (i + 1) * sides + (k + 1) % sides,
                          (i + 1) * sides + k))
    faces.append(tuple(reversed(range(sides))))
    faces.append(tuple(range((n - 1) * sides, n * sides)))
    data = bpy.data.meshes.new(name)
    data.from_pydata(verts, [], faces)
    obj = bpy.data.objects.new(name, data)
    bpy.context.scene.collection.objects.link(obj)
    obj.location = a
    obj.rotation_euler = Vector((0, 0, 1)).rotation_difference(b - a).to_euler()
    studio.smooth(obj)
    return studio.assign(obj, mat)


def long_bone(a, b, r, mat):
    """A limb bone from a to b: a shaft, thinnest in the middle, between
    knobbly ends."""
    return lathe("bone", a, b, [(0.0, 0.3 * r), (0.03, 1.45 * r), (0.09, 1.6 * r), (0.18, 1.0 * r), (0.5, 0.8 * r),
                                (0.82, 1.0 * r), (0.91, 1.55 * r), (0.97, 1.4 * r), (1.0, 0.3 * r)], mat, flat=0.85)


def tube(points, r, mat, name="tube"):
    """A smooth bent tube through world points (a rib, a collarbone)."""
    curve = bpy.data.curves.new(name, "CURVE")
    curve.dimensions = "3D"
    curve.bevel_depth = r
    curve.bevel_resolution = 2
    curve.use_fill_caps = True
    spline = curve.splines.new("NURBS")
    spline.points.add(len(points) - 1)
    for p, q in zip(spline.points, points):
        p.co = (*q, 1)
    spline.use_endpoint_u = True
    spline.order_u = 3
    obj = bpy.data.objects.new(name, curve)
    bpy.context.scene.collection.objects.link(obj)
    obj.data.materials.append(mat)
    return obj


def placed(obj, location):
    obj.location = location
    return obj


def adopt(child, parent):
    """Parents child to parent where it stands now."""
    bpy.context.view_layer.update()
    child.parent = parent
    child.matrix_parent_inverse = parent.matrix_world.inverted()
    return child


def chunk(rig, joint, pieces, axis=(0, 0, 1), radius=0.05, throw=1.0):
    """Gathers pieces (made where they stand in the rest pose) under an
    empty on a joint. A creature that falls apart when it dies (see
    fall_apart) drops each chunk as one: axis is its long axis, which ends
    up lying on the ground, radius how high it then rests and throw how far
    it is flung."""
    bpy.context.view_layer.update()
    corners = [o.matrix_world @ Vector(c) for o in pieces if o.type in ("MESH", "CURVE") for c in o.bound_box]
    empty = bpy.data.objects.new("chunk", None)
    bpy.context.scene.collection.objects.link(empty)
    empty.location = sum(corners, Vector()) / len(corners)
    adopt(empty, rig.joints[joint])
    for o in pieces:
        adopt(o, empty)
    rig.chunks.append((empty, Vector(axis), radius, throw))
    return empty


def build_skeleton():
    """A skeleton warrior on MakeHuman proportions (mpfb_body.py; the body
    itself is hidden and only lends its joints): every bone modelled, the
    skull grinning under a black iron helm, a dented black cuirass with
    round pauldrons and a fauld of plates over the hips. It carries a long
    rust-red saw-toothed sword and a round brass-bound shield, and when it
    dies it falls apart into a heap of bones."""
    import mpfb_body
    bone = studio.material("bone", (0.86, 0.84, 0.72), (0.50, 0.46, 0.34), scale=25, rough=0.55, bump=0.5)
    hollow = studio.material("socket", (0.05, 0.04, 0.03), scale=10)
    iron = studio.material("black iron", (0.34, 0.34, 0.37), (0.09, 0.09, 0.10), scale=22, rough=0.35, bump=0.4,
                           grime=0.5)
    rust = studio.material("rust blade", (0.86, 0.50, 0.36), (0.46, 0.18, 0.10), scale=30, rough=0.35, bump=0.5,
                           stretch=(4, 1, 1))
    brass = studio.material("shield brass", (0.92, 0.64, 0.22), (0.50, 0.26, 0.06), scale=24, rough=0.3, bump=0.3)
    bronze = studio.material("shield bronze", (0.52, 0.30, 0.12), (0.22, 0.11, 0.04), scale=24, rough=0.4, bump=0.4)
    leather = studio.material("grip leather", (0.24, 0.15, 0.09), (0.08, 0.05, 0.03), scale=25, rough=0.6)
    height = 3.1
    body, armature = mpfb_body.human(height, dict(gender=1.0, age=0.5, muscle=0.3, weight=0.0, height=0.5,
                                                   proportions=0.6, caucasian=1.0, african=0.0, asian=0.0))
    mpfb_body.bake_rest(body, armature, arm_out=0.20, elbow_forward=0.2)
    rig = Rig()
    mpfb_body.bind(rig, body, armature, 1.0)
    body.hide_render = True
    rig.kind = "skeleton"
    rig.height = height
    rig.chunks = []
    aw = armature.matrix_world

    def H(b):
        return aw @ armature.data.bones[b].head_local

    def T(b):
        return aw @ armature.data.bones[b].tail_local

    # Skull, jaw and helm: the head's middle and size from the head bone.
    top, neck = T("head"), H("head")
    s = (top.z - neck.z) / 0.30
    c = Vector((neck.x - 0.03 * s, 0, neck.z + 0.15 * s))

    def at(x, y, z):
        return c + Vector((x, y, z)) * s

    skull = [placed(shape("ball", (0.150 * s, 0.118 * s, 0.135 * s), bone), at(0, 0, 0.01)),
             placed(shape("ball", (0.085 * s, 0.090 * s, 0.075 * s), bone), at(0.085, 0, -0.085)),
             placed(shape("ball", (0.075 * s, 0.085 * s, 0.035 * s), bone, rotation=(0, math.radians(-15), 0)),
                    at(0.075, 0, -0.165)),
             placed(shape("box", (0.012 * s, 0.055 * s, 0.018 * s), bone), at(0.145, 0, -0.130))]
    for side in (1, -1):
        skull.append(placed(shape("ball", (0.035 * s, 0.035 * s, 0.032 * s), hollow), at(0.122, 0.048 * side, -0.035)))
        skull.append(placed(shape("ball", (0.04 * s, 0.03 * s, 0.025 * s), bone), at(0.105, 0.085 * side, -0.075)))
    skull.append(placed(shape("ball", (0.012 * s, 0.014 * s, 0.022 * s), hollow), at(0.152, 0, -0.085)))
    skull.append(placed(shape("box", (0.006 * s, 0.050 * s, 0.004 * s), hollow), at(0.152, 0, -0.130)))
    # The helm: a round iron cap with a brim and a ridge over the crown.
    skull.append(placed(shape("ball", (0.178 * s, 0.150 * s, 0.140 * s), iron), at(-0.012, 0, 0.065)))
    brim = placed(shape("cylinder", (0.205 * s, 0.180 * s, 0.012 * s), iron, rotation=(0, math.radians(-12), 0)),
                  at(-0.02, 0, 0.035))
    skull.append(brim)
    skull.append(placed(shape("box", (0.15 * s, 0.012 * s, 0.03 * s), iron), at(-0.01, 0, 0.19)))
    for side in (1, -1):
        skull.append(placed(shape("box", (0.05 * s, 0.012 * s, 0.07 * s), iron), at(-0.02, 0.150 * side, -0.05)))
    chunk(rig, "head", skull, axis=(1, 0, 0), radius=0.14 * s)
    # Neck and spine: a chain of vertebrae with spines down the back.
    spine = [H("pelvis") + Vector((-0.04, 0, 0.12)), H("spine_02"), H("spine_03"), T("spine_03"), H("neck_01")]
    pts = []
    for (a, b) in zip(spine, spine[1:]):
        for k in range(4):
            pts.append(a.lerp(b, k / 4))
    neck_pts = [H("neck_01").lerp(H("head"), k / 4) for k in range(1, 4)]
    back = []
    for i, p in enumerate(pts):
        w = 0.042 + 0.012 * (1 - i / len(pts))
        back.append(placed(shape("ball", (w, w * 1.15, 0.022), bone), p + Vector((-0.06, 0, 0))))
        back.append(placed(shape("cone", (0.014, 0.012, 0.035), bone, rotation=(0, math.radians(-120), 0)),
                           p + Vector((-0.11, 0, -0.01))))
    # Ribs: pairs curving round from the spine to the breastbone.
    sternum_top, sternum_bottom = Vector((0.13, 0, 2.70 * height / 3.4)), Vector((0.15, 0, 2.38 * height / 3.4))
    for i in range(7):
        u = i / 6
        z = (2.70 - 0.36 * u) * height / 3.4
        w = 0.13 + 0.09 * math.sin(math.pi * min(1.0, 0.25 + u * 0.75))
        front = 0.10 + 0.06 * math.sin(math.pi * min(1.0, 0.2 + u * 0.6))
        for side in (1, -1):
            rib = tube([(-0.06, 0.03 * side, z), (-0.07, 0.6 * w * side, z + 0.01), (0.0, w * side, z - 0.03),
                        (0.10, 0.75 * w * side, z - 0.07), (front + 0.02, 0.04 * side, z - 0.10)], 0.016, bone, "rib")
            chunk(rig, "spine", [rib], axis=(1, 0, 0), radius=0.03, throw=1.3)
    chunk(rig, "spine", back, axis=(0, 0, 1), radius=0.05)
    back = [placed(shape("box", (0.02, 0.03, (sternum_top.z - sternum_bottom.z) / 2), bone),
                   (sternum_top + sternum_bottom) / 2 + Vector((0.0, 0, -0.06)))]
    for side in ("l", "r"):
        back.append(tube([H(f"clavicle_{side}") + Vector((0.08, 0, -0.05)), H(f"clavicle_{side}").lerp(T(f"clavicle_{side}"), 0.5)
                          + Vector((0.07, 0, 0.0)), T(f"clavicle_{side}") + Vector((0.0, 0, 0.02))], 0.018, bone, "clavicle"))
    # The cuirass over the upper chest, with a ridge down its front.
    chunk(rig, "spine", back, axis=(0, 1, 0), radius=0.03, throw=1.3)
    chest = (H("spine_03") + T("spine_03")) / 2 + Vector((0.03, 0, 0.12))
    back = []
    back.append(placed(shape("ball", (0.22, 0.29, 0.25), iron), chest))
    back.append(placed(shape("box", (0.03, 0.02, 0.18), iron, rotation=(math.radians(45), 0, 0)),
                       chest + Vector((0.18, 0, 0.0))))
    for side in (1, -1):  # lames under the arms
        back.append(placed(shape("ball", (0.15, 0.08, 0.10), iron), chest + Vector((0.0, 0.20 * side, -0.14))))
    chunk(rig, "spine", back, axis=(0, 0, 1), radius=0.14)
    neck = [placed(shape("ball", (0.032, 0.035, 0.02), bone), p + Vector((-0.02, 0, 0))) for p in neck_pts]
    chunk(rig, "head", neck, radius=0.03)
    # The pelvis, with a fauld of iron plates hanging over its front.
    hip = H("pelvis")
    pelvis = [placed(shape("ball", (0.05, 0.065, 0.085), bone), hip + Vector((-0.08, 0, 0.02)))]
    for side in (1, -1):
        pelvis.append(placed(shape("ball", (0.05, 0.11, 0.10), bone, rotation=(math.radians(-25 * side), 0, 0)),
                             hip + Vector((-0.02, 0.12 * side, 0.08))))
        pelvis.append(placed(shape("ball", (0.045, 0.045, 0.045), bone), H(f"thigh_{'l' if side > 0 else 'r'}")))
        pelvis.append(tube([hip + Vector((-0.02, 0.15 * side, -0.02)), hip + Vector((0.07, 0.12 * side, -0.08)),
                            hip + Vector((0.10, 0.02 * side, -0.07))], 0.022, bone, "pubis"))
    pelvis.append(placed(shape("cylinder", (0.17, 0.22, 0.035), iron), hip + Vector((-0.01, 0, 0.12))))
    for k in range(3):
        y = (k - 1) * 0.13
        pelvis.append(placed(shape("box", (0.02, 0.065, 0.11), iron, rotation=(0, math.radians(-12), 0)),
                             hip + Vector((0.16, y, -0.02))))
    chunk(rig, "hips", pelvis, axis=(0, 1, 0), radius=0.10)
    for side in ("l", "r"):
        sy = 1 if side == "l" else -1
        arm = [long_bone(H(f"upperarm_{side}"), T(f"upperarm_{side}"), 0.042, bone),
               placed(shape("ball", (0.16, 0.16, 0.11), iron), H(f"upperarm_{side}") + Vector((0, 0.03 * sy, 0.02))),
               placed(shape("ball", (0.14, 0.145, 0.08), iron), H(f"upperarm_{side}") + Vector((0, 0.05 * sy, -0.09)))]
        chunk(rig, f"arm_{side}", arm, radius=0.08)
        e, w = H(f"lowerarm_{side}"), T(f"lowerarm_{side}")
        fore = [long_bone(e + Vector((0.015, 0, 0)), w + Vector((0.02, 0.0, 0)), 0.030, bone),
                long_bone(e + Vector((-0.02, 0.01 * sy, 0)), w + Vector((-0.01, 0.015 * sy, 0)), 0.024, bone),
                ]
        chunk(rig, f"elbow_{side}", fore, radius=0.04)
        fore = [placed(shape("ball", (0.035, 0.03, 0.03), bone), H(f"hand_{side}") + Vector((0, 0, -0.03)))]
        for f in ("thumb", "index", "middle", "ring", "pinky"):
            joints = [H(f"hand_{side}") + Vector((0, 0, -0.04))] + [H(f"{f}_0{k}_{side}") for k in (1, 2, 3)] + [T(f"{f}_03_{side}")]
            if f == "thumb":
                joints = joints[1:]
            for a, b in zip(joints, joints[1:]):
                fore.append(long_bone(a, b, 0.008, bone))
        chunk(rig, f"elbow_{side}", fore, radius=0.03, throw=1.4)
        thigh = [long_bone(H(f"thigh_{side}"), T(f"thigh_{side}"), 0.056, bone),
                 placed(shape("ball", (0.075, 0.08, 0.07), iron), T(f"thigh_{side}") + Vector((0.06, 0, 0.0)))]
        chunk(rig, f"leg_{side}", thigh, radius=0.06)
        k, a = H(f"calf_{side}"), T(f"calf_{side}")
        shin = [long_bone(k + Vector((0.01, 0, 0)), a + Vector((0.01, 0, 0)), 0.044, bone),
                long_bone(k + Vector((-0.03, 0.03 * sy, -0.03)), a + Vector((-0.02, 0.03 * sy, 0.01)), 0.022, bone),
                ]
        chunk(rig, f"knee_{side}", shin, radius=0.04)
        shin = [placed(shape("ball", (0.07, 0.05, 0.035), bone), H(f"foot_{side}") + Vector((-0.02, 0, -0.06)))]
        heel, ball, toe = H(f"foot_{side}") + Vector((0, 0, -0.07)), H(f"ball_{side}"), T(f"ball_{side}")
        for t in range(5):
            spread = (t - 2) * 0.026
            b1 = ball + Vector((0, spread, 0.0))
            shin.append(long_bone(heel + Vector((0.04, spread * 0.4, 0)), b1, 0.010, bone))
            shin.append(long_bone(b1, toe + Vector((-0.02 * abs(t - 1), spread * 1.1, -0.01)), 0.009, bone))
        chunk(rig, f"knee_{side}", shin, axis=(1, 0, 0), radius=0.03, throw=1.2)
    # The sword: a long rust-red blade with saw teeth along both edges.
    g = grip(rig, 0)
    hang(shape("cylinder", (0.03, 0.03, 0.13), leather, rotation=(0, math.radians(90), 0)), g, (0.02, 0, 0))
    hang(shape("ball", (0.045, 0.045, 0.045), iron), g, (-0.14, 0, 0))
    hang(shape("box", (0.03, 0.17, 0.03), iron), g, (0.17, 0, 0))
    length, width = 1.75, 0.055
    blade = hang(shape("box", (length / 2, width, 0.014), rust), g, (0.20 + length / 2, 0, 0))
    blade.modifiers.clear()
    hang(shape("cone", (0.014, width, 0.20), rust, rotation=(0, math.radians(90), 0)), g, (0.20 + length + 0.20, 0, 0))
    for k in range(11):
        x = 0.32 + k * (length - 0.15) / 10
        for side in (1, -1):
            tooth = hang(shape("cone", (0.030, 0.010, 0.050), rust, rotation=(math.radians(-90 * side), 0, 0)), g,
                         (x + 0.02 * side, side * (width + 0.03), 0))
            tooth.rotation_euler.z = math.radians(-20)
    rig.chunks.append((g, Vector((1, 0, 0)), 0.03, 2.2))
    # The shield on the left forearm: brass over a bronze rim, an iron boss.
    e, w = H("lowerarm_l"), T("lowerarm_l")
    # It faces out to the left side of the forearm, so it stays upright
    # whichever way the forearm swings.
    centre = e.lerp(w, 0.5) + Vector((0, 0.08, 0))
    out = (math.radians(-90), 0, 0)
    shield = [placed(shape("cylinder", (0.46, 0.46, 0.035), bronze, rotation=out), centre),
              placed(shape("cylinder", (0.42, 0.42, 0.03), brass, rotation=out), centre + Vector((0, 0.025, 0))),
              placed(shape("cylinder", (0.27, 0.27, 0.03), bronze, rotation=out), centre + Vector((0, 0.035, 0))),
              placed(shape("cylinder", (0.22, 0.22, 0.03), brass, rotation=out), centre + Vector((0, 0.045, 0))),
              placed(shape("ball", (0.11, 0.08, 0.11), iron), centre + Vector((0, 0.07, 0)))]
    for k in range(10):
        a = 2 * math.pi * k / 10
        shield.append(placed(shape("ball", (0.025, 0.025, 0.025), iron),
                             centre + Vector((0.35 * math.cos(a), 0.05, 0.35 * math.sin(a)))))
    chunk(rig, "elbow_l", shield, axis=(0, 0, 1), radius=0.04, throw=1.8)
    rig.poses = skeleton_poses()
    rig.animations = dict(ANIMATIONS, death=(16, False))
    return rig


def skeleton_stand(animation, s, c):
    """The skeleton waits as the original's does: nearly upright on
    slightly bent knees, the sword hanging low and forward in its right
    hand, the shield held out at its left."""
    r = {
        "spine": (0, 0.22 + 0.015 * s, 0),
        "head": (0, -0.10, 0),
        "arm_l": (0.25, -0.15, 0),
        "elbow_l": (0, -1.1, -1.1),
        "arm_r": (-0.12, -0.10 - 0.03 * s, 0),
        "elbow_r": (0, -0.40, 0),
        "weapon": (0, math.radians(-12), -0.25),
        "leg_l": (0.22, -0.40, 0.1),
        "knee_l": (0, 0.55, 0),
        "leg_r": (-0.22, 0.05, -0.1),
        "knee_r": (0, 0.45 + 0.03 * s, 0),
    }
    if animation == "walk":
        swing = 0.55
        r["spine"] = (0, 0.15, 0.06 * s)
        r["leg_l"] = (0.06, -swing * s, 0)
        r["leg_r"] = (-0.06, swing * s, 0)
        r["knee_l"] = (0, 0.15 + 0.6 * max(0.0, s), 0)
        r["knee_r"] = (0, 0.15 + 0.6 * max(0.0, -s), 0)
        # The sword is carried low, angled down across the front of the body.
        r["arm_r"] = (-0.05, -0.05 - 0.08 * s, 0)
        r["elbow_r"] = (0, -0.15, 0)
        r["weapon"] = (0, math.radians(26), 0.45)
    return r


def skeleton_poses():
    """The skeleton's animations, for pose()."""
    stand = skeleton_stand("idle", 0.0, 1.0)
    return {
        "idle": lambda s, c, k: skeleton_stand("idle", s, c),
        "walk": lambda s, c, k: skeleton_stand("walk", s, c),
        # The big swing: the sword drawn back and up over the head, then
        # brought down in front in a long lunge.
        "attack": [
            (0.0, stand),
            (0.25, hero_key(stand, spine=(0, -0.15, 0.35), head=(0, 0.1, -0.3), arm_r=(-0.30, -2.9, 0),
                            elbow_r=(0, -0.5, 0), weapon=(0, math.radians(80), 0), arm_l=(0.6, -1.0, 0))),
            (0.50, hero_key(stand, spine=(0, 0.85, -0.25), head=(0, -0.6, 0.15), arm_r=(-0.10, -1.2, 0),
                            elbow_r=(0, -0.1, 0), weapon=(0, math.radians(40), 0), arm_l=(0.7, -0.4, 0),
                            leg_l=(0.15, -1.3, 0.1), knee_l=(0, 1.4, 0), leg_r=(-0.15, 0.9, -0.1), knee_r=(0, 0.15, 0))),
            (0.75, hero_key(stand, spine=(0, 0.80, -0.2), head=(0, -0.55, 0.1), arm_r=(-0.10, -1.0, 0),
                            elbow_r=(0, -0.1, 0), weapon=(0, math.radians(45), 0), arm_l=(0.7, -0.4, 0),
                            leg_l=(0.15, -1.3, 0.1), knee_l=(0, 1.4, 0), leg_r=(-0.15, 0.9, -0.1), knee_r=(0, 0.15, 0))),
            (1.0, stand),
        ],
        "hit": [
            (0.0, stand),
            (0.35, hero_key(stand, spine=(0, -0.45, 0.2), head=(0, -0.35, 0), arm_l=(0.6, -1.6, 0),
                            arm_r=(-0.5, 0.4, 0), leg_l=(0.2, -0.6, 0.1), knee_l=(0, 0.5, 0))),
            (1.0, stand),
        ],
    }


def fall_apart(rig, k):
    """Death for a creature held together by nothing: from its stance each
    chunk (see chunk()) drops to the ground, the higher ones a little
    later, turning to lie flat and scattering, the weapons flung furthest."""
    import random
    root = rig.joints["root"]
    if not getattr(rig, "fallen", None):
        rig.pose(rig.poses["idle"](0.0, 1.0, 0.0))
        plant(rig)
        root.rotation_euler = (0, 0, 0)
        bpy.context.view_layer.update()
        lift = root.location.z
        rig.fallen = []
        rng = random.Random(7)
        for empty, axis, radius, throw in rig.chunks:
            m = empty.matrix_world.copy()
            empty.parent = root
            empty.matrix_parent_inverse = Matrix()
            a = rng.uniform(0, 2 * math.pi)
            drift = Vector((math.cos(a) * 1.2 - 0.3, math.sin(a) * 1.3, 0)) * throw * rng.uniform(0.5, 1.2)
            rig.fallen.append((empty, m, axis, radius, drift, rng.uniform(-0.6, 0.6), rng.uniform(0.0, 0.12)))
        rig.lift = lift
    root.location = (0, 0, 0)
    root.rotation_euler = (0, 0, 0)
    for empty, m, axis, radius, drift, spin, delay in rig.fallen:
        p0 = m.to_translation()
        q0 = m.to_quaternion()
        late = delay * 0.5 + 0.10 * min(1.0, p0.z / 3.0)
        u = max(0.0, min(1.0, (k - late) / 0.42))
        long = q0 @ axis
        flat = Vector((long.x, long.y, 0))
        if flat.length < 0.2:
            flat = Vector((math.cos(spin * 5), math.sin(spin * 5), 0))
        q1 = long.rotation_difference(flat.normalized()) @ q0
        q1 = Quaternion((0, 0, 1), spin) @ q1
        z = radius + (p0.z - radius) * (1 - u * u)
        if u >= 1:  # a little bounce after landing
            z += 0.05 * max(0.0, math.sin(math.pi * min(1.0, (k - late - 0.5) / 0.15)))
        p = Vector((p0.x + drift.x * u, p0.y + drift.y * u, max(radius, z)))
        empty.matrix_basis = Matrix.Translation(p) @ q0.slerp(q1, min(1.0, u * 1.2)).to_matrix().to_4x4() @ \
            Matrix.Diagonal(m.to_scale()).to_4x4()


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
SIZE.update({"skeleton": 200, "boar": 240, "boarrider": 280, "barbarian": 300, "lizard": 340})


def build_scorpion(giant=False):
    """A scorpion: a flat armoured body on eight short legs, two big
    pincers held out in front, and a segmented tail curled high over its
    back with a sting. The giant is glossy teal banded with black, with a
    dark blue sting; the small one is pale as old bone."""
    if giant:
        shell = studio.material("shell", (0.18, 0.70, 0.66), (0.01, 0.05, 0.06), scale=26, rough=0.25, bump=0.7,
                                band=(0.42, 0.60))
        dark = studio.material("shellband", (0.03, 0.06, 0.07), scale=20, rough=0.3)
        sting = studio.material("sting", (0.16, 0.10, 0.55), (0.03, 0.02, 0.15), scale=20, rough=0.3)
    else:
        shell = studio.material("shell", (0.72, 0.68, 0.56), (0.20, 0.17, 0.12), scale=26, rough=0.5, bump=0.6,
                                band=(0.40, 0.65))
        dark = studio.material("shellband", (0.25, 0.21, 0.15), scale=20, rough=0.5)
        sting = studio.material("sting", (0.35, 0.30, 0.22), scale=20, rough=0.4)
    rig = Rig()
    rig.kind = "spider"
    rig.joint("body", "root", (0, 0, 0.55))
    # Head plate and four body plates, each with a dark band behind it.
    rig.attach(shape("ball", (0.40, 0.42, 0.18), shell), "body", (0.45, 0, 0.02))
    for k in range(4):
        x = 0.15 - 0.32 * k
        rig.attach(shape("ball", (0.22, 0.46 - 0.03 * k, 0.17), shell), "body", (x, 0, 0.04))
        rig.attach(shape("ball", (0.06, 0.40 - 0.03 * k, 0.15), dark), "body", (x - 0.17, 0, 0.04))
    # Pincers on jointed arms.
    for side in (1, -1):
        segment(rig, "body", (0.6, 0.25 * side, 0), (1.05, 0.60 * side, 0.10), 0.08, shell)
        segment(rig, "body", (1.05, 0.60 * side, 0.10), (1.45, 0.40 * side, 0.15), 0.08, shell)
        rig.attach(shape("ball", (0.30, 0.14, 0.12), shell), "body", (1.65, 0.36 * side, 0.15))
        segment(rig, "body", (1.80, 0.32 * side, 0.15), (2.10, 0.22 * side, 0.16), 0.05, dark)
        segment(rig, "body", (1.80, 0.42 * side, 0.15), (2.05, 0.46 * side, 0.18), 0.04, dark)
    # The tail curls up and forward over the back.
    rig.joint("tail", "body", (-1.0, 0, 0.05))
    pts = [(-0.6 * math.cos(a) * (1.6 - a * 0.3), 0, 2.1 * math.sin(a)) for a in [i * 0.42 for i in range(8)]]
    for i in range(7):
        segment(rig, "tail", pts[i], pts[i + 1], 0.16 - 0.01 * i, shell)
        rig.attach(shape("ball", (0.18 - 0.01 * i,) * 3, dark if i % 2 else shell), "tail", pts[i + 1])
    tip = Vector(pts[-1])
    rig.attach(shape("ball", (0.16, 0.12, 0.13), sting), "tail", tuple(tip + Vector((0.12, 0, -0.02))))
    rig.attach(shape("cone", (0.05, 0.05, 0.22), sting, rotation=(0, math.radians(150), 0)), "tail",
               tuple(tip + Vector((0.30, 0, -0.15))))
    # Eight short legs, splayed out low.
    leg = shell
    rig.legs = []
    for k, angle in enumerate((60, 85, 110, 135)):
        for side in (1, -1):
            name = f"hip{k}{'l' if side > 0 else 'r'}"
            a = math.radians(angle) * side
            out_ = Vector((math.cos(a), math.sin(a), 0))
            rig.joint(name, "body", tuple(out_ * 0.35 + Vector((0.1 - 0.25 * k, 0, 0))))
            knee = out_ * 0.55 + Vector((0, 0, 0.30))
            foot = out_ * 1.05 + Vector((0, 0, -0.55))
            segment(rig, name, (0, 0, 0), knee, 0.06, leg)
            segment(rig, name, knee, foot, 0.05, dark)
            rig.legs.append((name, a, k, side))
    scale = 1.6 if giant else 0.85
    rig.joints["root"].scale = (scale, scale, scale)
    return rig


def build_fire():
    """A fire spirit: a ball of flame, a white-hot heart inside flickering
    tongues of yellow, orange and red."""
    def flame(name, colour, strength):
        m = studio.material(name, colour, tuple(c * 0.6 for c in colour), scale=8, rough=0.5)
        p = m.node_tree.nodes["Principled BSDF"]
        p.inputs["Emission Strength"].default_value = strength
        m.node_tree.links.new(p.inputs["Base Color"].links[0].from_socket, p.inputs["Emission Color"])
        return m
    core = flame("core", (1.0, 0.85, 0.45), 4.0)
    mid = flame("flame", (1.0, 0.48, 0.06), 3.0)
    rim = flame("flamerim", (0.85, 0.14, 0.02), 2.5)
    rig = Rig()
    rig.kind = "fire"
    rig.joint("body", "root", (0, 0, 1.4))
    rig.attach(shape("ball", (0.45, 0.45, 0.50), core), "body", (0, 0, 0))
    rig.attach(shape("ball", (0.62, 0.62, 0.66), mid), "body", (0, 0, 0.02))
    rig.tongues = []
    for k in range(14):
        a = 2 * math.pi * k / 14
        name = f"tongue{k}"
        rig.joint(name, "body", (0.35 * math.cos(a), 0.35 * math.sin(a), 0.1 + 0.1 * (k % 3)))
        h = 0.9 + 0.6 * ((k * 7) % 5) / 4
        rig.attach(shape("cone", (0.22, 0.22, h), rim if k % 2 == 0 else mid,
                         rotation=(-0.35 * math.sin(a), 0.35 * math.cos(a), 0)), name, (0, 0, h * 0.8))
        rig.tongues.append((name, a, k))
    rig.attach(shape("cone", (0.35, 0.35, 1.4), mid), "body", (0, 0, 1.3))
    # A lower ring of tongues licking outward hides the round heart.
    for k in range(12):
        a = 2 * math.pi * (k + 0.5) / 12
        rig.attach(shape("cone", (0.24, 0.24, 0.8), rim if k % 2 else mid,
                         rotation=(-0.6 * math.sin(a), 0.6 * math.cos(a), 0)),
                   "body", (0.5 * math.cos(a), 0.5 * math.sin(a), 0.1))
    rig.joints["root"].scale = (1.6, 1.6, 1.6)
    return rig


def fire_pose(rig, animation, t, frames):
    """The flame bobs and its tongues flicker; it flares to strike and
    gutters out when it dies."""
    k = min(1.0, t * frames / max(1, frames - 1))
    r = {}
    grow = 1.0
    for name, a, n in rig.tongues:
        flick = math.sin(2 * math.pi * (t * (2 + n % 3)) + n)
        r[name] = (0.25 * flick * math.sin(a), -0.25 * flick * math.cos(a), 0)
    offset = (0, 0, 0.12 * math.sin(2 * math.pi * t))
    if animation == "attack":
        grow = 1 + 0.4 * math.sin(math.pi * k)
        offset = (0.5 * math.sin(math.pi * k), 0, 0.1)
    elif animation == "hit":
        grow = 1 - 0.25 * math.sin(math.pi * k)
    elif animation == "death":
        grow = max(0.05, 1 - k)
        offset = (0, 0, -0.9 * k)
    rig.pose(r, offset)
    rig.joints["body"].scale = (grow, grow, grow)


def build_raptor():
    """A grey raptor: a lean beast running on two clawed legs, its body
    level, a long stiff tail behind, a narrow toothy head, small grasping
    arms and a row of short spikes and two horns on the head."""
    hide = studio.material("hide", (0.32, 0.33, 0.36), (0.04, 0.04, 0.05), scale=30, detail=8, rough=0.35, bump=0.9,
                           band=(0.40, 0.68))
    belly = studio.material("belly", (0.58, 0.58, 0.58), (0.20, 0.20, 0.21), scale=24, rough=0.5)
    spike = studio.material("spike", (0.85, 0.85, 0.86), (0.40, 0.40, 0.44), scale=30, rough=0.3)
    eye = studio.material("eye", (0.95, 0.20, 0.10), scale=60, rough=0.3)
    rig = Rig(lean=-0.35, weapon=0, stance=0.18)
    rig.joint("hips", "root", (0, 0, 1.68))
    rig.joint("spine", "hips", (0, 0, 0.0))
    rig.joint("head", "spine", (1.25, 0, 0.55))
    rig.joint("arm_l", "spine", (0.80, 0.22, 0.05))
    rig.joint("arm_r", "spine", (0.80, -0.22, 0.05))
    rig.joint("leg_l", "hips", (0, 0.26, 0))
    rig.joint("leg_r", "hips", (0, -0.26, 0))
    # A level body and a neck rising forward.
    rig.attach(shape("ball", (0.72, 0.32, 0.38), hide), "spine", (0.25, 0, 0.10))
    rig.attach(shape("ball", (0.52, 0.25, 0.24), belly), "spine", (0.30, 0, -0.08))
    segment(rig, "spine", (0.8, 0, 0.15), (1.25, 0, 0.55), 0.18, hide)
    for k in range(7):
        rig.attach(shape("cone", (0.05, 0.04, 0.16), spike, rotation=(0, math.radians(-30), 0)), "spine",
                   (0.85 - 0.25 * k, 0, 0.48 - 0.02 * abs(k - 2)))
    # Head: long jaws, teeth, eyes, two horns.
    rig.attach(shape("ball", (0.34, 0.24, 0.24), hide), "head", (0.05, 0, 0.05))
    segment(rig, "head", (0.15, 0, 0.05), (0.72, 0, -0.02), 0.15, hide)
    segment(rig, "head", (0.15, 0, -0.10), (0.66, 0, -0.16), 0.09, belly)
    for i in range(5):
        rig.attach(shape("cone", (0.02, 0.02, 0.05), spike, rotation=(math.pi, 0, 0)), "head",
                   (0.25 + 0.08 * i, 0.07, -0.06))
        rig.attach(shape("cone", (0.02, 0.02, 0.05), spike, rotation=(math.pi, 0, 0)), "head",
                   (0.25 + 0.08 * i, -0.07, -0.06))
    for side in (1, -1):
        rig.attach(shape("ball", (0.04, 0.035, 0.03), eye), "head", (0.22, 0.13 * side, 0.12))
        segment(rig, "head", (0.0, 0.10 * side, 0.18), (-0.35, 0.16 * side, 0.40), 0.04, spike)
    # A long stiff tail straight out behind.
    rig.joint("tail", "hips", (-0.45, 0, 0.10))
    pts = [(-0.34 * i, 0, -0.10 * i) for i in range(6)]
    for i in range(5):
        segment(rig, "tail", pts[i], pts[i + 1], 0.22 - 0.032 * i, hide)
        rig.attach(shape("cone", (0.04, 0.03, 0.12), spike, rotation=(0, math.radians(-40), 0)), "tail",
                   (pts[i][0], 0, pts[i][2] + 0.22 - 0.03 * i))
    # Small arms with hooked claws.
    for side in ("arm_l", "arm_r"):
        limb(rig, side, 0.45, 0.06, hide, "ball", hide, (0.07, 0.06, 0.07))
        rig.attach(shape("cone", (0.025, 0.025, 0.10), spike, rotation=(0, math.radians(150), 0)), side,
                   (0.06, 0, -0.52))
    # Powerful legs: thigh, shin and a long clawed foot.
    for side in ("leg_l", "leg_r"):
        rig.attach(shape("ball", (0.28, 0.20, 0.46), hide), side, (0.0, 0, -0.30))
        segment(rig, side, (0.05, 0, -0.60), (-0.22, 0, -1.38), 0.10, hide)
        segment(rig, side, (-0.22, 0, -1.38), (0.05, 0, -1.66), 0.07, hide)
        for c in (-1, 0, 1):
            segment(rig, side, (0.05, 0, -1.66), (0.30, 0.08 * c, -1.68), 0.035, hide)
            rig.attach(shape("cone", (0.025, 0.025, 0.07), spike, rotation=(0, math.radians(100), 0)), side,
                       (0.34, 0.08 * c, -1.68))
    rig.joints["root"].scale = (1.7, 1.7, 1.7)
    return rig


BUILDERS.update({"scorpion": lambda: build_scorpion(), "bigscorpion": lambda: build_scorpion(giant=True),
                 "fire": build_fire, "raptor": build_raptor})
HEIGHT.update({"scorpion": 2.0, "bigscorpion": 5.4, "fire": 6.4, "raptor": 6.4})
SIZE.update({"bigscorpion": 280, "raptor": 300, "fire": 200})


def build_bonebeast():
    """A winged beast of yellowed bone: a ribbed, spiked body over dark red
    flesh, a horned skull, clawed arms and legs, a spiny tail and great
    wings of long bone fingers with tattered membranes."""
    bone = studio.material("bone", (0.82, 0.76, 0.58), (0.36, 0.30, 0.18), scale=20, rough=0.55, bump=0.7)
    flesh = studio.material("flesh", (0.42, 0.10, 0.06), (0.12, 0.03, 0.02), scale=16, rough=0.5, bump=0.6)
    skin = studio.material("membrane", (0.62, 0.52, 0.36), (0.24, 0.18, 0.10), scale=4, rough=0.7, bump=0.0)
    skin.use_backface_culling = False
    rig = Rig()
    rig.joint("body", "root", (0, 0, 2.1))
    rig.joint("head", "body", (0.22, 0, 0.75))
    rig.attach(shape("ball", (0.40, 0.46, 0.58), flesh), "body", (0, 0, 0.15))
    for k in range(6):  # ribs
        rig.attach(shape("ball", (0.34, 0.40 - 0.02 * k, 0.04), bone), "body", (0.03, 0, 0.45 - 0.13 * k))
    rig.attach(shape("ball", (0.24, 0.30, 0.16), bone), "body", (0, 0, -0.45))  # pelvis
    for k in range(5):  # spines down the back
        rig.attach(shape("cone", (0.05, 0.05, 0.16), bone, rotation=(0, math.radians(-70), 0)), "body",
                   (-0.30, 0, 0.55 - 0.2 * k))
    rig.attach(shape("ball", (0.20, 0.17, 0.20), bone), "head", (0.05, 0, 0.08))
    rig.attach(shape("cone", (0.10, 0.10, 0.18), bone, rotation=(0, math.radians(100), 0)), "head", (0.28, 0, 0.0))
    for side in (1, -1):
        segment(rig, "head", (0, 0.10 * side, 0.18), (-0.25, 0.22 * side, 0.50), 0.05, bone)  # horns
        segment(rig, "head", (-0.25, 0.22 * side, 0.50), (-0.50, 0.20 * side, 0.62), 0.03, bone)
        rig.attach(shape("ball", (0.035, 0.035, 0.035), flesh), "head", (0.18, 0.07 * side, 0.12))
        name = f"leg0{'l' if side > 0 else 'r'}"
        rig.joint(name, "body", (0, 0.20 * side, -0.50))
        segment(rig, name, (0, 0, 0), (0.25, 0.05 * side, -0.60), 0.09, bone)
        segment(rig, name, (0.25, 0.05 * side, -0.60), (0.0, 0.05 * side, -1.15), 0.07, bone)
        for k in (-1, 0, 1):
            segment(rig, name, (0.0, 0.05 * side, -1.15), (0.22, (0.05 + 0.08 * k) * side, -1.30), 0.03, flesh)
        arm = f"arm{'l' if side > 0 else 'r'}"
        rig.joint(arm, "body", (0.10, 0.36 * side, 0.45))
        segment(rig, arm, (0, 0, 0), (0.35, 0.12 * side, -0.40), 0.06, bone)
        segment(rig, arm, (0.35, 0.12 * side, -0.40), (0.70, 0.10 * side, -0.55), 0.05, bone)
        for k in range(4):  # long claws
            segment(rig, arm, (0.70, 0.10 * side, -0.55), (1.05, (0.04 + 0.05 * k) * side, -0.75 + 0.05 * k),
                    0.015, bone)
        rig.attach(shape("ball", (0.14, 0.12, 0.10), bone), arm, (0, 0, 0))  # shoulder spur
        rig.attach(shape("cone", (0.04, 0.04, 0.16), bone, rotation=(math.radians(-40 * side), 0, 0)), arm,
                   (0, 0.05 * side, 0.12))
    points = [(-0.20 - 0.10 * i, 0, -0.55 - 0.16 * i + 0.012 * i * i) for i in range(9)]
    for i in range(8):
        segment(rig, "body", points[i], points[i + 1], 0.07 - 0.006 * i, bone)
        rig.attach(shape("cone", (0.03, 0.03, 0.08), bone, rotation=(0, math.radians(-60), 0)), "body",
                   (points[i][0] - 0.05, 0, points[i][2] + 0.05))
    for side in (1, -1):
        name = "wing_l" if side > 0 else "wing_r"
        rig.joint(name, "body", (-0.20, 0.25 * side, 0.55))
        spike_wing(rig, name, side, 2.6, bone)
    rig.attach(shape("ball", (0.36, 0.44, 0.30), bone), "body", (0.02, 0, 0.62))  # shoulder plates
    rig.beat, rig.wing_rest, rig.wing_depth = 12, 0.0, 0.30
    rig.joints["root"].scale = (2.0, 2.0, 2.0)
    return rig


def spike_wing(rig, joint, side, span, bone):
    """A wing that is all bone: a jointed arm and long curved spikes fanning
    back from its wrist, with no membrane between them."""
    wrist = Vector((-0.15, 0.40 * side, 0.40)) * span
    segment(rig, joint, (0, 0, 0), wrist, 0.05 * span, bone)
    for k in range(6):
        a = math.radians(70 - 22 * k)
        length = (1.0 - 0.08 * k) * span
        mid = wrist + Vector((-0.35 * math.cos(a) * 0.5, 0.45 * side, 0.6 * math.sin(a))) * length * 0.5
        tip = wrist + Vector((-0.55 * math.cos(a), 0.55 * side, 0.9 * math.sin(a))) * length
        segment(rig, joint, wrist, mid, 0.035 * span, bone)
        segment(rig, joint, mid, tip, 0.018 * span, bone)


def build_vampire():
    """A gaunt vampire with white hair and a pale face, wrapped in a long
    crimson cloak with a high collar."""
    skin = studio.material("paleskin", (0.86, 0.80, 0.78), (0.46, 0.38, 0.38), scale=5, rough=0.5, bump=0.2)
    hair = studio.material("whitehair", (0.88, 0.86, 0.86), (0.50, 0.48, 0.50), scale=30, rough=0.6, stretch=(1, 1, 5))
    cloak = studio.material("crimson", (0.42, 0.03, 0.06), (0.10, 0.005, 0.015), scale=12, rough=0.55, bump=0.5,
                            stretch=(1, 1, 4))
    dark = studio.material("darkcloth", (0.14, 0.05, 0.07), (0.04, 0.01, 0.02), scale=18, rough=0.6)
    # No glossy sheen: it turns the crimson pink.
    cloak.node_tree.nodes["Principled BSDF"].inputs["Specular IOR Level"].default_value = 0.0
    rig = Rig(lean=0.08, stance=0.06)
    rig.joint("hips", "root", (0, 0, 1.0))
    rig.joint("spine", "hips", (0, 0, 0.05))
    rig.joint("head", "spine", (0.06, 0, 1.0))
    rig.joint("arm_l", "spine", (0.0, 0.30, 0.82))
    rig.joint("arm_r", "spine", (0.0, -0.30, 0.82))
    rig.joint("leg_l", "hips", (0, 0.12, 0))
    rig.joint("leg_r", "hips", (0, -0.12, 0))
    rig.attach(shape("flare", (0.46, 0.48, 0.95), cloak), "spine", (-0.06, 0, 0.0))  # cloak skirt to the floor
    for side in (1, -1):
        rig.attach(shape("ball", (0.13, 0.15, 0.10), cloak), "spine", (-0.02, 0.30 * side, 0.92))  # padded shoulders
    rig.attach(shape("ball", (0.24, 0.32, 0.45), cloak), "spine", (0, 0, 0.55))
    rig.attach(shape("cylinder", (0.20, 0.30, 0.18), cloak, rotation=(0, math.radians(-15), 0)), "spine",
               (-0.06, 0, 0.98))  # high collar
    rig.attach(shape("ball", (0.15, 0.13, 0.17), skin), "head", (0.04, 0, 0.08))
    rig.attach(shape("ball", (0.17, 0.15, 0.16), hair), "head", (-0.04, 0, 0.14))
    rig.attach(shape("cone", (0.12, 0.10, 0.22), hair, rotation=(0, math.radians(160), 0)), "head", (-0.12, 0, -0.08))
    for side in ("arm_l", "arm_r"):
        limb(rig, side, 0.80, 0.09, cloak, "ball", skin, (0.06, 0.05, 0.07))
        rig.attach(shape("ball", (0.13, 0.12, 0.14), cloak), side, (0, 0, -0.05))
    for side in ("leg_l", "leg_r"):
        limb(rig, side, 0.95, 0.08, dark, "box", dark, (0.12, 0.06, 0.04))
    rig.arms = "out"
    rig.joints["root"].scale = (1.45, 1.45, 1.45)
    return rig


def build_bonewarrior():
    """A hunched warrior of bound bone and old leather: a horned skull, a
    great curved bone over its shoulders, bony spurs and long claws."""
    bone = studio.material("bone", (0.70, 0.62, 0.46), (0.26, 0.20, 0.12), scale=20, rough=0.55, bump=0.7)
    wrap = studio.material("wraps", (0.38, 0.24, 0.13), (0.11, 0.06, 0.03), scale=20, rough=0.7, bump=0.8,
                           stretch=(1, 1, 5))
    dark = studio.material("darkleather", (0.18, 0.12, 0.08), (0.05, 0.03, 0.02), scale=18, rough=0.6)
    rig = Rig(lean=0.35, weapon=0, stance=0.16)
    rig.joint("hips", "root", (0, 0, 1.05))
    rig.joint("spine", "hips", (0, 0, 0.05))
    rig.joint("head", "spine", (0.20, 0, 0.98))
    rig.joint("arm_l", "spine", (0.05, 0.40, 0.80))
    rig.joint("arm_r", "spine", (0.05, -0.40, 0.80))
    rig.joint("leg_l", "hips", (0, 0.16, 0))
    rig.joint("leg_r", "hips", (0, -0.16, 0))
    rig.attach(shape("ball", (0.26, 0.30, 0.36), wrap), "spine", (0.02, 0, 0.36))
    rig.attach(shape("ball", (0.30, 0.42, 0.30), wrap), "spine", (0.0, 0, 0.75))
    for k in range(4):
        rig.attach(shape("ball", (0.28, 0.34, 0.03), dark), "spine", (0.02, 0, 0.20 + 0.12 * k))  # bindings
    # Great curling horns.
    for side in (1, -1):
        pts = [(-0.05 - 0.22 * math.sin(a) * 0.6, side * (0.12 + 0.30 * (1 - math.cos(a))), 0.15 + 0.35 * math.sin(a))
               for a in [math.pi * 1.3 * i / 8 for i in range(9)]]
        for i in range(8):
            segment(rig, "head", pts[i], pts[i + 1], 0.07 - 0.006 * i, bone)
    rig.attach(shape("ball", (0.18, 0.16, 0.18), bone), "head", (0.06, 0, 0.08))
    rig.attach(shape("ball", (0.10, 0.11, 0.07), bone), "head", (0.18, 0, -0.02))
    for side in (1, -1):
        segment(rig, "head", (0, 0.10 * side, 0.16), (-0.10, 0.24 * side, 0.36), 0.04, bone)
        rig.attach(shape("ball", (0.035, 0.035, 0.035), dark), "head", (0.19, 0.06 * side, 0.10))
    for side in ("arm_l", "arm_r"):
        limb(rig, side, 0.95, 0.14, wrap, "ball", bone, (0.13, 0.10, 0.10))
        rig.attach(shape("ball", (0.17, 0.17, 0.24), bone), side, (0, 0, -0.55))
        rig.attach(shape("ball", (0.16, 0.16, 0.12), bone), side, (0, 0, 0.0))
        rig.attach(shape("cone", (0.05, 0.05, 0.16), bone), side, (-0.12, 0, 0.10))
        for k in range(3):
            segment(rig, side, (0.05, 0, -0.98), (0.20, 0.06 * (k - 1), -1.25), 0.02, bone)
    for side in ("leg_l", "leg_r"):
        limb(rig, side, 0.98, 0.11, wrap, "box", bone, (0.18, 0.10, 0.06))
        rig.attach(shape("ball", (0.12, 0.12, 0.10), bone), side, (0.05, 0, -0.50))
    rig.arms = "out"
    rig.joints["root"].scale = (2.2, 2.2, 2.2)
    return rig


def build_angel():
    """The fallen angel: a lean pale figure marked with blue sigils, golden
    armour at the shoulders and hips, long hair and two great golden wings
    of layered feathers."""
    skin = studio.material("angelskin", (0.80, 0.70, 0.58), (0.40, 0.32, 0.24), scale=6, rough=0.45, bump=0.3)
    sigil = studio.material("sigil", (0.45, 0.62, 0.95), (0.20, 0.30, 0.55), scale=8, rough=0.4)
    gold = studio.material("goldarmour", (0.92, 0.74, 0.36), (0.44, 0.30, 0.10), scale=16, rough=0.3, bump=0.4)
    feather = studio.material("feathers", (0.88, 0.72, 0.40), (0.42, 0.30, 0.12), scale=14, rough=0.5, bump=0.9,
                              stretch=(1, 6, 1))
    feather.use_backface_culling = False
    rig = Rig()
    rig.joint("body", "root", (0, 0, 2.4))
    rig.joint("head", "body", (0.06, 0, 0.78))
    rig.attach(shape("ball", (0.22, 0.30, 0.45), skin), "body", (0, 0, 0.25))
    rig.attach(shape("ball", (0.10, 0.18, 0.12), sigil), "body", (0.16, 0, 0.30))
    rig.attach(shape("cylinder", (0.26, 0.30, 0.16), gold), "body", (0, 0, -0.18))
    rig.attach(shape("flare", (0.30, 0.30, 0.30), gold), "body", (0, 0, -0.42))
    rig.attach(shape("ball", (0.13, 0.12, 0.16), skin), "head", (0.04, 0, 0.06))
    rig.attach(shape("cone", (0.15, 0.13, 0.40), gold, rotation=(0, math.radians(165), 0)), "head", (-0.12, 0, -0.10))
    for side in (1, -1):
        segment(rig, "head", (0, 0.08 * side, 0.15), (-0.20, 0.14 * side, 0.45), 0.03, gold)  # crest
        name = f"leg0{'l' if side > 0 else 'r'}"
        rig.joint(name, "body", (0, 0.12 * side, -0.45))
        segment(rig, name, (0, 0, 0), (0.10, 0.02 * side, -0.70), 0.08, skin)
        segment(rig, name, (0.10, 0.02 * side, -0.70), (-0.05, 0.02 * side, -1.30), 0.06, skin)
        rig.attach(shape("ball", (0.10, 0.09, 0.10), gold), name, (0.10, 0.02 * side, -0.70))
        arm = f"arm{'l' if side > 0 else 'r'}"
        rig.joint(arm, "body", (0.05, 0.36 * side, 0.55))
        segment(rig, arm, (0, 0, 0), (0.25, 0.10 * side, -0.45), 0.06, skin)
        segment(rig, arm, (0.25, 0.10 * side, -0.45), (0.45, 0.10 * side, -0.75), 0.05, sigil)
        rig.attach(shape("ball", (0.16, 0.14, 0.12), gold), arm, (0, 0, 0.02))
        rig.attach(shape("ball", (0.06, 0.05, 0.07), skin), arm, (0.47, 0.10 * side, -0.80))
    for side in (1, -1):
        name = "wing_l" if side > 0 else "wing_r"
        rig.joint(name, "body", (-0.15, 0.20 * side, 0.55))
        feathered_wing(rig, name, side, 2.6, gold, feather)
        wing(rig, name, side, 2.5, gold, feather)
    rig.beat, rig.wing_rest, rig.wing_depth = 12, 0.0, 0.30
    rig.joints["root"].scale = (1.8, 1.8, 1.8)
    return rig


def feathered_wing(rig, joint, side, span, bone, feather):
    """A bird's wing: an arm of gold and rows of long flat feathers fanning
    back and down from it."""
    shoulder = Vector((0, 0, 0))
    elbow = Vector((-0.15, 0.45 * side, 0.45)) * span
    tip = Vector((-0.30, 1.05 * side, 0.75)) * span
    segment(rig, joint, shoulder, elbow, 0.04 * span, bone)
    segment(rig, joint, elbow, tip, 0.03 * span, bone)
    for row, (length, width) in enumerate(((0.80, 0.22), (0.60, 0.20), (0.40, 0.18), (0.22, 0.16))):
        for k in range(18):
            f = k / 17
            base = shoulder.lerp(elbow, f * 2) if f < 0.5 else elbow.lerp(tip, (f - 0.5) * 2)
            down = Vector((-0.55 - 0.3 * f, 0.15 * side * f, -1.0 + 0.4 * f)).normalized() * length * span * (1 - 0.15 * row)
            a, b = base, base + down
            obj = shape("box", (width * span / 2, 0.01, (b - a).length / 2), feather)
            obj.rotation_euler = Vector((0, 0, 1)).rotation_difference(b - a).to_euler()
            rig.attach(obj, joint, tuple((a + b) / 2 + Vector((0.02 * row, 0, 0))))


BUILDERS.update({"bonebeast": build_bonebeast, "vampire": build_vampire, "bonewarrior": build_bonewarrior,
                 "angel": build_angel})
HEIGHT.update({"bonebeast": 12.0, "vampire": 3.7, "bonewarrior": 7.0, "angel": 11.0})
SIZE.update({"bonebeast": 420, "angel": 420, "bonewarrior": 240})


# The hero's companions (Character/PARTNER): a wolf-like dog, recoloured by
# partners.txt's tint for each kind, and a small long-bodied dog. Besides
# the usual charts they run, bite, fall (death) and jump; the death and
# jump charts have no direction (8): the third field is the direction they
# are drawn from instead (the jumps face the camera).
DOG_ANIMATIONS = {
    "idle": (12, True),
    "walk": (12, True),
    "run": (8, True),
    "hit": (6, False),
    "death": (12, False, 1),
    "bite": (10, False),
    "jumpup": (14, False, 0),
    "jumpdown": (22, False, 0),
}


def build_dog(small=False):
    """A big wolf-like dog with a dark brown-black mottled coat, a
    near-black saddle, greyish legs, belly and muzzle, pricked ears and a
    bushy tail hanging behind. Small:
    a little long-bodied dog with short legs, a long muzzle, floppy ears
    and a thin tail, in reddish tan."""
    if small:
        coat = studio.material("coat", (0.82, 0.52, 0.22), (0.48, 0.26, 0.08), scale=40, detail=8, rough=0.8, bump=0.6,
                               stretch=(1, 1, 3))
        back = studio.material("back", (0.68, 0.40, 0.15), (0.36, 0.18, 0.05), scale=40, detail=8, rough=0.8, bump=0.6)
        pale = studio.material("pale", (0.90, 0.66, 0.36), (0.60, 0.38, 0.16), scale=40, rough=0.8, bump=0.4)
    else:
        # Dark brown-black, mottled with grey-tan flecks; a near-black saddle;
        # greyish legs, belly and muzzle.
        coat = studio.material("coat", (0.40, 0.34, 0.27), (0.055, 0.045, 0.035), scale=5, detail=12, rough=0.85,
                               bump=1.4, stretch=(1, 1, 2), band=(0.48, 0.62), spot=0.22, spot_colour=(0.60, 0.54, 0.45))
        back = studio.material("back", (0.22, 0.17, 0.12), (0.03, 0.022, 0.015), scale=5, detail=12, rough=0.85,
                               bump=1.4, stretch=(1, 1, 2), band=(0.50, 0.62), spot=0.12, spot_colour=(0.40, 0.35, 0.29))
        pale = studio.material("pale", (0.74, 0.70, 0.62), (0.32, 0.28, 0.22), scale=5, detail=10, rough=0.85,
                               bump=1.0, band=(0.40, 0.58))
    nose = studio.material("nose", (0.06, 0.05, 0.05), scale=30, rough=0.3)
    eye = studio.material("eye", (0.80, 0.62, 0.25) if not small else (0.10, 0.06, 0.04), scale=60, rough=0.2)
    rig = Rig()
    rig.kind = "dog"
    rig.animations = DOG_ANIMATIONS
    leg = 0.20 if small else 0.42  # length of each of a leg's two parts
    long = 1.25 if small else 1.30  # body length
    rig.body_height = 2 * leg + 0.10
    rig.joint("body", "root", (0, 0, rig.body_height))
    # Body: chest and ribs, a slimmer waist, the dark saddle and pale belly.
    rig.attach(shape("ball", (0.60 * long, 0.27, 0.28), coat), "body", (0, 0, 0.02))
    rig.attach(shape("ball", (0.40, 0.28, 0.38), coat), "body", (0.38 * long, 0, 0.0))  # deep chest
    rig.attach(shape("ball", (0.30, 0.24, 0.26), coat), "body", (-0.42 * long, 0, 0.02))  # haunches
    rig.attach(shape("ball", (0.58 * long, 0.25, 0.17) if not small else (0.55 * long, 0.22, 0.13), back), "body",
               (-0.02, 0, 0.13 if not small else 0.15))
    rig.attach(shape("ball", (0.48 * long, 0.19, 0.16), pale), "body", (0.12, 0, -0.12))
    # Neck and head, with a lower jaw that opens to bite.
    rig.joint("neck", "body", (0.58 * long, 0, 0.12))
    if small:
        segment(rig, "neck", (0, 0, 0), (0.12, 0, 0.26), 0.14, coat)
    else:
        segment(rig, "neck", (0, 0, 0), (0.24, 0, 0.32), 0.18, coat)
        rig.attach(shape("ball", (0.20, 0.22, 0.26), pale), "neck", (0.14, 0, 0.02))  # pale ruff under the throat
        rig.attach(shape("ball", (0.20, 0.19, 0.16), back), "neck", (0.06, 0, 0.20))
    rig.joint("head", "neck", (0.16, 0, 0.32) if small else (0.28, 0, 0.36))
    rig.attach(shape("ball", (0.22, 0.19, 0.18), coat), "head", (0, 0, 0))
    rig.attach(shape("ball", (0.16, 0.15, 0.10), back), "head", (-0.02, 0, 0.07))
    snout = 0.46 if small else 0.44
    segment(rig, "head", (0.08, 0, -0.02), (snout, 0, -0.06), 0.075 if small else 0.085, pale if not small else coat)
    rig.attach(shape("ball", (0.045, 0.05, 0.04), nose), "head", (snout + 0.02, 0, -0.05))
    rig.joint("jaw", "head", (0.08, 0, -0.08))
    segment(rig, "jaw", (0, 0, 0), (snout - 0.10, 0, -0.03), 0.05, pale)
    for side in (1, -1):
        rig.attach(shape("ball", (0.035, 0.03, 0.025), eye), "head", (0.15, 0.10 * side, 0.05))
        if small:
            # Floppy ears hanging beside the head.
            rig.attach(shape("ball", (0.10, 0.03, 0.15), back, rotation=(math.radians(-15 * side), 0, 0)), "head",
                       (-0.04, 0.17 * side, -0.06))
        else:
            rig.attach(shape("cone", (0.085, 0.04, 0.15), back, rotation=(math.radians(-15 * side), 0, 0)), "head",
                       (-0.04, 0.11 * side, 0.22))
            rig.attach(shape("ball", (0.07, 0.10, 0.06), pale), "head", (0.10, 0.10 * side, -0.04))  # pale mask
    # Tail: curled up over the back (bushy, pale-tipped) or long and thin.
    rig.joint("tail", "body", (-0.58 * long, 0, 0.10))
    if small:
        pts = [(0, 0, 0), (-0.20, 0, 0.06), (-0.38, 0, 0.16), (-0.50, 0, 0.30)]
        for i in range(3):
            segment(rig, "tail", pts[i], pts[i + 1], 0.045 - 0.01 * i, coat)
    else:
        # Hanging out behind and a little down, thick, paler towards the tip.
        pts = [(0, 0, 0), (-0.20, 0, -0.06), (-0.42, 0, -0.16), (-0.62, 0, -0.22), (-0.80, 0, -0.24)]
        for i in range(4):
            segment(rig, "tail", pts[i], pts[i + 1], 0.09 + 0.01 * min(i, 2), coat if i < 2 else pale)
            rig.attach(shape("ball", (0.13, 0.11, 0.11), coat if i < 2 else pale), "tail", pts[i + 1])
    # Legs: hip or shoulder, then knee (or hock) and paw. Pale lower legs.
    rig.legs = []
    for front in (1, -1):
        for side in (1, -1):
            name = f"leg{'f' if front > 0 else 'b'}{'l' if side > 0 else 'r'}"
            x = (0.42 if front > 0 else -0.40) * long
            rig.joint(name, "body", (x, 0.15 * side, -0.10))
            if front < 0:
                rig.attach(shape("ball", (0.17, 0.11, 0.22 if not small else 0.13), coat), name, (0, 0, -0.06))
            segment(rig, name, (0, 0, 0), (0, 0, -leg), 0.085 if not small else 0.06, coat)
            rig.joint(name + "_k", name, (0, 0, -leg))
            segment(rig, name + "_k", (0, 0, 0), (0, 0, -leg), 0.06 if not small else 0.05, pale)
            rig.attach(shape("ball", (0.07, 0.05, 0.04), pale), name + "_k", (0.04, 0, -leg - 0.01))
            rig.legs.append((name, front, side))
    rig.joints["root"].scale = (1.1, 1.1, 1.1) if not small else (0.62, 0.62, 0.62)
    return rig


def dog_pose(rig, animation, t, frames):
    """Trots, gallops, flinches, falls on its side, lunges to bite and
    leaps out of sight and back down."""
    s = math.sin(2 * math.pi * t)
    c = math.cos(2 * math.pi * t)
    k = min(1.0, t * frames / max(1, frames - 1))
    r = {"tail": (0, 0, 0.25 * s), "head": (0, 0.03 * s, 0), "neck": (0, 0.05, 0)}
    offset = [0, 0, 0]
    knees = {}
    if animation == "idle":
        r["body"] = (0, 0, 0)
        offset[2] = 0.01 * s  # breathing
    elif animation == "walk":
        # A trot: diagonal pairs of legs swing together.
        for name, front, side in rig.legs:
            phase = s if front * side > 0 else -s
            r[name] = (0, 0.45 * phase, 0)
            lift = max(0.0, (c if front * side > 0 else -c))
            knees[name] = 0.7 * lift
        r["head"] = (0, 0.05 * c, 0)
        offset[2] = 0.03 * abs(s)
    elif animation == "run":
        # A gallop: front legs together, back legs half a stride later.
        b = math.sin(2 * math.pi * t + 1.6)
        for name, front, side in rig.legs:
            phase = (s if front > 0 else b) + 0.1 * side
            r[name] = (0, 0.75 * phase, 0)
            knees[name] = 0.9 * max(0.0, math.cos(2 * math.pi * t + (0 if front > 0 else 1.6)))
        r["body"] = (0, 0.12 * c, 0)
        r["tail"] = (0, 0.3, 0.1 * s)
        r["neck"] = (0, 0.25, 0)
        offset[2] = 0.10 * max(0.0, s)
    elif animation == "hit":
        jolt = math.sin(math.pi * k)
        r["body"] = (0.15 * jolt, -0.12 * jolt, 0)
        r["neck"] = (0, -0.35 * jolt, 0)
        offset[0] = -0.22 * jolt
    elif animation == "death":
        # Staggers, then drops onto its side, legs stiff.
        fall = min(1.0, k * 1.4) ** 2
        r["body"] = (math.radians(85) * fall, 0, 0)
        for name, front, side in rig.legs:
            r[name] = (0, -0.35 * front * fall, 0)
        r["neck"] = (0, 0.4 * fall, 0)
        r["tail"] = (0, 0.6 * fall, 0)
        offset[2] = -(rig.body_height - 0.25) * fall  # down until its side is on the ground
        offset[1] = 0.20 * fall
    elif animation == "bite":
        # Draws back, lunges with the jaws wide, snaps them shut.
        back = math.sin(math.pi * min(1.0, k / 0.3)) if k < 0.3 else 0.0
        lunge = math.sin(math.pi * (k - 0.3) / 0.7) if k >= 0.3 else 0.0
        offset[0] = -0.15 * back + 0.40 * lunge
        r["body"] = (0, 0.12 * lunge - 0.08 * back, 0)
        r["neck"] = (0, 0.30 * lunge - 0.15 * back, 0)
        r["jaw"] = (0, 0.6 * (lunge if k < 0.6 else max(0.0, 1 - (k - 0.6) / 0.15)), 0)
        for name, front, side in rig.legs:
            r[name] = (0, (-0.5 if front > 0 else 0.5) * lunge, 0)
    elif animation == "jumpup":
        # Crouches, then springs straight up out of sight.
        f = t * frames
        if f < 5:
            crouch = math.sin(math.pi / 2 * f / 4)
            offset[2] = -0.20 * crouch
            for name, front, side in rig.legs:
                r[name] = (0, 0.5 * front * crouch, 0)
                knees[name] = 1.1 * crouch
        else:
            u = (f - 5) / (frames - 6)
            offset[2] = 9.0 * u * u + 0.6 * u
            r["body"] = (0, -0.6, 0)
            for name, front, side in rig.legs:
                r[name] = (0, (-0.7 if front > 0 else 0.6), 0)
    elif animation == "jumpdown":
        # Drops from above, lands hard in a crouch and gets up.
        f = t * frames
        if f < 8:
            u = 1 - f / 8
            offset[2] = 9.0 * u * u
            r["body"] = (0, 0.45 * u, 0)
            for name, front, side in rig.legs:
                r[name] = (0, (-0.8 if front > 0 else 0.5) * u, 0)
        else:
            u = min(1.0, (f - 8) / (frames - 10))
            crouch = math.sin(math.pi / 2 * (1 - u)) if f >= 10 else 1.0
            offset[2] = -0.22 * crouch
            for name, front, side in rig.legs:
                r[name] = (0, 0.5 * front * crouch, 0)
                knees[name] = 1.2 * crouch
            r["neck"] = (0, 0.3 * crouch, 0)
    for name, front, side in rig.legs:
        bend = knees.get(name, 0.0)
        # Front knees fold the paw back; hocks fold the other way.
        r[name + "_k"] = (0, (bend if front > 0 else -bend), 0)
    rig.pose(r, tuple(offset))


BUILDERS.update({"dog": lambda: build_dog(), "smalldog": lambda: build_dog(small=True)})
HEIGHT.update({"dog": 2.0, "smalldog": 1.4})
SIZE.update({"dog": 360, "smalldog": 300})


# The hawk companion (Character/PARTNER/00000002, partners.txt kind 5).
# Like the original's, it is always on the wing: it hovers beside the hero,
# flies, flies flat out, flinches and stoops on its prey, and has no death
# or jump charts (the game plays its idle for those).
HAWK_ANIMATIONS = {"idle": (10, True), "walk": (10, True), "run": (10, True), "hit": (4, False), "bite": (8, False)}
HAWK_HOVER = 2.8  # height of the body above the ground


def build_hawk():
    """A dark brown hawk with barred wings and tail, a paler grey-brown
    head, a hooked horn-coloured beak and yellow feet tucked under it."""
    feathers = plaid("barred feathers", (0.28, 0.24, 0.21), (0.10, 0.08, 0.07), scale=9.0)
    dark = studio.material("dark feathers", (0.18, 0.16, 0.14), (0.06, 0.05, 0.04), scale=30, bump=0.6,
                           stretch=(4, 1, 1))
    head = studio.material("head feathers", (0.46, 0.42, 0.36), (0.20, 0.18, 0.15), scale=40, bump=0.5)
    pale = studio.material("breast", (0.44, 0.38, 0.32), (0.20, 0.16, 0.12), scale=40, bump=0.5, spot=0.3,
                           spot_colour=(0.22, 0.16, 0.12))
    beak = studio.material("beak", (0.66, 0.60, 0.44), (0.30, 0.26, 0.18), scale=40, rough=0.4)
    feet = studio.material("feet", (0.86, 0.70, 0.22), (0.50, 0.38, 0.08), scale=40, rough=0.5)
    eye = studio.material("hawk eye", (0.90, 0.62, 0.10), (0.40, 0.24, 0.02), scale=60, rough=0.2)
    rig = Rig()
    rig.joint("body", "root", (0, 0, HAWK_HOVER))
    rig.joint("head", "body", (0.30, 0, 0.06))
    rig.joint("tail", "body", (-0.30, 0, 0.0))
    rig.attach(shape("ball", (0.34, 0.14, 0.13), dark), "body", (0, 0, 0.01))
    rig.attach(shape("ball", (0.26, 0.12, 0.10), pale), "body", (0.04, 0, -0.04))
    rig.attach(shape("ball", (0.12, 0.10, 0.10), head), "head", (0.04, 0, 0.02))
    rig.attach(sharp("cone", (0.04, 0.035, 0.07), beak, rotation=(0, math.radians(110), 0)), "head", (0.17, 0, 0.0))
    for side in (1, -1):
        rig.attach(shape("ball", (0.022, 0.022, 0.022), eye), "head", (0.12, 0.06 * side, 0.05))
        rig.attach(shape("ball", (0.05, 0.03, 0.03), feet), "body", (-0.10, 0.05 * side, -0.12))
    # The tail: a fan of five long feathers.
    for k in range(5):
        a = math.radians(-24 + 12 * k)
        rig.attach(sharp("box", (0.22, 0.045, 0.012), feathers, rotation=(0, 0, a)), "tail",
                   (-0.20 * math.cos(a), -0.20 * math.sin(a), 0))
    # The wings: a broad inner wing and long fingered primaries at the tip.
    for side in (1, -1):
        name = "wing_l" if side > 0 else "wing_r"
        rig.joint(name, "body", (0.04, 0.10 * side, 0.06))
        rig.attach(shape("ball", (0.24, 0.62, 0.025), feathers), name, (-0.06, 0.58 * side, 0))
        rig.attach(shape("ball", (0.10, 0.60, 0.03), dark), name, (0.05, 0.56 * side, 0.01))  # leading edge
        for k in range(5):
            a = math.radians(side * (-6 - 9 * k))
            rig.attach(sharp("box", (0.04, 0.24, 0.01), dark, rotation=(0, 0, a)), name,
                       (-0.02 - 0.06 * k, (1.20 + 0.14 * math.cos(math.radians(9 * k))) * side, 0))
    rig.kind = "hawk"
    rig.animations = HAWK_ANIMATIONS
    return rig


def hawk_pose(rig, animation, t, frames):
    """Wings beat (twice a loop hovering, faster flying flat out, held up in
    a glide between); it bobs, flinches back when hit and stoops to the
    ground and back up to bite."""
    k = min(1.0, t * frames / max(1, frames - 1))
    beats = {"idle": 2, "walk": 2, "run": 3, "hit": 1, "bite": 1}[animation]
    phase = 2 * math.pi * t * beats
    beat = math.sin(phase)
    rest, depth = 0.55, {"idle": 0.65, "walk": 0.55, "run": 0.7}.get(animation, 0.6)
    r = {"wing_l": (rest + depth * beat, 0, 0), "wing_r": (-rest - depth * beat, 0, 0),
         "head": (0, -0.1, 0), "tail": (0, 0.05 * math.cos(phase), 0)}
    offset = (0, 0, -0.10 * beat)
    body = {"idle": 0.10, "walk": 0.0, "run": -0.12}.get(animation, 0.0)
    if animation == "hit":
        offset = (-0.35 * math.sin(math.pi * k), 0, 0.15 * math.sin(math.pi * k))
        body = -0.3 * math.sin(math.pi * k)
        r["wing_l"], r["wing_r"] = (1.0, 0, 0), (-1.0, 0, 0)
    elif animation == "bite":
        dive = math.sin(math.pi * k)
        offset = (0.7 * dive, 0, -(HAWK_HOVER - 0.6) * dive)
        body = 0.5 * dive
        r["wing_l"], r["wing_r"] = (1.1 * dive + 0.2, 0, 0), (-1.1 * dive - 0.2, 0, 0)
        r["head"] = (0, -0.1 - 0.4 * dive, 0)
    r["body"] = (0, body, 0)
    rig.pose(r, offset)


BUILDERS["hawk"] = build_hawk
HEIGHT["hawk"] = 5.0
SIZE["hawk"] = 200


def pose(rig, animation, t, frames):
    """Rotations (radians, about X, Y, Z) per joint at time t in [0, 1)."""
    s = math.sin(2 * math.pi * t)
    c = math.cos(2 * math.pi * t)
    lean = rig.lean
    if getattr(rig, "kind", None) == "hawk":
        hawk_pose(rig, animation, t, frames)
        return
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
    if getattr(rig, "kind", None) == "dog":
        dog_pose(rig, animation, t, frames)
        return
    if getattr(rig, "kind", None) == "fire":
        fire_pose(rig, animation, t, frames)
        return
    poses = getattr(rig, "poses", None)
    if poses and (animation in poses or animation == "death"):
        k = min(1.0, t * frames / max(1, frames - 1))
        if animation == "death" and getattr(rig, "chunks", None):
            fall_apart(rig, k)
            return
        p = poses[animation]
        r = keyframes(p, k) if isinstance(p, list) else p(s, c, k)
        rig.pose(r)
        if animation in getattr(rig, "two_hands", ((),))[0]:
            two_hands(rig)
        if "root" in r:
            rig.joints["root"].rotation_euler = Euler(r["root"])
            ground(rig)
        else:
            plant(rig)
        return
    r = {"spine": (0, lean, 0), "head": (0, -lean, 0), "weapon": (0, rig.weapon, 0)}
    offset = (0, 0, 0)
    if getattr(rig, "kind", None) == "hero" and animation in ("idle", "walk", "attack", "hit"):
        k = min(1.0, t * frames / max(1, frames - 1))
        if animation == "attack":
            heroine = getattr(rig, "heroine", False)
            r = keyframes(HEROINE_ATTACK if heroine else HERO_ATTACK, k)
            arms = HEROINE_SWORD_ARM if heroine else HERO_SWORD_ARM
            arm, elbow, (wrist, twist) = arms[round(k * (len(arms) - 1))]
            r.update(arm_r=arm, elbow_r=elbow, weapon=(0, math.radians(wrist), twist))
            shields = HEROINE_SHIELD_ARM if heroine else HERO_SHIELD_ARM
            r["arm_l"], r["elbow_l"] = shields[round(k * (len(shields) - 1))]
        elif animation == "hit":
            r = keyframes(HERO_HIT, k)
        elif animation == "idle" and getattr(rig, "heroine", False):
            r = heroine_stand(s)
        else:
            r = hero_guard(animation, s, c)
        rig.pose(r)
        plant(rig)
        return
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


def hero_guard(animation, s, c):
    """The hero's fighting stance, as the original's player stands and
    walks: crouched on bent knees with the feet wide apart, the shield
    held up in front of the chest and the sword out in front, low."""
    r = {
        "spine": (0, 0.18 + 0.02 * s, -0.35),
        "head": (0, -0.15, 0.30),
        "arm_l": (0.30, -1.05, 0),
        "elbow_l": (-1.45, 0.45, 0),
        "arm_r": (-0.30, -0.15 - 0.03 * s, 0),
        "elbow_r": (0, -1.10, 0),
        "weapon": (0, math.radians(45), 0),
        "leg_l": (0.32, -0.70, 0.2),
        "knee_l": (0, 0.85, 0),
        "leg_r": (-0.32, 0.15, -0.2),
        "knee_r": (0, 0.70 + 0.05 * s, 0),
    }
    if animation == "walk":
        # Upright, the shield hanging at the side, the sword low in front.
        swing = 0.6
        r["spine"] = (0, 0.10, 0.06 * s)
        r["head"] = (0, -0.08, -0.06 * s)
        r["leg_l"] = (0.06, -swing * s, 0)
        r["leg_r"] = (-0.06, swing * s, 0)
        r["knee_l"] = (0, 0.15 + 0.55 * max(0.0, s), 0)
        r["knee_r"] = (0, 0.15 + 0.55 * max(0.0, -s), 0)
        r["arm_l"] = (0.18, -0.15 + 0.25 * s, 0)
        r["elbow_l"] = (0, -0.35, 1.45)
        r["arm_r"] = (-0.12, -0.25 - 0.15 * s, 0)
        r["elbow_r"] = (0, -0.55, 0)
        r["weapon"] = (0, math.radians(70), 0)
    return r


def heroine_stand(s):
    """The heroine waits upright, as the original's does: feet a little
    apart, arms held out from her sides, the shield arm forward."""
    return {
        "spine": (0, 0.05 + 0.015 * s, 0),
        "head": (0, -0.05, 0),
        "arm_l": (0.40, -0.55, 0),
        "elbow_l": (-0.9, 0.3, 0),
        "arm_r": (-0.40, -0.10 - 0.03 * s, 0),
        "elbow_r": (0, -0.45, 0),
        "weapon": (0, math.radians(55), 0),
        "leg_l": (0.12, -0.05, 0.1),
        "knee_l": (0, 0.12, 0),
        "leg_r": (-0.12, 0.10, -0.1),
        "knee_r": (0, 0.10 + 0.02 * s, 0),
    }


def keyframes(keys, k):
    """Joint rotations at k in [0, 1], eased between (k, rotations) keys."""
    for (k0, a), (k1, b) in zip(keys, keys[1:]):
        if k <= k1:
            u = (k - k0) / (k1 - k0)
            u = u * u * (3 - 2 * u)
            return {j: tuple(x + (y - x) * u for x, y in zip(a.get(j, (0, 0, 0)), b.get(j, (0, 0, 0))))
                    for j in set(a) | set(b)}
    return dict(keys[-1][1])


def hero_key(base, **joints):
    pose = dict(base)
    pose.update(joints)
    return pose


GUARD = hero_guard("idle", 0.0, 1.0)
# The hero's attack, timed like the original's ten frames: a beat in the
# stance, the sword drawn back low behind him with the shield thrust out in
# front as he sinks onto his front knee, then swung up behind his head as he
# springs up with his head thrown back, and over into a long lunge, body flat out over the front knee,
# the sword thrust straight ahead of his middle at the target and the shield
# flung up behind.
# Arm angles past pi keep each swing going the way it travels (back and up,
# then over the top and forward) instead of through the hips.
HERO_ATTACK = [
    (0.0, GUARD),
    (0.11, GUARD),
    (0.22, hero_key(GUARD, spine=(0, 0.10, 0.45), head=(0, -0.05, -0.40), arm_r=(-0.15, 1.40, 0),
                    elbow_r=(0, -0.2, 0), weapon=(0, math.radians(135), 0), arm_l=(0.30, -1.35, 0),
                    elbow_l=(-1.2, 0.4, 0), leg_l=(0.32, -0.95, 0.2), knee_l=(0, 1.2, 0),
                    leg_r=(-0.32, 0.45, -0.2), knee_r=(0, 0.55, 0))),
    (0.33, hero_key(GUARD, spine=(0, 0.10, 0.45), head=(0, -0.05, -0.40), arm_r=(-0.15, 1.40, 0),
                    elbow_r=(0, -0.2, 0), weapon=(0, math.radians(135), 0), arm_l=(0.30, -1.35, 0),
                    elbow_l=(-1.2, 0.4, 0), leg_l=(0.32, -0.95, 0.2), knee_l=(0, 1.2, 0),
                    leg_r=(-0.32, 0.45, -0.2), knee_r=(0, 0.55, 0))),
    (0.44, hero_key(GUARD, spine=(0, -0.05, 0.30), head=(0, -0.45, -0.15), arm_r=(-0.30, 2.8, 0),
                    elbow_r=(0, -0.2, 0), weapon=(0, math.radians(105), 0), arm_l=(0.35, -1.6, 0),
                    elbow_l=(-1.0, 0.2, 0), leg_l=(0.20, -0.90, 0.1), knee_l=(0, 0.5, 0),
                    leg_r=(-0.20, 0.50, -0.1), knee_r=(0, 0.4, 0))),
    (0.556, hero_key(GUARD, spine=(0, -0.10, 0.15), head=(0, -0.40, -0.05), arm_r=(-0.25, 2.95, 0),
                    elbow_r=(0, -1.0, 0), weapon=(0, math.radians(90), 0), arm_l=(0.35, -1.8, 0),
                    elbow_l=(-1.0, 0, 0), leg_l=(0.12, -1.00, 0.1), knee_l=(0, 0.4, 0),
                    leg_r=(-0.12, 0.60, -0.1), knee_r=(0, 0.9, 0))),
    (0.67, hero_key(GUARD, spine=(0, 0.70, -0.15), head=(0, -0.55, 0.05), arm_r=(0.15, 4.18, 0),
                    elbow_r=(0, -0.2, 0), weapon=(0, math.radians(86), 0), arm_l=(0.20, -3.6, 0),
                    elbow_l=(-0.7, 0.8, 0.2), leg_l=(0.12, -1.25, 0.1), knee_l=(0, 1.3, 0),
                    leg_r=(-0.12, 0.81, -0.1), knee_r=(0, 0.25, 0))),
    (0.778, hero_key(GUARD, spine=(0, 1.35, -0.20), head=(0, -0.90, 0.10), arm_r=(0.30, 4.25, 0),
                    elbow_r=(0, 0, 0), weapon=(0, math.radians(50), 0), arm_l=(0.0, -4.2, 0),
                    elbow_l=(-0.6, 1.5, 0.5), leg_l=(0.12, -1.50, 0.1), knee_l=(0, 1.85, 0),
                    leg_r=(-0.12, 0.97, -0.1), knee_r=(0, 0.25, 0))),
    (0.889, hero_key(GUARD, spine=(0, 1.30, -0.20), head=(0, -0.85, 0.10), arm_r=(0.30, 4.22, 0),
                     elbow_r=(0, -0.05, 0), weapon=(0, math.radians(53), 0), arm_l=(0.0, -4.1, 0),
                     elbow_l=(-0.6, 1.5, 0.5), leg_l=(0.12, -1.50, 0.1), knee_l=(0, 1.85, 0),
                     leg_r=(-0.12, 0.97, -0.1), knee_r=(0, 0.25, 0))),
    # On the last frame the back knee sags down towards the ground.
    (1.0, hero_key(GUARD, spine=(0, 1.30, -0.20), head=(0, -0.85, 0.10), arm_r=(0.30, 4.22, 0),
                   elbow_r=(0, -0.05, 0), weapon=(0, math.radians(53), 0), arm_l=(0.0, -4.1, 0),
                   elbow_l=(-0.6, 1.5, 0.5), leg_l=(0.12, -1.50, 0.1), knee_l=(0, 1.85, 0),
                   leg_r=(-0.12, 0.556, -0.1), knee_r=(0, 0.9, 0))),
]
# The heroine's attack, her own like the original's, over the same ten
# frames with the blow on frame 7: from a crouch with the sword held low
# out to her side, she draws it back low behind her, raises it up behind
# her shoulder as she steps into a lunge, cuts down and across in front as
# she drops low over her front knee with the shield raised over her head,
# thrusts it straight ahead, and ends low, back leg stretched out to the
# ground, with the sword held up across her body to her left.
STAND = heroine_stand(0.0)
# Her fighting crouch at the start of the attack: knees bent, feet apart,
# leaning a little into it.
CROUCH = hero_key(STAND, spine=(0, 0.2, -0.2), head=(0, -0.15, 0.15), leg_l=(0.25, -0.6, 0.15), knee_l=(0, 0.8, 0),
                  leg_r=(-0.25, 0.25, -0.15), knee_r=(0, 0.7, 0))
LOW_LUNGE = dict(leg_l=(0.12, -1.45, 0.1), knee_l=(0, 1.5, 0), leg_r=(-0.12, 0.84, -0.1), knee_r=(0, 0.3, 0))
SHIELD_UP = dict(arm_l=(0.0, -3.5, 0), elbow_l=(-0.6, 1.5, 0.5))
HEROINE_ATTACK = [
    (0.0, hero_key(CROUCH, arm_r=(-0.4, 0.125, 0), elbow_r=(0, -0.625, 0), weapon=(0, math.radians(140), 1.1))),
    (0.111, hero_key(CROUCH, arm_r=(-0.4, 0.125, 0), elbow_r=(0, -0.625, 0), weapon=(0, math.radians(140), 1.1))),
    (0.222, hero_key(CROUCH, spine=(0, 0.25, 0.2), arm_r=(-0.2, 0.75, 0), elbow_r=(0, -0.5, 0),
                     weapon=(0, math.radians(135), 0.3))),
    (0.333, hero_key(CROUCH, spine=(0, 0.25, 0.2), arm_r=(-0.2, 0.75, 0), elbow_r=(0, -0.5, 0),
                     weapon=(0, math.radians(135), 0.3))),
    (0.444, hero_key(STAND, spine=(0, 0.3, 0.4), arm_r=(0.2, 2.375, 0), elbow_r=(0, -1.5, 0),
                     weapon=(0, math.radians(165), 0), leg_l=(0.12, -1.3, 0.1), knee_l=(0, 1.45, 0),
                     leg_r=(-0.12, 0.76, -0.1), knee_r=(0, 0.3, 0))),
    (0.556, hero_key(STAND, spine=(0, 0.3, 0.4), arm_r=(0.2, 2.375, 0), elbow_r=(0, -1.5, 0),
                     weapon=(0, math.radians(165), 0), leg_l=(0.12, -1.3, 0.1), knee_l=(0, 1.45, 0),
                     leg_r=(-0.12, 0.76, -0.1), knee_r=(0, 0.3, 0))),
    # Arm angles past pi keep the cut going over the top of the shoulder.
    (0.667, hero_key(STAND, spine=(0, 0.45, -0.2), arm_r=(0.0, 5.53, 0), elbow_r=(0, -1.25, 0),
                     weapon=(0, math.radians(125), 0.6), arm_l=(0.2, -3.0, 0), elbow_l=(-0.7, 0.8, 0.2), **LOW_LUNGE)),
    (0.778, hero_key(STAND, spine=(0, 0.5, -0.3), arm_r=(0.6, 5.03, 0), elbow_r=(0, -0.875, 0),
                     weapon=(0, math.radians(95), 1.5), **SHIELD_UP, **LOW_LUNGE)),
    (0.889, hero_key(STAND, spine=(0, 0.2, -0.1), arm_r=(0.4, 4.91, 0), elbow_r=(0, -1.5, 0),
                     weapon=(0, math.radians(30), 1.5), **SHIELD_UP, **LOW_LUNGE)),
    (1.0, hero_key(STAND, spine=(0, 0.2, -0.1), arm_r=(0.4, 4.91, 0), elbow_r=(0, -1.5, 0),
                   weapon=(0, math.radians(30), 1.5), **SHIELD_UP, **LOW_LUNGE)),
]
# The sword and shield arms in each frame of the attacks, fitted to the
# original's: tools/art/match_attack.py measures where the original's sword
# is (its hilt and the way the blade points) and where its shield is (its
# middle and the way it faces) in all eight directions of every frame,
# finds the 3D placing that best gives those pictures, and solves our
# shoulders, elbows and wrist to hold sword and shield there.
HERO_SWORD_ARM = [  # frame by frame: arm_r, elbow_r, weapon (degrees about Y, then the twist about Z)
    ((-0.4, 0.0625, -0.6), (0, 0.0, 0), (-39, -0.15)),
    ((-0.4, 0.0625, -0.6), (0, 0.0, 0), (-39, -0.15)),
    ((-0.2, 1.0, -0.8), (0, 0.0, 0), (144, 0.45)),
    ((-0.2, 1.0, -0.8), (0, 0.0, 0), (144, 0.45)),
    ((-0.2, 1.9375, -0.8), (0, 0.0, 0), (24, -0.1)),
    ((0.3, 2.9375, -0.8), (0, 0.0, 0), (-3, -0.7)),
    ((1.3, 4.5, -0.8), (0, 0.0, 0), (30, -0.25)),
    ((1.4, 4.4375, -0.8), (0, 0.0, 0), (15, -0.25)),
    ((1.4, 4.5, -0.8), (0, 0.0, 0), (18, -0.2)),
    ((1.3, 4.5, -0.8), (0, 0.0, 0), (21, -0.25)),
]
HEROINE_SWORD_ARM = [  # frame by frame: arm_r, elbow_r, weapon (degrees about Y, then the twist about Z)
    ((0.1, 0.6875, 0.0), (0, 0.0, 0), (0, -1.3)),
    ((0.1, 0.6875, 0.0), (0, 0.0, 0), (0, -1.3)),
    ((0.0, 0.8125, 0.0), (0, 0.0, 0), (165, 1.15)),
    ((0.0, 0.8125, 0.0), (0, 0.0, 0), (165, 1.15)),
    ((-0.1, 2.625, 0.0), (0, -1.5, 0), (159, -0.95)),
    ((-0.1, 2.625, 0.0), (0, -1.5, 0), (159, -0.95)),
    ((0.0, 4.375, 0.4), (0, 0.0, 0), (33, -1.25)),
    ((0.6, 4.5, 0.4), (0, 0.0, 0), (72, -0.85)),
    ((1.6, 4.5, -0.4), (0, 0.0, 0), (48, 0.8)),
    ((1.6, 4.5, -0.4), (0, 0.0, 0), (48, 0.8)),
]
HERO_SHIELD_ARM = [  # frame by frame: arm_l, elbow_l
    ((-0.35, -0.15, 0.60), (-0.40, -0.50, 0.00)),
    ((-0.35, -0.15, 0.60), (-0.40, -0.50, 0.00)),
    ((-0.05, -1.15, -0.80), (-1.20, -0.40, 0.00)),
    ((-0.05, -1.15, -0.80), (-1.20, -0.40, 0.00)),
    ((-0.10, -1.27, 0.90), (-1.70, 0.40, 0.70)),
    ((1.70, -2.40, -0.20), (-1.40, 0.80, -0.35)),
    ((1.55, -2.38, 0.20), (0.95, 0.80, -0.30)),
    ((1.20, -2.70, 1.20), (1.35, 1.20, -0.25)),
    ((1.20, -2.65, 1.20), (1.40, 1.20, -0.25)),
    ((1.10, -2.45, 1.20), (2.20, 1.20, 0.70)),
]
HEROINE_SHIELD_ARM = [  # frame by frame: arm_l, elbow_l
    ((-0.20, -0.55, 0.80), (-0.20, 0.05, 0.20)),
    ((-0.20, -0.55, 0.80), (-0.20, 0.05, 0.20)),
    ((-0.15, -0.65, 0.85), (-1.25, 0.05, 0.20)),
    ((-0.15, -0.65, 0.85), (-1.25, 0.05, 0.20)),
    ((-1.60, -0.78, 1.35), (-0.45, 0.60, 0.00)),
    ((-1.60, -0.78, 1.35), (-0.45, 0.60, 0.00)),
    ((2.00, -2.67, -0.10), (-1.90, 0.40, 0.00)),
    ((2.10, -2.70, 0.60), (-2.65, 0.40, 0.00)),
    ((-1.60, -2.75, 0.50), (-2.65, 0.20, 0.00)),
    ((-1.60, -2.75, 0.50), (-2.65, 0.20, 0.00)),
]


# Struck: doubled over, then thrown back with the shield arm flung up, and
# back into the stance.
HERO_HIT = [
    (0.0, hero_key(GUARD, spine=(0, 1.15, 0), head=(0, 0.3, 0), arm_l=(0.3, -0.6, 0), elbow_l=(-1.0, 0, 0),
                   arm_r=(-0.2, -0.9, 0), elbow_r=(0, -0.4, 0), leg_l=(0.3, -1.1, 0.2), knee_l=(0, 1.5, 0),
                   leg_r=(-0.3, -0.3, -0.2), knee_r=(0, 1.3, 0))),
    (0.35, hero_key(GUARD, spine=(0, -0.60, 0.2), head=(0, -0.40, 0), arm_l=(0.5, -2.7, 0), elbow_l=(-0.6, 0, 0),
                    arm_r=(-0.5, 0.5, 0), elbow_r=(0, -0.3, 0), leg_l=(0.3, -0.9, 0.2), knee_l=(0, 0.5, 0))),
    (1.0, GUARD),
]


def plant(rig):
    """Moves the rig down (or up) until its lowest foot stands on the ground."""
    bpy.context.view_layer.update()
    low = min((o.matrix_world @ Vector(corner)).z for name in ("knee_l", "knee_r")
              for o in rig.joints[name].children if o.type == "MESH" for corner in o.bound_box)
    rig.joints["root"].location.z -= low


def two_hands(rig):
    """Bends the left arm so its palm (joint palm_l) holds the weapon too, at
    a point along it: rig.two_hands is (animations, the weapon's grip
    empty, the point in the grip's space). Two-bone IK, the elbow out to
    the left and down."""
    _, g, point = rig.two_hands
    bpy.context.view_layer.update()
    J = rig.joints
    target = g.matrix_world @ point
    s = J["arm_l"].matrix_world.translation
    e0 = J["elbow_l"].matrix_world.translation
    p0 = J["palm_l"].matrix_world.translation
    l1, l2 = (e0 - s).length, (p0 - e0).length
    span = target - s
    d = max(abs(l1 - l2) + 1e-3, min(l1 + l2 - 1e-3, span.length))
    n = span.normalized()
    pole = J["spine"].matrix_world.to_3x3() @ Vector((-0.3, 1.0, -1.0))
    pole = (pole - pole.dot(n) * n).normalized()
    a = (l1 * l1 - l2 * l2 + d * d) / (2 * d)
    elbow = s + n * a + pole * math.sqrt(max(0.0, l1 * l1 - a * a))
    for joint, child, aim in (("arm_l", "elbow_l", elbow - s), ("elbow_l", "palm_l", target - elbow)):
        bpy.context.view_layer.update()
        parent = J[joint].parent.matrix_world.to_3x3().normalized()
        rest = rig.rest[child].normalized()
        J[joint].rotation_euler = rest.rotation_difference(parent.inverted() @ aim.normalized()).to_euler()


def ground(rig):
    """Moves the rig down (or up) until its lowest point touches the ground."""
    bpy.context.view_layer.update()
    low = min((o.matrix_world @ Vector(corner)).z for o in bpy.data.objects
              if o.type == "MESH" and not o.hide_render and o.name != "shadow catcher" for corner in o.bound_box)
    rig.joints["root"].location.z -= low


def facing(direction):
    """Blender Z rotation that makes a model facing +X look towards one of
    the engine's directions (0 = screen down, then counter-clockwise)."""
    sx, sy = math.sin(math.radians(45 * direction)), math.cos(math.radians(45 * direction))
    x = (sx / studio.PROJ[0] + sy / studio.PROJ[1]) / 2
    y = (sy / studio.PROJ[1] - sx / studio.PROJ[0]) / 2
    return math.atan2(-y, x)


def render_frames(scene, rig, kind, out, preview, skip=()):
    """Renders every animation from the eight directions into out; returns
    the frames.txt lines and calls back for each posed frame."""
    ax, ay = studio.anchor(scene)
    lines = [f"anchor {ax:.0f} {ay:.0f}"]
    for name, (frames, loop, *directional) in getattr(rig, "animations", ANIMATIONS).items():
        if kind.startswith("person") and name not in ("idle", "walk"):
            continue  # townspeople only stand about and stroll
        if name in skip:
            continue
        lines.append(f"animation {name} {frames} {'loop' if loop else 'once'}")
        if name in getattr(rig, "strikes", {}):
            lines.append(f"strike {name} {rig.strikes[name]}")  # the frame the blow lands on
        # An animation with no direction is drawn once, from the direction
        # it names, as direction 8.
        for d in ([8] if directional else range(8)):
            for f in ([frames // 2] if preview else range(frames)):
                pose(rig, name, f / frames, frames)
                spin = rig.joints["root"].rotation_euler
                rig.joints["root"].rotation_euler = Euler((spin.x, spin.y, facing(directional[0] if directional else d)))
                yield lines, name, d, f
        print(name, "done", flush=True)


def depth(scene, objects):
    """Distance from the camera plane to the middle of some objects."""
    bpy.context.view_layer.update()
    cam = scene.camera.matrix_world
    forward = cam.to_3x3() @ Vector((0, 0, -1))
    points = [o.matrix_world @ Vector(c) for o in objects for c in o.bound_box]
    middle = sum(points, Vector()) / len(points)
    return (middle - cam.translation).dot(forward)


def render_hero(scene, rig, out, preview, layers, only=None):
    """The hero's body (layer 0, into out) and each piece of equipment
    (into out/layerNN). An equipment layer is rendered with the body held
    out, so it shows only where it is in front of the body, and with every
    other piece hidden. Its frames.txt gives each frame a draw priority
    from how far the piece is from the camera compared with the chest
    (the body is 20, lower is nearer)."""
    body = [o for o in bpy.data.objects if o.type == "MESH" and o.name != "shadow catcher"]
    catcher = bpy.data.objects["shadow catcher"]
    gear = {layer: build_gear(rig, layer) for layer in sorted(GEAR)}
    chest = [o for o in body if o.parent is rig.joints["spine"]][:1]
    for layer in layers:
        folder = out if layer == 0 else out / f"layer{layer:02d}"
        folder.mkdir(parents=True, exist_ok=True)
        for o in body:
            o.is_holdout = layer != 0
            # Held out of an equipment layer, the body must not darken it or
            # the ground either, by shadow or by bounced light.
            for ray in ("visible_shadow", "visible_diffuse", "visible_glossy", "visible_transmission"):
                setattr(o, ray, layer == 0)
        for n, objects in gear.items():
            for o in objects:
                o.hide_render = n != layer
        catcher.hide_render = layer != 0 and GEAR[layer][2]
        priorities = []
        lines = []
        skip = [name for name in ANIMATIONS if (only and name not in only) or (layer and name == "death")]
        for lines, name, d, f in render_frames(scene, rig, "hero", folder, preview, skip):
            if layer:
                delta = depth(scene, gear[layer]) - depth(scene, chest)
                p = 10 if GEAR[layer][2] else max(1, min(19, 9 + round(delta / 0.2)))
                priorities.append(f"priority {name} {d} {f} {p}")
            studio.render(scene, str(folder / f"{name}_{d}_{f}.png"))
        (folder / "frames.txt").write_text("\n".join(lines + priorities) + "\n")


def main():
    preview = "--preview" in sys.argv
    blend = "--blend" in sys.argv
    layers = None
    if "--layers" in sys.argv:
        k = sys.argv.index("--layers")
        layers = [int(x) for x in sys.argv[k + 1].split(",")]
        del sys.argv[k:k + 2]
    only = None
    if "--only" in sys.argv:
        k = sys.argv.index("--only")
        only = sys.argv[k + 1].split(",")
        del sys.argv[k:k + 2]
    args = [a for a in sys.argv if a not in ("--preview", "--blend")]
    kind, out = args[-2], Path(args[-1])
    out.mkdir(parents=True, exist_ok=True)
    scene = studio.reset(samples=24)
    rig = BUILDERS[kind]()
    height = HEIGHT[kind]
    size = SIZE.get(kind, 160)
    studio.camera(scene, size, size, target=(0, 0, height * 0.42))
    if blend:
        # The model in its rest pose; frames are posed from code.
        studio.save_blend(out / f"{kind}.blend")
    if kind in ("hero", "heroine"):
        render_hero(scene, rig, out, preview, layers or [0, *sorted(GEAR)], only)
        return
    lines = []
    animations = getattr(rig, "animations", ANIMATIONS)
    skip = [name for name in animations if only and name not in only]
    for lines, name, d, f in render_frames(scene, rig, kind, out, preview, skip):
        studio.render(scene, str(out / f"{name}_{d}_{f}.png"))
    (out / "frames.txt").write_text("\n".join(lines) + "\n")


if __name__ == "__main__":
    main()
