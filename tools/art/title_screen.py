#!/usr/bin/env python3
"""The title screen's picture for the open content pack.

    python3.13 tools/art/title_screen.py <content pack folder> [logo font]

Draws System/Title/Pattern/Title, pattern 0 of the original's Title.njp:
the whole 640 x 480 screen behind the title menu (src/game/title.cpp).
Dark blue-black stone with a pale mist in the middle, old copper pipes
running round the edges, and the name in glowing blue letters with a
large S and F.

Our own art, made from scratch in the original's manner: the stone and the
pipes are a Blender scene (the `bpy` module, `pip install bpy`), seen
straight on and lit from the top left so the pipes throw soft shadows on
the stone; the mist and the name are painted afterwards with numpy and
Pillow. The name is set in New Rocker (SIL Open Font License, from
github.com/google/fonts, ofl/newrocker); pass its path, or put it in
tools/art/fonts/. shrink.py then makes the picture 256 colours.
"""

import math
import random
import sys
import tempfile
from pathlib import Path

import numpy as np
from PIL import Image, ImageDraw, ImageFilter, ImageFont

sys.path.insert(0, str(Path(__file__).resolve().parent))
import shrink  # noqa: E402
from ui import write_sheet  # noqa: E402

W, H = 640, 480
FONTS = [Path(__file__).resolve().parent / "fonts/NewRocker-Regular.ttf",
         Path("/usr/share/fonts/truetype/newrocker/NewRocker-Regular.ttf")]

# The pipes, as runs of screen points (x, y); a run bends round a corner at
# each inner point. Each run: (radius in pixels, depth, points). Depth puts
# a run in front of (smaller) or behind (larger) the runs it crosses.
PIPES = [
    # Left side.
    (4, 0.0, [(14, 490), (14, 318), (58, 318), (58, 132), (-12, 132)]),
    (3, 0.02, [(26, 490), (26, 330), (70, 330), (70, 120), (-12, 120)]),
    (3, 0.1, [(-12, 196), (34, 196), (34, 250), (128, 250), (128, 282), (222, 282)]),
    (3, 0.12, [(-12, 208), (22, 208), (22, 262), (116, 262), (116, 304), (150, 304)]),
    (3, 0.2, [(36, -12), (36, 64), (102, 64), (102, 112), (156, 112)]),
    (5, 0.05, [(84, 490), (84, 400), (40, 400), (40, 272)]),
    (3, 0.15, [(-12, 438), (120, 438), (120, 404), (168, 404), (168, 490)]),
    (3, 0.16, [(-12, 450), (132, 450), (132, 416), (156, 416), (156, 490)]),
    (3, 0.25, [(96, 490), (96, 472), (122, 472)]),
    (3, 0.12, [(-12, 30), (70, 30), (70, 8), (232, 8), (232, -12)]),
    (3, 0.3, [(150, -12), (150, 22), (188, 22), (188, 40)]),
    (2, 0.32, [(-12, 90), (18, 90), (18, 108)]),
    (3, 0.08, [(190, 490), (190, 470), (240, 470), (240, 490)]),
    # Right side.
    (4, 0.0, [(626, -12), (626, 184), (586, 184), (586, 352), (652, 352)]),
    (3, 0.02, [(614, -12), (614, 172), (574, 172), (574, 364), (652, 364)]),
    (3, 0.1, [(560, -12), (560, 40), (600, 40), (600, 140), (652, 140)]),
    (5, 0.05, [(652, 238), (612, 238), (612, 322), (512, 322), (512, 490)]),
    (3, 0.18, [(468, -12), (468, 26), (520, 26), (520, 80)]),
    (3, 0.19, [(480, -12), (480, 14), (532, 14), (532, 60)]),
    (3, 0.12, [(652, 410), (580, 410), (580, 452), (452, 452), (452, 490)]),
    (3, 0.13, [(652, 422), (592, 422), (592, 463), (420, 463)]),
    (3, 0.22, [(630, 490), (630, 440)]),
    (3, 0.2, [(482, 490), (482, 380), (456, 380), (456, 344)]),
    (3, 0.28, [(398, -12), (398, 14), (440, 14)]),
    (3, 0.15, [(652, 66), (634, 66)]),
    (2, 0.3, [(652, 270), (628, 270), (628, 282)]),
    # Short branches off the runs above, ending in the mouths the title's
    # steam blows from (MOUTHS).
    (3, 0.04, [(586, 208), (535, 208)]),
    (3, 0.04, [(600, 60), (562, 60)]),
    (3, 0.24, [(36, 54), (62, 54)]),
    (3, 0.04, [(574, 350), (547, 350)]),
    (3, 0.04, [(26, 377), (53, 377)]),
    (3, 0.04, [(586, 294), (566, 294)]),
]

# Where the steam comes out (src/logic/title_smoke.h, the original's places
# for its jets): these run ends are left open, with a nozzle, not capped.
# The jets at (122, 472) and (113, 473) share the one mouth.
MOUTHS = {(222, 282), (535, 208), (122, 472), (562, 60), (62, 54), (547, 350), (53, 377), (566, 294), (420, 463)}

# Where a run ends inside the screen it is closed with a cap; couplings sit
# along the straight lengths about this far apart, in pixels.
COUPLING_STEP = 90
# The radii above, in pixels, are multiplied by this.
PIPE_SCALE = 1.4
# How fine the stone's grain is, in bumps per Blender unit (100 pixels).
STONE_GRAIN = 9.0
# The name: letter sizes, the most width it may take, and its top.
# The L's long foot leaves a gap before the A: pull it in.
KERN = {"L": -16}
LOGO_SMALL, LOGO_BIG, LOGO_WIDTH, LOGO_TOP, LOGO_NARROW = 100, 190, 540, 62, 0.8


def run_blender(target, pipes_only):
    """Renders the stone and the pipes, straight on, to target, and the
    pipes alone on nothing to pipes_only (to tell them from the stone)."""
    import bpy

    import studio

    scene = studio.reset(samples=48)
    scene.render.film_transparent = False
    for obj in list(bpy.data.objects):
        if obj.name == "shadow catcher":
            bpy.data.objects.remove(obj)
    world = scene.world.node_tree.nodes["Background"]
    world.inputs[0].default_value = (0.05, 0.06, 0.09, 1)
    world.inputs[1].default_value = 0.35
    sun = bpy.data.objects["sun"]
    # From the top left and in front, low enough that the pipes cast short
    # shadows down and to the right on the stone.
    sun.rotation_euler = (math.radians(50), math.radians(-35), 0)
    sun.data.energy = 1.8
    sun.data.angle = math.radians(14)

    cam = bpy.data.cameras.new("camera")
    cam.type = "ORTHO"
    cam.ortho_scale = W / 100
    cam_obj = bpy.data.objects.new("camera", cam)
    scene.collection.objects.link(cam_obj)
    scene.camera = cam_obj
    cam_obj.location = (0, -20, 0)
    cam_obj.rotation_euler = (math.pi / 2, 0, 0)
    scene.render.resolution_x, scene.render.resolution_y = W, H

    def at(x, y, depth=0.0):
        return ((x - W / 2) / 100, depth, (H / 2 - y) / 100)

    # A cold blue light skimming the stone from the top left picks out its
    # ridges, as on the original's screen.
    rim = bpy.data.lights.new("rim", "SUN")
    rim.energy = 13.0
    rim.color = (0.2, 0.55, 1.0)
    rim.angle = math.radians(4)
    rim_obj = bpy.data.objects.new("rim", rim)
    scene.collection.objects.link(rim_obj)
    rim_obj.rotation_euler = (math.radians(14), math.radians(-40), 0)

    # The stone: a slab just behind the pipes, its face broken up by two
    # layers of noise, fine grain on coarse lumps.
    stone = studio.material("stone", (0.06, 0.07, 0.12), dark=(0.008, 0.009, 0.018), scale=2.0, detail=6,
                            rough=0.6, bump=0.0, band=(0.3, 0.75), grime=0.7)
    bpy.ops.mesh.primitive_grid_add(x_subdivisions=480, y_subdivisions=360, size=1,
                                    location=(0, 0.45, 0), rotation=(math.pi / 2, 0, 0))
    slab = bpy.context.object
    slab.scale = (W / 100 + 1, H / 100 + 1, 1)
    bpy.ops.object.transform_apply(scale=True)
    for i, (size, depth, strength) in enumerate(((0.05, 4, 0.035), (0.35, 3, 0.09))):
        tex = bpy.data.textures.new(f"stone{i}", "CLOUDS")
        tex.noise_scale = size
        tex.noise_depth = depth
        tex.noise_basis = "VORONOI_F2_F1" if i == 0 else "BLENDER_ORIGINAL"
        mod = slab.modifiers.new(f"stone{i}", "DISPLACE")
        mod.texture = tex
        mod.strength = strength
        mod.texture_coords = "GLOBAL"
    studio.assign(slab, stone)

    copper = studio.material("copper", (0.52, 0.32, 0.20), dark=(0.12, 0.06, 0.035), scale=40, detail=10,
                             rough=0.38, bump=0.5, spot=0.3, spot_colour=(0.22, 0.13, 0.08), grime=0.55)
    copper.node_tree.nodes["Principled BSDF"].inputs["Metallic"].default_value = 0.65
    iron = studio.material("iron", (0.24, 0.20, 0.17), dark=(0.05, 0.04, 0.03), scale=50, rough=0.55, bump=0.4)

    def cylinder(a, b, r, mat):
        ax, ay, az = a
        bx, by, bz = b
        length = math.dist(a, b)
        if length < 1e-6:
            return
        bpy.ops.mesh.primitive_cylinder_add(vertices=20, radius=r, depth=length,
                                            location=((ax + bx) / 2, (ay + by) / 2, (az + bz) / 2))
        obj = bpy.context.object
        dx, dz = bx - ax, bz - az
        obj.rotation_euler = (0, math.atan2(dx, dz), 0)
        studio.smooth(obj)
        studio.assign(obj, mat)

    def sphere(p, r, mat):
        bpy.ops.mesh.primitive_uv_sphere_add(segments=20, ring_count=10, radius=r, location=p)
        studio.smooth(studio.assign(bpy.context.object, mat))

    rng = random.Random(7)
    for radius, depth, points in PIPES:
        radius *= PIPE_SCALE
        r = radius / 100
        bend = max(2.4 * radius, 10)
        # Round the corners: each inner point becomes a quarter circle.
        path = [points[0]]
        corners = []
        for i in range(1, len(points) - 1):
            (x0, y0), (x1, y1), (x2, y2) = points[i - 1], points[i], points[i + 1]
            d0 = math.dist((x0, y0), (x1, y1))
            d1 = math.dist((x1, y1), (x2, y2))
            k = min(bend, d0 / 2, d1 / 2)
            u = ((x1 - x0) / d0, (y1 - y0) / d0)
            v = ((x2 - x1) / d1, (y2 - y1) / d1)
            p, q = (x1 - u[0] * k, y1 - u[1] * k), (x1 + v[0] * k, y1 + v[1] * k)
            path.append(p)
            for s in range(1, 8):
                t = s / 8
                # A quadratic curve through the corner, close to a circle.
                path.append(((1 - t) ** 2 * p[0] + 2 * t * (1 - t) * x1 + t * t * q[0],
                             (1 - t) ** 2 * p[1] + 2 * t * (1 - t) * y1 + t * t * q[1]))
            path.append(q)
            corners.append((p, q))
        path.append(points[-1])
        curve = bpy.data.curves.new("pipe", "CURVE")
        curve.dimensions = "3D"
        curve.bevel_depth = r
        curve.bevel_resolution = 6
        curve.use_fill_caps = True
        spline = curve.splines.new("POLY")
        spline.points.add(len(path) - 1)
        for sp, (x, y) in zip(spline.points, path):
            sp.co = (*at(x, y, depth), 1)
        obj = bpy.data.objects.new("pipe", curve)
        scene.collection.objects.link(obj)
        curve.materials.append(copper)
        # Couplings: a ring each side of every bend, and along the lengths.
        legs = []
        for i in range(len(points) - 1):
            a, b = points[i], points[i + 1]
            if i > 0:
                a = corners[i - 1][1]
            if i < len(points) - 2:
                b = corners[i][0]
            legs.append((a, b))
        for li, ((x0, y0), (x1, y1)) in enumerate(legs):
            length = math.dist((x0, y0), (x1, y1))
            if length < 1:
                continue
            ux, uy = (x1 - x0) / length, (y1 - y0) / length
            ring_r, ring_w = r * 1.45, max(radius * 0.9, 4)
            marks = []
            if li > 0:
                marks.append(ring_w / 2)
            if li < len(legs) - 1:
                marks.append(length - ring_w / 2)
            n = int(length // COUPLING_STEP)
            for k in range(1, n + 1):
                s = k * length / (n + 1) + rng.uniform(-10, 10)
                if all(abs(s - m) > 18 for m in marks):
                    marks.append(s)
            for s in marks:
                cx, cy = x0 + ux * s, y0 + uy * s
                a = at(cx - ux * ring_w / 2, cy - uy * ring_w / 2, depth)
                b = at(cx + ux * ring_w / 2, cy + uy * ring_w / 2, depth)
                cylinder(a, b, ring_r, copper if rng.random() < 0.6 else iron)
        # Ends inside the screen get a cap.
        for (x, y), (px, py) in ((points[0], points[1]), (points[-1], points[-2])):
            if 0 <= x <= W and 0 <= y <= H:
                d = math.dist((x, y), (px, py))
                ux, uy = (x - px) / d, (y - py) / d
                if (x, y) in MOUTHS:
                    # A short flared nozzle, open at the end.
                    cylinder(at(x - ux * 7, y - uy * 7, depth), at(x, y, depth), r * 1.45, iron)
                    cylinder(at(x - ux * 2, y - uy * 2, depth), at(x, y, depth), r * 1.7, iron)
                    continue
                cylinder(at(x - ux * 3, y - uy * 3, depth), at(x + ux * 3, y + uy * 3, depth), r * 1.6, iron)
                sphere(at(x + ux * 3, y + uy * 3, depth), r * 1.1, iron)

    studio.render(scene, str(target))
    slab.hide_render = True
    scene.render.film_transparent = True
    scene.cycles.samples = 8
    studio.render(scene, str(pipes_only))


def font(size, path=None):
    for f in ([Path(path)] if path else []) + FONTS:
        if f.exists():
            return ImageFont.truetype(str(f), size)
    sys.exit("title_screen.py: no logo font; pass the path to NewRocker-Regular.ttf")


def blur(a, radius):
    im = Image.fromarray(np.clip(a * 255, 0, 255).astype(np.uint8))
    return np.asarray(im.filter(ImageFilter.GaussianBlur(radius)), dtype=float) / 255


def mist():
    """A faint cloud of blue in the middle of the screen, behind the name."""
    rng = np.random.default_rng(3)
    field = np.zeros((H, W))
    for scale, weight in ((80, 0.5), (32, 0.3), (12, 0.2)):
        small = rng.random((H // scale + 2, W // scale + 2))
        im = Image.fromarray((small * 255).astype(np.uint8)).resize(((W // scale + 2) * scale, (H // scale + 2) * scale),
                                                                      Image.BICUBIC)
        field += weight * np.asarray(im, dtype=float)[:H, :W] / 255
    return np.clip((field - 0.3) * 1.6, 0, 1)


def centre(sx, sy):
    gy, gx = np.mgrid[0:H, 0:W].astype(float)
    return np.exp(-(((gx - W / 2) / sx) ** 2 + ((gy - H * 0.47) / sy) ** 2))


def logo(font_path=None):
    """The name, as an RGBA layer the size of the screen: SHADOW and FLARE
    in pale glowing blue, the S hanging below the line and the F rising
    above it."""
    big, small = font(LOGO_BIG, font_path), font(LOGO_SMALL, font_path)
    canvas = Image.new("L", (W * 2, H))  # room for the name before it is fitted
    d = ImageDraw.Draw(canvas)
    base = 260  # the baseline of the small letters
    x = 20
    parts = [("S", big, 50, -14), ("HADOW", small, 0, -5), ("F", big, 0, -40), ("LARE", small, 0, -5)]
    for text, f, drop, after in parts:
        for ch in text:
            left, top, right, bottom = d.textbbox((0, 0), ch, font=f, anchor="ls")
            d.text((x - left, base + drop), ch, font=f, fill=255, anchor="ls")
            x += right - left + after + KERN.get(ch, 0)
    m = np.asarray(canvas, dtype=float) / 255
    cols = np.where(m.max(axis=0) > 0)[0]
    m = m[:, cols[0]:cols[-1] + 1]
    # Narrow the letters a little, so they stand tall like the original's.
    im = Image.fromarray((m * 255).astype(np.uint8))
    m = np.asarray(im.resize((round(m.shape[1] * LOGO_NARROW), H), Image.LANCZOS), dtype=float) / 255
    # Fit it to the width the original's name takes, and centre it.
    if m.shape[1] > LOGO_WIDTH:
        im = Image.fromarray((m * 255).astype(np.uint8))
        im = im.resize((LOGO_WIDTH, round(H * LOGO_WIDTH / m.shape[1])), Image.LANCZOS)
        m = np.asarray(im, dtype=float) / 255
    rows = np.where(m.max(axis=1) > 0)[0]
    m = m[rows[0]:rows[-1] + 1]
    full = np.zeros((H, W))
    height = m.shape[0]
    x0, y0 = (W - m.shape[1]) // 2, LOGO_TOP
    full[y0:y0 + m.shape[0], x0:x0 + m.shape[1]] = m
    m = full
    gy = np.mgrid[0:H, 0:W][0].astype(float)
    t = np.clip((gy - y0) / height, 0, 1)
    # Near white at the top, through pale blue to a deep blue and violet.
    stops = [(0.0, (240, 244, 255)), (0.35, (170, 190, 255)), (0.7, (90, 105, 235)), (1.0, (80, 50, 200))]
    fill = np.zeros((H, W, 3))
    for (t0, c0), (t1, c1) in zip(stops, stops[1:]):
        k = np.clip((t - t0) / (t1 - t0), 0, 1)[..., None]
        inside = ((t >= t0) & (t <= t1))[..., None]
        fill = np.where(inside, np.array(c0) + (np.array(c1) - np.array(c0)) * k, fill)
    # A bevel: lit edges up and to the left, dark down and to the right.
    soft = blur(m, 1.6)
    gyy, gxx = np.gradient(soft)
    light = np.clip(-(gxx * 0.7 + gyy * 0.9) * 7, -1, 1)
    fill = fill * (1 + 0.4 * np.minimum(light, 0)[..., None]) + 90 * np.maximum(light, 0)[..., None]
    # Inside the strokes, a darker blue core so the letters read as solid.
    core = np.clip(blur(m, 3.0) * 1.4 - 0.6, 0, 1)
    fill = fill * (1 - 0.25 * core[..., None])
    # A thin dark outline, then a soft blue glow round the name.
    outline = np.clip(blur(m, 1.0) * 3, 0, 1)
    glow = np.clip(blur(m, 6) * 0.9 + blur(m, 18) * 0.5, 0, 1)
    rgb = np.zeros((H, W, 3)) + np.array([50, 90, 240])
    alpha = glow * 0.55
    rgb = rgb * (1 - outline[..., None]) + np.array([12, 12, 40]) * outline[..., None]
    alpha = np.maximum(alpha, outline)
    rgb = rgb * (1 - m[..., None]) + np.clip(fill, 0, 255) * m[..., None]
    alpha = np.maximum(alpha, m)
    return rgb, alpha


def compose(stone_pipes, pipes_only, font_path=None):
    base = np.asarray(Image.open(stone_pipes).convert("RGB"), dtype=float)
    stone = 1 - np.asarray(Image.open(pipes_only).convert("RGBA"), dtype=float)[..., 3:] / 255
    # Deepen the stone so only its ridges catch the blue light, and let the
    # middle of the screen fall away into darkness with a little mist.
    deep = 255 * (base / 255) ** 2.7 * 2.4
    dark = 1 - 0.85 * centre(240, 160)[..., None]
    haze = (mist() * centre(230, 150))[..., None] * np.array([18, 30, 60])
    base = base * 0.85 * (1 - stone) + (deep * dark + haze) * stone
    rgb, alpha = logo(font_path)
    out = base * (1 - alpha[..., None]) + rgb * alpha[..., None]
    return Image.fromarray(np.clip(out, 0, 255).astype(np.uint8))


def main():
    out = Path(sys.argv[1])
    font_path = sys.argv[2] if len(sys.argv) > 2 else None
    with tempfile.TemporaryDirectory() as tmp:
        layer, pipes = Path(tmp) / "stone_pipes.png", Path(tmp) / "pipes.png"
        run_blender(layer, pipes)
        picture = compose(layer, pipes, font_path)
    folder = out / "System/Title/Pattern"
    write_sheet(folder, "Title", picture, [(0, 0, W, H, 0, 0)],
                "The title screen behind the menu, the original's Title.njp pattern 0.")
    sheet = folder / "Title.sheet"
    sheet.write_text(sheet.read_text().replace("tools/art/ui.py", "tools/art/title_screen.py"))
    shrink.shrink(folder / "Title.png")
    print("wrote the title screen")


if __name__ == "__main__":
    main()
