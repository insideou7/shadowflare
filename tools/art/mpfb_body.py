"""Characters on a MakeHuman body (MPFB 2, CC0), driven by creatures.py's rig.

MPFB's base mesh, modelling targets, game-engine rig and weights are CC0
(see LICENSE.ASSETS.md in the MPFB repository), so what we render from them
can go into the MIT content pack. MPFB itself is GPL code that we only run,
like Blender; nothing of it is copied into the repository.

Set-up, once:

    git clone --depth 1 https://github.com/makehumancommunity/mpfb2.git
    export MPFB_SRC=$PWD/mpfb2/src/mpfb

The body is skinned to MPFB's rig. Each rig bone we pose is tied to the
matching joint empty of creatures.Rig (Copy Transforms from an empty on it), so the pose
code, plant(), facing() and the equipment layers work as they do for the
models made of simple shapes.
"""

import math
import os
from pathlib import Path

import addon_utils
import bpy
from mathutils import Matrix, Vector

EXTENSION = "bl_ext.user_default.mpfb"
EYES = ("helper-l-eye", "helper-r-eye")

# creatures.Rig joint: (MPFB bones it drives, bone whose head is the joint)
JOINTS = {
    "hips": (("pelvis",), "pelvis"),
    "spine": (("spine_01",), "spine_01"),
    "head": (("neck_01", "head"), "neck_01"),
    "arm_l": (("upperarm_l",), "upperarm_l"),
    "elbow_l": (("lowerarm_l",), "lowerarm_l"),
    "arm_r": (("upperarm_r",), "upperarm_r"),
    "elbow_r": (("lowerarm_r",), "lowerarm_r"),
    "leg_l": (("thigh_l",), "thigh_l"),
    "knee_l": (("calf_l",), "calf_l"),
    "leg_r": (("thigh_r",), "thigh_r"),
    "knee_r": (("calf_r",), "calf_r"),
    # Ankles turn the feet (and the toes with them), so a walking foot can
    # stand flat, strike with its heel and push off from its ball.
    "ankle_l": (("foot_l",), "foot_l"),
    "ankle_r": (("foot_r",), "foot_r"),
}
PARENT = {"hips": "root", "spine": "hips", "head": "spine", "arm_l": "spine", "arm_r": "spine",
          "elbow_l": "arm_l", "elbow_r": "arm_r", "leg_l": "hips", "leg_r": "hips", "knee_l": "leg_l",
          "knee_r": "leg_r", "ankle_l": "knee_l", "ankle_r": "knee_r"}


def enable():
    """Loads MPFB as a Blender extension from $MPFB_SRC."""
    if EXTENSION in bpy.context.preferences.addons:
        return
    src = os.environ.get("MPFB_SRC")
    if not src or not Path(src, "blender_manifest.toml").exists():
        raise SystemExit("set MPFB_SRC to mpfb2/src/mpfb (see tools/art/mpfb_body.py)")
    folder = Path(bpy.utils.user_resource("EXTENSIONS"), "user_default")
    folder.mkdir(parents=True, exist_ok=True)
    link = folder / "mpfb"
    if not link.exists():
        link.symlink_to(Path(src).resolve())
    addon_utils.enable(EXTENSION, default_set=True)


def services():
    import importlib
    hs = importlib.import_module(EXTENSION + ".services.humanservice").HumanService
    ts = importlib.import_module(EXTENSION + ".services.targetservice").TargetService
    props = importlib.import_module(EXTENSION + ".entities.objectproperties").HumanObjectProperties
    loc = importlib.import_module(EXTENSION + ".services.locationservice").LocationService
    return hs, ts, props, loc


def human(height, macros, targets=()):
    """An MPFB human `height` Blender units tall, with the game-engine rig.
    macros are MPFB's macro sliders (gender, age, muscle, weight, height,
    proportions, african/asian/caucasian); targets are (path under MPFB's
    data/targets, weight)."""
    enable()
    hs, ts, props, loc = services()
    body = hs.create_human(scale=0.1)
    for key, value in macros.items():
        props.set_value(key, value, entity_reference=body)
    ts.reapply_macro_details(body)
    for path, weight in targets:
        ts.load_target(body, os.path.join(loc.get_mpfb_data("targets"), path), weight=weight)
    # MPFB shows only the "body" group; show the eyeballs too.
    for mod in body.modifiers:
        if mod.type == "MASK":
            keep = {body.vertex_groups[n].index for n in (mod.vertex_group, *EYES)}
            shown = body.vertex_groups.new(name="body and eyes")
            shown.add([v.index for v in body.data.vertices if any(g.group in keep and g.weight > 0 for g in v.groups)],
                      1.0, "REPLACE")
            mod.vertex_group = shown.name
    armature = hs.add_builtin_rig(body, "game_engine")
    armature["height"] = height
    return body, armature


def _set_bone(armature, name, direction):
    """Turns a pose bone (in armature space) to point along direction."""
    pb = armature.pose.bones[name]
    bpy.context.view_layer.update()
    m = pb.matrix.copy()
    head = m.to_translation()
    current = (m.to_3x3() @ Vector((0, 1, 0))).normalized()
    turn = current.rotation_difference(Vector(direction).normalized()).to_matrix().to_4x4()
    pb.matrix = Matrix.Translation(head) @ turn @ Matrix.Translation(-head) @ m
    bpy.context.view_layer.update()


def bake_rest(body, armature, arm_out=0.14, arm_forward=0.0, elbow_forward=0.12):
    """Brings the arms down from MPFB's A pose so the rest pose is what
    creatures.Rig expects (arms hanging at the sides), and bakes the
    modelling targets and that pose into the mesh."""
    for side, s in (("l", 1), ("r", -1)):
        _set_bone(armature, f"upperarm_{side}", (s * arm_out, -arm_forward, -1))
        _set_bone(armature, f"lowerarm_{side}", (s * arm_out * 0.6, -elbow_forward, -1))
        _set_bone(armature, f"hand_{side}", (s * arm_out * 0.4, -elbow_forward, -1))
    deps = bpy.context.evaluated_depsgraph_get()
    mesh = bpy.data.meshes.new_from_object(body.evaluated_get(deps), preserve_all_data_layers=True, depsgraph=deps)
    body.modifiers.clear()
    body.shape_key_clear()
    old = body.data
    body.data = mesh
    bpy.data.meshes.remove(old)
    bpy.context.view_layer.objects.active = armature
    with bpy.context.temp_override(active_object=armature, object=armature, selected_objects=[armature]):
        bpy.ops.object.mode_set(mode="POSE")
        bpy.ops.pose.armature_apply(selected=False)
        bpy.ops.object.mode_set(mode="OBJECT")
    mod = body.modifiers.new("Armature", "ARMATURE")
    mod.object = armature
    bpy.context.view_layer.update()
    k = armature["height"] / max((body.matrix_world @ v.co).z for v in body.data.vertices)
    armature.scale = (k, k, k)
    bpy.context.view_layer.update()


def dominant_bone(body):
    """For each vertex, the name of the bone that moves it most."""
    armature = body.find_armature()
    names = {g.index: g.name for g in body.vertex_groups if g.name in armature.data.bones}
    out = []
    for v in body.data.vertices:
        best = max((g for g in v.groups if g.group in names), key=lambda g: g.weight, default=None)
        out.append(names[best.group] if best else "")
    return out


def paint(body, pick, eye=None):
    """Gives each face the material pick(centre, bones of its vertices,
    normal) returns; centre and normal are in the body's rest pose, in
    world units, front towards -Y. The eyeballs get `eye` (a dark brown
    unless given). Materials are added to the mesh as they are first
    returned."""
    import studio
    eye = eye or studio.material("eye", (0.16, 0.10, 0.07), (0.05, 0.03, 0.02), scale=60, rough=0.2, bump=0)
    eyes = {g.index for g in body.vertex_groups if g.name in EYES}
    in_eye = [any(g.group in eyes and g.weight > 0 for g in v.groups) for v in body.data.vertices]
    bones = dominant_bone(body)
    mw = body.matrix_world
    slots = {}
    for poly in body.data.polygons:
        centre = mw @ poly.center
        normal = (mw.to_3x3() @ poly.normal).normalized()
        if all(in_eye[i] for i in poly.vertices):
            mat = eye
        else:
            mat = pick(centre, [bones[i] for i in poly.vertices], normal)
        if mat.name not in slots:
            body.data.materials.append(mat)
            slots[mat.name] = len(body.data.materials) - 1
        poly.material_index = slots[mat.name]
    for poly in body.data.polygons:
        poly.use_smooth = True


def bind(rig, body, armature, scale):
    """Places rig's joints (made by creatures.Rig, root only) on the
    armature's bones and ties those bones to them. The model then faces
    +X like every other creature. scale is the rig root's scale."""
    armature.rotation_euler = (0, 0, math.radians(90))
    bpy.context.view_layer.update()
    aw = armature.matrix_world

    def head(bone):
        return aw @ armature.data.bones[bone].head_local

    world = {"root": Vector((0, 0, 0))}
    for joint in JOINTS:
        world[joint] = head(JOINTS[joint][1])
    for joint in JOINTS:
        rel = (world[joint] - world[PARENT[joint]]) / scale
        rig.joint(joint, PARENT[joint], tuple(rel))
    for side in ("l", "r"):
        rig.splits[f"arm_{side}"] = (f"elbow_{side}", rig.rest[f"elbow_{side}"].z)
        rig.splits[f"leg_{side}"] = (f"knee_{side}", rig.rest[f"knee_{side}"].z)
    # The weapon joint sits in the palm of the right hand.
    palm = (head("hand_r") + head("middle_01_r")) / 2
    rig.joint("weapon", "elbow_r", tuple((palm - world["elbow_r"]) / scale))
    rig.joints["root"].scale = (scale, scale, scale)
    bpy.context.view_layer.update()
    for joint, (bones, _) in JOINTS.items():
        for bone in bones:
            # An empty on the joint where the bone rests; the bone copies
            # its world transform, so it does not also inherit its parent's.
            follow = bpy.data.objects.new(f"bone {bone}", None)
            bpy.context.scene.collection.objects.link(follow)
            follow.parent = rig.joints[joint]
            follow.matrix_world = aw @ armature.pose.bones[bone].matrix
            con = armature.pose.bones[bone].constraints.new("COPY_TRANSFORMS")
            con.target = follow
    bpy.context.view_layer.update()
    # Hidden markers that plant() and the equipment draw priority read:
    # the soles under the knees and the chest on the spine.
    for side in ("l", "r"):
        sole = (head(f"ball_{side}") + head(f"foot_{side}")) / 2
        sole.z = min((body.matrix_world @ v.co).z for v in body.data.vertices)
        marker(rig, f"knee_{side}", sole - world[f"knee_{side}"], scale)
        # Where the sole's heel and ball touch the ground, from the ankle,
        # for the walk's foot planting (creatures.gait).
        ball = head(f"ball_{side}")
        ankle = world[f"ankle_{side}"]
        heel = Vector((ankle.x - 0.35 * (ball.x - ankle.x), ankle.y, sole.z))
        rig.feet = getattr(rig, "feet", {})
        rig.feet[side] = ((heel - ankle) / scale, (Vector((ball.x, ball.y, sole.z)) - ankle) / scale)
    marker(rig, "spine", (head("spine_03") + head("neck_01")) / 2 - world["spine"], scale)


def marker(rig, joint, offset, scale):
    bpy.ops.mesh.primitive_cube_add(size=0.02)
    o = bpy.context.object
    o.name = f"marker {joint}"
    o.hide_render = True
    o.parent = rig.joints[joint]
    o.location = Vector(offset) / scale
    return o


def rest_frame(body):
    """Maps the body's world coordinates back to its rest frame (front
    towards -Y), whichever way bind() has turned it."""
    armature = body.find_armature()
    return Matrix.Rotation(-armature.rotation_euler.z, 4, "Z") @ body.matrix_world


def part(bones):
    """What a face belongs to, from the bones that move its vertices:
    head, neck, torso, upperarm, forearm, hand, thigh, shin or foot."""
    kinds = []
    for b in bones:
        if b.startswith(("upperarm", "clavicle")):
            kinds.append("upperarm" if b.startswith("upperarm") else "torso")
        elif b.startswith("lowerarm"):
            kinds.append("forearm")
        elif b.startswith(("hand", "thumb", "index", "middle", "ring", "pinky")):
            kinds.append("hand")
        elif b.startswith("thigh"):
            kinds.append("thigh")
        elif b.startswith("calf"):
            kinds.append("shin")
        elif b.startswith(("foot", "ball")):
            kinds.append("foot")
        elif b == "head":
            kinds.append("head")
        elif b == "neck_01":
            kinds.append("neck")
        else:
            kinds.append("torso")
    return max(set(kinds), key=kinds.count)


def shell(body, pick, name="shell"):
    """Clothing or armour made from the body itself: the faces for which
    pick(centre, part, normal) returns (material, thickness), pushed out
    along their normals and skinned to the same rig. Returns the new mesh
    object, or None if pick chose nothing."""
    import bmesh
    bones = dominant_bone(body)
    m = rest_frame(body)
    rot = m.to_3x3()
    obj = body.copy()
    obj.data = body.data.copy()
    obj.name = name
    obj.modifiers.clear()
    for mat in list(obj.data.materials):
        obj.data.materials.pop()
    bpy.context.scene.collection.objects.link(obj)
    bm = bmesh.new()
    bm.from_mesh(obj.data)
    bm.normal_update()
    bm.faces.ensure_lookup_table()
    slots, drop = {}, []
    push = {}
    for f in bm.faces:
        centre = m @ f.calc_center_median()
        normal = (rot @ f.normal).normalized()
        chosen = pick(centre, part([bones[v.index] for v in f.verts]), normal)
        if not chosen:
            drop.append(f)
            continue
        mat, thick = chosen
        if mat.name not in slots:
            obj.data.materials.append(mat)
            slots[mat.name] = len(obj.data.materials) - 1
        f.material_index = slots[mat.name]
        for v in f.verts:
            push[v.index] = max(push.get(v.index, 0.0), thick)
    if len(drop) == len(bm.faces):
        bm.free()
        bpy.data.objects.remove(obj)
        return None
    # Thickness is in world units; the mesh is scaled with its armature.
    k = body.matrix_world.to_scale().z
    for v in bm.verts:
        if v.index in push:
            v.co += v.normal * push[v.index] / k
    bmesh.ops.delete(bm, geom=drop, context="FACES")
    bm.to_mesh(obj.data)
    bm.free()
    for poly in obj.data.polygons:
        poly.use_smooth = True
    mod = obj.modifiers.new("Armature", "ARMATURE")
    mod.object = body.find_armature()
    return obj
