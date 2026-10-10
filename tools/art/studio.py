"""Shared Blender set-up for rendering sprites in ShadowFlare's style.

Run inside Blender's Python (the `bpy` module). The camera is orthographic
and set to the original's 15/10 projection, so a model one Blender unit
wide covers 100 world units in the game. Models stand at the origin facing
Blender +X; the sprite's anchor is where the origin lands in the image.

Game world axes map to Blender as x -> +X, y -> -Y, up -> +Z.
"""

import math

import bpy
from bpy_extras.object_utils import world_to_camera_view
from mathutils import Vector

PROJ = (15, 10)
ELEVATION = math.asin(PROJ[1] / PROJ[0])        # camera pitch above the ground
PIXELS_PER_UNIT = PROJ[0] / math.cos(math.radians(45))  # screen px per Blender unit along a ground axis


def reset(samples=24):
    bpy.ops.wm.read_factory_settings(use_empty=True)
    scene = bpy.context.scene
    scene.render.engine = "CYCLES"
    scene.cycles.device = "CPU"
    scene.cycles.samples = samples
    scene.cycles.use_denoising = False
    scene.render.film_transparent = True
    scene.render.image_settings.file_format = "PNG"
    scene.render.image_settings.color_mode = "RGBA"
    scene.view_settings.view_transform = "Standard"
    world = bpy.data.worlds.new("world")
    world.use_nodes = True
    world.node_tree.nodes["Background"].inputs[0].default_value = (0.42, 0.44, 0.48, 1)
    world.node_tree.nodes["Background"].inputs[1].default_value = 0.95
    scene.world = world
    # Key light from the top left of the screen (world -x), warm.
    sun = bpy.data.lights.new("sun", "SUN")
    sun.energy = 2.6
    sun.color = (1.0, 0.95, 0.85)
    sun.angle = math.radians(8)
    sun_obj = bpy.data.objects.new("sun", sun)
    scene.collection.objects.link(sun_obj)
    sun_obj.rotation_euler = (math.radians(22), 0, math.radians(-90 - 20))
    # Invisible ground that only keeps the shadows.
    bpy.ops.mesh.primitive_plane_add(size=60)
    ground = bpy.context.object
    ground.name = "shadow catcher"
    ground.is_shadow_catcher = True
    return scene


def camera(scene, width, height, target=(0, 0, 0)):
    """An orthographic camera with the game's angle, centred on target."""
    cam = bpy.data.cameras.new("camera")
    cam.type = "ORTHO"
    cam.ortho_scale = max(width, height) / PIXELS_PER_UNIT
    obj = bpy.data.objects.new("camera", cam)
    scene.collection.objects.link(obj)
    scene.camera = obj
    scene.render.resolution_x = width
    scene.render.resolution_y = height
    # Looking from the screen's bottom (world +x +y, Blender +X -Y) down at the target.
    d = 40
    direction = Vector((math.cos(ELEVATION) * math.cos(math.radians(-45)),
                        math.cos(ELEVATION) * math.sin(math.radians(-45)),
                        math.sin(ELEVATION)))
    obj.location = Vector(target) + direction * d
    obj.rotation_euler = (math.pi / 2 - ELEVATION, 0, math.radians(45))
    return obj


def anchor(scene, point=(0, 0, 0)):
    """Pixel position of a 3D point in the rendered image."""
    bpy.context.view_layer.update()
    co = world_to_camera_view(scene, scene.camera, Vector(point))
    return co.x * scene.render.resolution_x, (1 - co.y) * scene.render.resolution_y


def render(scene, path):
    scene.render.filepath = path
    bpy.ops.render.render(write_still=True)


def linear(c):
    """sRGB 0-1 colour to Blender's linear colour space."""
    return tuple(x / 12.92 if x <= 0.04045 else ((x + 0.055) / 1.055) ** 2.4 for x in c)


def material(name, colour, dark=None, scale=6.0, detail=6.0, rough=0.85, bump=0.3, spot=0.0, spot_colour=None):
    """A matte material whose colour wobbles with noise, like the
    textured models of late-90s pre-rendered games. Colours are sRGB."""
    colour = linear(colour)
    dark = linear(dark) if dark else None
    spot_colour = linear(spot_colour) if spot_colour else None
    mat = bpy.data.materials.new(name)
    mat.use_nodes = True
    nodes, links = mat.node_tree.nodes, mat.node_tree.links
    bsdf = nodes["Principled BSDF"]
    bsdf.inputs["Roughness"].default_value = rough
    noise = nodes.new("ShaderNodeTexNoise")
    noise.inputs["Scale"].default_value = scale
    noise.inputs["Detail"].default_value = detail
    ramp = nodes.new("ShaderNodeValToRGB")
    ramp.color_ramp.elements[0].position = 0.3
    ramp.color_ramp.elements[0].color = (*(dark or [c * 0.55 for c in colour]), 1)
    ramp.color_ramp.elements[1].position = 0.75
    ramp.color_ramp.elements[1].color = (*colour, 1)
    links.new(noise.outputs["Fac"], ramp.inputs["Fac"])
    out = ramp.outputs["Color"]
    if spot > 0:
        # Sparse blotches of a second colour (moss, rust, spots).
        vor = nodes.new("ShaderNodeTexNoise")
        vor.inputs["Scale"].default_value = scale * 0.6
        ramp2 = nodes.new("ShaderNodeValToRGB")
        ramp2.color_ramp.elements[0].position = 1 - spot
        ramp2.color_ramp.elements[1].position = min(1.0, 1 - spot + 0.08)
        links.new(vor.outputs["Fac"], ramp2.inputs["Fac"])
        mix = nodes.new("ShaderNodeMix")
        mix.data_type = "RGBA"
        mix.inputs["B"].default_value = (*spot_colour, 1)
        links.new(ramp2.outputs["Color"], mix.inputs["Factor"])
        links.new(out, mix.inputs["A"])
        out = mix.outputs["Result"]
    links.new(out, bsdf.inputs["Base Color"])
    if bump > 0:
        b = nodes.new("ShaderNodeBump")
        b.inputs["Strength"].default_value = bump
        links.new(noise.outputs["Fac"], b.inputs["Height"])
        links.new(b.outputs["Normal"], bsdf.inputs["Normal"])
    return mat


def assign(obj, mat):
    obj.data.materials.clear()
    obj.data.materials.append(mat)
    return obj


def smooth(obj):
    for p in obj.data.polygons:
        p.use_smooth = True
    return obj


def displace(obj, strength, size, seed=0):
    tex = bpy.data.textures.new(f"disp{seed}", "CLOUDS")
    tex.noise_scale = size
    tex.noise_depth = 2
    mod = obj.modifiers.new("displace", "DISPLACE")
    mod.texture = tex
    mod.strength = strength
    mod.texture_coords = "GLOBAL"
    # Different seeds sample different parts of the noise.
    obj.location.x += 0  # keep the object where it is
    mod.texture_coords_object = None
    tex.noise_basis = "BLENDER_ORIGINAL"
    if seed:
        empty = bpy.data.objects.new(f"noise{seed}", None)
        bpy.context.scene.collection.objects.link(empty)
        empty.location = (seed * 7.3, seed * 3.1, seed * 1.7)
        mod.texture_coords = "OBJECT"
        mod.texture_coords_object = empty
    return mod
