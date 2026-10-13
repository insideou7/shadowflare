"""Animated characters in ShadowFlare's style, rendered with studio.py.

    python tools/art/creatures.py [--preview] [--blend] [--only idle,walk] <kind> <output folder>

where kind is goblin, hero, heroine, fly, ghoul, shaman, golem, spider, bigspider,
swordsman, archer, ogre, demon, dragon, boar, boarrider, skeleton,
barbarian, lizard, scorpion, bigscorpion, fire, raptor, dog, smalldog or person<N> (a townsperson from the
PEOPLE table).

(run with a Python that has the `bpy` module). Characters are built from
simple shapes hung on joints and posed in code (the hero and the goblin are
MakeHuman bodies driven by the same joints: see mpfb_body.py, which needs
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
from mathutils import Euler, Vector

sys.path.insert(0, str(Path(__file__).resolve().parent))
import studio  # noqa: E402
from town import mesh  # noqa: E402

SIZE = {"hero": 200, "heroine": 200, "golem": 300, "shaman": 200, "bigspider": 200, "ogre": 220, "demon": 300, "dragon": 400}  # rendered frame size in pixels, 160 unless listed
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
    """The goblin on a MakeHuman body (mpfb_body.py): short, lean and
    knotted with muscle, green-skinned with big pointed ears and a jutting
    jaw, in a spiked dark leather vest, an iron cap with a crest of spikes,
    bracers and a loincloth, swinging a spiked club. It fights crouched,
    like the hero, so it uses the hero's poses."""
    import mpfb_body
    skin = studio.material("goblin skin", (0.46, 0.60, 0.22), (0.24, 0.34, 0.10), scale=30, bump=0.15)
    vest = studio.material("vest", (0.20, 0.18, 0.17), (0.06, 0.05, 0.05), scale=25, rough=0.6, bump=0.4)
    cloth = studio.material("loincloth", (0.16, 0.13, 0.11), (0.05, 0.04, 0.03), scale=18, bump=0.3)
    iron = studio.material("iron", (0.66, 0.66, 0.68), (0.24, 0.24, 0.27), scale=30, rough=0.35, bump=0.3, grime=0.4)
    wood = studio.material("club", (0.50, 0.28, 0.12), (0.22, 0.11, 0.04), scale=10, stretch=(12, 1, 1))
    height = 3.1
    ears = []
    for side in ("l", "r"):
        ears += [(f"ears/{side}-ear-shape-pointed.target.gz", 1.0), (f"ears/{side}-ear-scale-incr.target.gz", 1.6),
                 (f"ears/{side}-ear-wing-incr.target.gz", 1.0), (f"ears/{side}-ear-rot-backward.target.gz", 0.8),
                 (f"ears/{side}-ear-trans-up.target.gz", 0.6)]
    body, armature = mpfb_body.human(height, dict(gender=1.0, age=0.4, muscle=1.0, weight=0.55, height=0.0,
                                                   proportions=0.1, caucasian=1.0, african=0.0, asian=0.0),
                                     ears + [("chin/chin-prognathism-incr.target.gz", 1.0),
                                             ("chin/chin-width-incr.target.gz", 0.6),
                                             ("nose/nose-scale-vert-incr.target.gz", 0.8),
                                             ("nose/nose-point-down.target.gz", 0.6),
                                             ("eyebrows/eyebrows-angle-down.target.gz", 1.0),
                                             ("head/head-scale-horiz-incr.target.gz", 0.4)])
    mpfb_body.bake_rest(body, armature, arm_out=0.25, elbow_forward=0.25)
    mpfb_body.paint(body, lambda centre, bones, normal: skin)
    rig = Rig()
    scale = 1.4
    mpfb_body.bind(rig, body, armature, scale)
    rig.kind = "hero"
    rig.goblin = True
    rig.height = height

    def pick(centre, part, normal):
        z = centre.z / height
        if part == "torso":
            return (vest, 0.08) if z >= 0.555 else (cloth, 0.07)
        if part == "thigh" and z > 0.42:
            return cloth, 0.06
        if part == "forearm":
            return iron, 0.04
        if part == "head" and (z > 0.935 or (z > 0.89 and normal.y > 0.2)):
            return iron, 0.035
        return None

    mpfb_body.shell(body, pick, "goblin gear")
    # Spikes up the back of the vest and a crest along the cap.
    neck = rig.joints["head"].matrix_world.translation.z
    top = (height - neck) / scale
    for k in range(4):
        rig.attach(shape("cone", (0.06, 0.06, 0.18), iron, rotation=(0, math.radians(-110), 0)), "spine",
                   (-0.26, (k % 2 - 0.5) * 0.26, 0.28 + 0.10 * k))
    for k in range(5):
        t = k / 4 - 0.5
        rig.attach(shape("cone", (0.035, 0.03, 0.10), iron, rotation=(0, math.radians(-25 + 70 * t), 0)), "head",
                   (0.02 - 0.22 * t, 0, top - 0.02 - abs(t) * 0.10))
    # A club with an iron-studded head, held like the hero's sword.
    g = grip(rig, 0)
    hang(shape("cylinder", (0.06, 0.06, 0.50), wood, rotation=(0, math.radians(90), 0)), g, (0.32, 0, 0))
    hang(shape("ball", (0.26, 0.21, 0.21), wood), g, (0.98, 0, 0))
    hang(shape("cylinder", (0.22, 0.22, 0.04), iron, rotation=(0, math.radians(90), 0)), g, (0.80, 0, 0))
    for k in range(10):
        a = 2 * math.pi * k / 10
        hang(shape("cone", (0.04, 0.04, 0.12), iron, rotation=(a, 0, 0)), g,
             (0.92 + 0.12 * (k % 2), -0.24 * math.sin(a), 0.24 * math.cos(a)))
    hang(shape("cone", (0.04, 0.04, 0.12), iron, rotation=(0, math.radians(90), 0)), g, (1.30, 0, 0))
    return rig


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


# layer: (name, builder, armour). Armour casts no shadow of its own: the
# body's shadow, drawn on layer 0, already covers it.
GEAR = {
    5: ("cloth", gear_cloth, True),
    6: ("plate", gear_plate, True),
    9: ("round_shield", gear_round_shield, False),
    12: ("short_sword", gear_short_sword, False),
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
    if getattr(rig, "kind", None) == "dog":
        dog_pose(rig, animation, t, frames)
        return
    if getattr(rig, "kind", None) == "fire":
        fire_pose(rig, animation, t, frames)
        return
    r = {"spine": (0, lean, 0), "head": (0, -lean, 0), "weapon": (0, rig.weapon, 0)}
    offset = (0, 0, 0)
    if getattr(rig, "kind", None) == "hero" and animation in ("idle", "walk", "attack", "hit"):
        k = min(1.0, t * frames / max(1, frames - 1))
        if animation == "attack":
            r = keyframes(GOBLIN_ATTACK if getattr(rig, "goblin", False) else HERO_ATTACK, k)
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
# The goblin's club swing: drawn back with the shield arm raised, swung up
# over the head, then down and forward in a deep lunge.
GOBLIN_ATTACK = [
    (0.0, GUARD),
    (0.25, hero_key(GUARD, spine=(0, 0.15, 0.40), head=(0, -0.10, -0.35), arm_r=(-0.45, 1.5, 0),
                    elbow_r=(0, 0, 0), weapon=(0, math.radians(90), 0), arm_l=(0.45, -2.6, 0), elbow_l=(-0.5, 0, 0))),
    (0.5, hero_key(GUARD, spine=(0, -0.05, 0.25), head=(0, 0.0, -0.20), arm_r=(-0.20, -2.9, 0),
                   elbow_r=(0, -1.1, 0), weapon=(0, math.radians(90), 0), arm_l=(0.45, -2.7, 0), elbow_l=(-0.5, 0, 0))),
    (0.72, hero_key(GUARD, spine=(0, 0.95, -0.10), head=(0, -0.80, 0.05), arm_r=(-0.10, -1.5, 0),
                    elbow_r=(0, -0.1, 0), weapon=(0, math.radians(60), 0), arm_l=(0.45, -2.5, 0),
                    elbow_l=(-0.5, 0, 0), leg_l=(0.12, -1.35, 0.1), knee_l=(0, 1.55, 0),
                    leg_r=(-0.12, 1.05, -0.1), knee_r=(0, 0.10, 0))),
    (1.0, hero_key(GUARD, spine=(0, 0.95, -0.10), head=(0, -0.80, 0.05), arm_r=(-0.10, -1.4, 0),
                   elbow_r=(0, -0.1, 0), weapon=(0, math.radians(65), 0), arm_l=(0.45, -2.4, 0),
                   elbow_l=(-0.5, 0, 0), leg_l=(0.12, -1.35, 0.1), knee_l=(0, 1.55, 0),
                   leg_r=(-0.12, 1.05, -0.1), knee_r=(0, 0.10, 0))),
]
# The hero's attack, timed like the original's ten frames: a beat in the
# stance, the sword drawn back low behind him with the shield thrust out in
# front as he sinks onto his front knee, then swung up behind his head as he
# springs up, and over into a long lunge, body flat out over the front knee,
# the sword thrust straight at the target and the shield flung up behind.
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
    (0.44, hero_key(GUARD, spine=(0, -0.05, 0.30), head=(0, 0.0, -0.25), arm_r=(-0.30, 2.8, 0),
                    elbow_r=(0, -0.2, 0), weapon=(0, math.radians(105), 0), arm_l=(0.35, -1.6, 0),
                    elbow_l=(-1.0, 0.2, 0), leg_l=(0.20, -0.90, 0.1), knee_l=(0, 0.5, 0),
                    leg_r=(-0.20, 0.50, -0.1), knee_r=(0, 0.4, 0))),
    (0.556, hero_key(GUARD, spine=(0, -0.10, 0.15), head=(0, 0.05, -0.10), arm_r=(-0.25, 2.95, 0),
                    elbow_r=(0, -1.0, 0), weapon=(0, math.radians(90), 0), arm_l=(0.35, -1.8, 0),
                    elbow_l=(-1.0, 0, 0), leg_l=(0.12, -1.00, 0.1), knee_l=(0, 0.4, 0),
                    leg_r=(-0.12, 0.60, -0.1), knee_r=(0, 0.9, 0))),
    (0.67, hero_key(GUARD, spine=(0, 0.70, -0.15), head=(0, -0.55, 0.05), arm_r=(-0.15, 4.18, 0),
                    elbow_r=(0, -0.2, 0), weapon=(0, math.radians(86), 0), arm_l=(0.20, -3.6, 0),
                    elbow_l=(-0.7, 0.8, 0.2), leg_l=(0.12, -1.25, 0.1), knee_l=(0, 1.3, 0),
                    leg_r=(-0.12, 1.00, -0.1), knee_r=(0, 0.2, 0))),
    (0.778, hero_key(GUARD, spine=(0, 1.20, -0.20), head=(0, -0.90, 0.10), arm_r=(-0.10, 4.33, 0),
                    elbow_r=(0, 0, 0), weapon=(0, math.radians(55), 0), arm_l=(0.0, -4.2, 0),
                    elbow_l=(-0.6, 1.5, 0.5), leg_l=(0.12, -1.40, 0.1), knee_l=(0, 1.6, 0),
                    leg_r=(-0.12, 1.25, -0.1), knee_r=(0, 0.05, 0))),
    (1.0, hero_key(GUARD, spine=(0, 1.15, -0.20), head=(0, -0.85, 0.10), arm_r=(-0.10, 4.30, 0),
                   elbow_r=(0, -0.05, 0), weapon=(0, math.radians(58), 0), arm_l=(0.0, -4.1, 0),
                   elbow_l=(-0.6, 1.5, 0.5), leg_l=(0.12, -1.40, 0.1), knee_l=(0, 1.6, 0),
                   leg_r=(-0.12, 1.25, -0.1), knee_r=(0, 0.05, 0))),
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
