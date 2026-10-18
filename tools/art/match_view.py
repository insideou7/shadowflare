"""Matches a character's camera angle, scale, facing and posture to the original's.

    python3.13 tools/art/match_view.py <kind> <original folder> <out folder>
        [--calibrate] [--anims idle,walk,attack,hit] [--pitch DEG] [--scale S]
        [--yaw d0,d1,...,d7] [--stride N] [--sheet] [--fit animation:frame]

kind is a creatures.py builder (hero, heroine, goblin, ...); the original
folder is the matching character in the user's own copy of the game, for
example game/ShadowFlare/Player/Male (the body is animation layer 0).

It renders flat silhouettes of our model's body (no equipment) with
Blender's Workbench engine, which takes a fraction of a second a frame, and
compares them with the original's body layer, frame by frame and direction
by direction, with both anchors (the feet) on the same point:

- --calibrate searches the camera pitch, the scale and a yaw offset for
  each of the eight directions that best fit the original's idle and walk,
  and prints the numbers. Pass the result back with --pitch, --scale and
  --yaw to check it, then bake it into studio.py / creatures.py.
- Without it, it reports the posture of each animation in --anims: per
  direction and frame, the overlap of the two silhouettes (IoU) and the
  differences in height, width, how far the body leans and how wide the
  feet stand (report.txt and report.json in the out folder).
- --fit walk:5 searches our joint angles for the pose that best fits that
  frame of the original (see fit() below) and prints it as hero_key
  arguments to paste and tidy.
- --sheet also writes, per animation, a picture of the original, ours and
  both outlines laid over each other, at 2x. It holds the original's
  pixels, so it goes in the scratchpad or /mnt/project-files/art-tests/,
  never in the repository.

Only measurements are taken from the original; nothing it draws is copied
into our art.
"""

import json
import math
import sys
from pathlib import Path

import numpy as np

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
sys.path.insert(0, str(HERE.parent / "re"))
import caf  # noqa: E402
import render as njp  # noqa: E402
import upd  # noqa: E402

try:  # Blender is only needed to render ours; measuring works without it.
    import bpy
    from mathutils import Euler, Vector

    import creatures
    import studio
except ImportError:
    bpy = None

# Original chart per animation, as build_pack.py maps them.
PLAYER = {"idle": 0, "walk": 1, "run": 2, "hit": 3, "attack": 5, "shoot": 10, "heal": 11, "cast": 13}
ENEMY = {"idle": 0, "walk": 1, "hit": 2, "death": 3, "attack": 4, "cast": 7}
SIZE = 240  # canvas, pixels
# The original's animation layers that make the bare body: the player's
# body is layer 0 (equipment is drawn over it in other layers); a monster
# or townsperson is all of its layers. Set by main().
BODY = [(0,)]
ALL = "all"
FEET = (120, 190)  # where the anchor sits on the canvas


# --- the original ----------------------------------------------------------

def load_original(folder):
    """(charts, bank) of the character's first animation file."""
    folder = Path(folder)
    stem = "Animation00" if (folder / "Animation00.Caf").exists() else "Animation"
    return (caf.parse((folder / f"{stem}.Caf").read_bytes())["charts"],
            upd.parse((folder / f"{stem}.Njp").read_bytes()))


def original_frame(charts, bank, chart, direction, frame, layers=None, colour=False):
    """The original's frame on our canvas: an alpha mask (or RGBA if colour),
    drawn the way tools/re/animframe.py composes it. layers: which
    animation layers (None: all; BODY[0] for the player's bare body)."""
    if layers is None:
        layers = BODY[0]
    img = np.zeros((SIZE, SIZE, 4), np.uint8)
    cells = []
    for li, layer in enumerate(charts[chart]["dirs"][direction]["layers"]):
        if (layers == ALL or li in layers) and frame < len(layer) and layer[frame]["status"] != 8:
            cells.append((layer[frame]["priority"], li, layer[frame]))
    for _, _, cell in sorted(cells, key=lambda c: -c[0]):
        r = njp.render_pattern(bank, cell["pattern"])
        if not r:
            continue
        pattern = bank["patterns"][cell["pattern"]]
        x0, y0 = pattern["rect"][0], pattern["rect"][1]
        if pattern["rect"][2] <= 0:
            parts = [e for e in pattern["parts"] if 0 <= e["part"] < len(bank["parts"])]
            x0, y0 = min(e["x"] for e in parts), min(e["y"] for e in parts)
        w, h, px = r
        px = np.frombuffer(bytes(px), np.uint8).reshape(h, w, 4)
        X, Y = FEET[0] + x0, FEET[1] + y0
        xa, ya, xb, yb = max(0, X), max(0, Y), min(SIZE, X + w), min(SIZE, Y + h)
        if xa >= xb or ya >= yb:
            continue
        src = px[ya - Y:yb - Y, xa - X:xb - X]
        on = src[:, :, 3] > 0
        img[ya:yb, xa:xb][on] = src[on]
    return img if colour else img[:, :, 3] > 0


def frame_count(charts, chart):
    return len(charts[chart]["dirs"][0]["layers"][0])


# --- ours ------------------------------------------------------------------

class Silhouettes:
    """Our model, rendered flat and fast, with the anchor on FEET."""

    def __init__(self, kind, out):
        self.kind, self.out = kind, Path(out)
        self.scene = studio.reset(samples=1)
        self.rig = creatures.BUILDERS[kind]()
        creatures.add_casting(self.rig, kind)
        s = self.scene
        s.render.engine = "BLENDER_WORKBENCH"
        s.display.shading.light = "FLAT"
        # Flat colours by object: the body red, equipment green, so one
        # render tells which piece is in front at every pixel.
        s.display.shading.color_type = "OBJECT"
        s.display.render_aa = "OFF"
        s.render.film_transparent = True
        bpy.data.objects["shadow catcher"].hide_render = True
        self.pitch = None
        self.path = str(self.out / "_silhouette.png")
        self.body = [o for o in bpy.data.objects if o.type == "MESH" and o.name != "shadow catcher"]
        for o in self.body:
            o.color = (1, 0, 0, 1)
        self.gear = {}
        self.layer = 0

    def show(self, layer):
        """Layer 0 shows the body; another shows that piece of equipment
        (creatures.GEAR) where it is in front of the body, as the original
        draws its layers."""
        if layer == self.layer:
            return
        if layer and layer not in self.gear:
            self.gear[layer] = creatures.build_gear(self.rig, layer)
            for o in self.gear[layer]:
                o.color = (0, 1, 0, 1)
        for n, objects in self.gear.items():
            for o in objects:
                o.hide_render = n != layer
        self.layer = layer

    def set_pitch(self, pitch):
        if pitch == self.pitch:
            return
        if self.scene.camera:
            bpy.data.objects.remove(self.scene.camera)
        # Centre the camera so the anchor (the origin) lands on FEET.
        studio.camera(self.scene, SIZE, SIZE, elevation=pitch)
        ax, ay = studio.anchor(self.scene)
        cam = self.scene.camera
        right = cam.matrix_world.to_3x3().col[0]
        up = cam.matrix_world.to_3x3().col[1]
        px = cam.data.ortho_scale / SIZE
        cam.location += right * (FEET[0] - ax) * -px + up * (FEET[1] - ay) * px
        self.pitch = pitch

    def mask(self, animation, frames, frame, direction, yaw=0.0, layer=0):
        creatures.pose(self.rig, animation, frame / frames, frames)
        return self.shoot(direction, yaw, layer)

    def captured(self, animation, frames, frame):
        """Our pose at a frame as {joint: (x, y, z)} plus "shift" (the hips
        moved off their rest place), to start a fit from."""
        creatures.pose(self.rig, animation, frame / frames, frames)
        r = {name: tuple(e.rotation_euler) for name, e in self.rig.joints.items() if name != "root"}
        r["shift"] = tuple(self.rig.joints["hips"].location - self.rig.rest["hips"])
        return r

    def mask_pose(self, r, direction, yaw=0.0):
        """Silhouette of a given pose (as captured() returns)."""
        r = dict(r)
        shift = r.pop("shift", (0, 0, 0))
        self.rig.pose(r)
        self.rig.joints["hips"].location = self.rig.rest["hips"] + Vector(shift)
        creatures.plant(self.rig)
        return self.shoot(direction, yaw, 0)

    def shoot(self, direction, yaw, layer):
        self.show(layer)
        rig = self.rig
        spin = rig.joints["root"].rotation_euler
        rig.joints["root"].rotation_euler = Euler((spin.x, spin.y, creatures.facing(direction, rig) + yaw))
        studio.render(self.scene, self.path)
        img = bpy.data.images.load(self.path, check_existing=False)
        a = np.array(img.pixels[:], np.float32).reshape(SIZE, SIZE, 4)[::-1]
        bpy.data.images.remove(img)
        if layer:
            return (a[:, :, 3] > 0.5) & (a[:, :, 1] > a[:, :, 0])
        return a[:, :, 3] > 0.5

    def frames(self, animation):
        return getattr(self.rig, "animations", creatures.ANIMATIONS)[animation][0]


# --- measuring ------------------------------------------------------------

def scaled(mask, s):
    """A mask scaled by s about the anchor (nearest neighbour)."""
    if abs(s - 1) < 1e-6:
        return mask
    ys, xs = np.mgrid[0:SIZE, 0:SIZE]
    sx = np.clip(np.round(FEET[0] + (xs - FEET[0]) / s).astype(int), 0, SIZE - 1)
    sy = np.clip(np.round(FEET[1] + (ys - FEET[1]) / s).astype(int), 0, SIZE - 1)
    return mask[sy, sx]


def iou(a, b):
    u = np.logical_or(a, b).sum()
    return float(np.logical_and(a, b).sum() / u) if u else 1.0


def metrics(mask):
    """Measurements of a silhouette, in pixels from the anchor (x right,
    y up): height, left/right reach, centre of mass, lean (degrees the
    head sits off the hips, + = to the screen right),
    and feet spread (width of the lowest 12% of the body)."""
    ys, xs = np.nonzero(mask)
    if not len(ys):
        return None
    top, bottom = ys.min(), ys.max()
    h = bottom - top + 1
    # Lean: the line from the hips (rows 45-60% down) to the head (the
    # top 15%), in degrees from upright, + = head to the screen right.
    head = ys < top + h * 0.15
    hips = (ys > top + h * 0.45) & (ys < top + h * 0.60)
    dx = xs[head].mean() - xs[hips].mean()
    dy = ys[hips].mean() - ys[head].mean()
    lean = math.degrees(math.atan2(dx, dy))
    low = ys > bottom - h * 0.12
    return {
        "height": float(FEET[1] - top),
        "left": float(FEET[0] - xs.min()),
        "right": float(xs.max() - FEET[0]),
        "cx": float(xs.mean() - FEET[0]),
        "cy": float(FEET[1] - ys.mean()),
        "lean": lean,
        "feet": float(xs[low].max() - xs[low].min() + 1),
        "area": float(len(xs)),
    }


def compare(orig, ours):
    a, b = metrics(orig), metrics(ours)
    if not a or not b:
        return None
    d = {k: b[k] - a[k] for k in a}
    d["iou"] = iou(orig, ours)
    d["height_ratio"] = b["height"] / a["height"]
    return d, a, b


# --- calibration -----------------------------------------------------------
#
# The eight directions of a frame are one 3D pose seen from eight sides. A
# point of the model (the shield's middle, the sword's middle, the top of
# the head) goes round a level circle as it turns, and an orthographic
# camera pitched p above the ground draws that circle as an ellipse whose
# height is sin(p) times its width. So fitting an ellipse to where the point
# lands in the eight directions measures the camera's pitch, whatever the
# pose; and where on the ellipse each direction lands measures how far the
# model was turned for it. Centres of drawn layers are not exact 3D points
# (the body hides parts of them), so the same measurement is made on our
# renders at several pitches, and the pitch whose measurement matches the
# original's is the answer.

FEATURES = {"shield": 9, "sword": 12, "head": 0}  # head: in the body layers


def feature_point(mask, name):
    ys, xs = np.nonzero(mask)
    if len(ys) < 5:
        return None
    if name == "head":
        top = ys.min()
        sel = ys < top + (FEET[1] - top) * 0.12
        xs, ys = xs[sel], ys[sel]
    return float(xs.mean()), float(ys.mean())


def ellipse(points):
    """(pitch in degrees, centre, angle of each point in degrees) of the
    level circle that best projects to points, or None."""
    p = np.array(points)
    x, y = p[:, 0], p[:, 1]
    A, B, C, D = np.linalg.lstsq(np.stack([x * x, y * y, x, y], 1), np.ones(len(x)), rcond=None)[0]
    if A * B <= 0:
        return None
    k = min(1.0, math.sqrt(A / B))
    cx, cy = -C / (2 * A), -D / (2 * B)
    # Screen y is down; the side of the circle nearer the camera is lower.
    angles = [math.degrees(math.atan2((py - cy) / k, px - cx)) for px, py in points]
    return math.degrees(math.asin(k)), (cx, cy), angles


def measure_views(get_mask, features, frames):
    """Per feature: the ellipse of each sampled frame."""
    out = {}
    for name in features:
        fits = []
        for f in frames:
            points = [feature_point(get_mask(f, d, FEATURES[name]), name) for d in range(8)]
            if all(points):
                e = ellipse(points)
                if e:
                    fits.append(e)
        out[name] = fits
    return out


def wrap(a):
    return (a + 180) % 360 - 180


def calibrate(sil, charts, bank, table, args):
    """Camera pitch and per-direction turn, measured as above, plus the
    sword's length on screen for scale."""
    chart = table["idle"]
    n = frame_count(charts, chart)
    m = sil.frames("idle")
    times = [k / 5 for k in range(5)]
    player = sil.kind in ("hero", "heroine")
    features = ["shield", "sword", "head"] if player else ["head"]

    def orig_mask(t, d, layer):
        return original_frame(charts, bank, chart, d, int(t * n), layers=(layer,) if layer else None)

    def our_mask(t, d, layer):
        return sil.mask("idle", m, int(round(t * m)) % m, d, layer=layer)

    log = []

    def say(line):
        log.append(line)
        print(line, flush=True)

    theirs = measure_views(orig_mask, features, times)
    target = {f: float(np.median([e[0] for e in fits])) for f, fits in theirs.items() if fits}
    say("original, ellipse pitch per feature (median of idle frames): "
        + "  ".join(f"{f} {v:.1f}" for f, v in target.items()))
    pitches = args.get("pitches", [32, 36, 40, 41.8, 44, 48])
    curve = {}
    for deg in pitches:
        sil.set_pitch(math.radians(deg))
        ours = measure_views(our_mask, features, times)
        curve[deg] = {f: float(np.median([e[0] for e in fits])) for f, fits in ours.items() if fits}
        say(f"ours at pitch {deg:5.1f}: " + "  ".join(f"{f} {v:.1f}" for f, v in curve[deg].items()))
    answers = {}
    for f, want in target.items():
        xs = [p for p in pitches if f in curve[p]]
        ys = [curve[p][f] for p in xs]
        if len(xs) >= 2 and ys[-1] != ys[0]:
            # The measurement rises with the pitch: read the curve backwards.
            order = np.argsort(ys)
            answers[f] = float(np.interp(want, np.array(ys)[order], np.array(xs)[order]))
    pitch = float(np.median(list(answers.values())))
    say("pitch that reproduces each: " + "  ".join(f"{f} {v:.1f}" for f, v in answers.items())
        + f"  -> {pitch:.1f} degrees (studio.ELEVATION is {math.degrees(studio.ELEVATION):.1f})")
    # Facing: where each direction sits round the ellipse, ours against
    # theirs. A constant difference is only the pose (where the shield is
    # held), so it is taken out; what is left is our turn per direction.
    sil.set_pitch(math.radians(pitch))
    best = "shield" if "shield" in theirs and theirs["shield"] else features[-1]
    ours = measure_views(our_mask, [best], times)[best]
    diffs = []
    for (_, _, a), (_, _, b) in zip(theirs[best], ours):
        dd = [wrap(x - y) for x, y in zip(a, b)]
        mean = math.degrees(math.atan2(np.mean(np.sin(np.radians(dd))), np.mean(np.cos(np.radians(dd)))))
        diffs.append([wrap(x - mean) for x in dd])
    yaw = [float(np.median([d[k] for d in diffs])) for k in range(8)] if diffs else [0.0] * 8
    spacing = [wrap(theirs[best][0][2][(k + 1) % 8] - theirs[best][0][2][k]) for k in range(8)] if theirs[best] else []
    say("original's turn between directions (deg): " + " ".join(f"{v:+.0f}" for v in spacing))
    say("extra turn our directions need (deg, from the " + best + "): " + " ".join(f"{v:+.0f}" for v in yaw))
    scale = None
    if player:
        # The sword is rigid: its longest drawn length over the eight
        # directions is its true length, whatever the pose.
        def longest(get):
            best_len = 0
            for d in range(8):
                ys, xs = np.nonzero(get(0, d, FEATURES["sword"]))
                if len(xs) > 3:
                    pts = np.stack([xs, ys], 1).astype(float)
                    axis = np.linalg.svd(pts - pts.mean(0), full_matrices=False)[2][0]
                    t = (pts - pts.mean(0)) @ axis
                    best_len = max(best_len, t.max() - t.min() + 1)
            return best_len
        a, b = longest(orig_mask), longest(our_mask)
        scale = a / b if b else None
        if scale:
            say(f"short sword drawn length: original {a:.0f}px, ours {b:.0f}px (ratio {scale:.3f})")
    heights = []
    for t in times:
        for d in range(8):
            mo, mm = metrics(orig_mask(t, d, 0)), metrics(our_mask(t, d, 0))
            if mo and mm:
                heights.append(mo["height"] / mm["height"])
    say(f"idle height, original / ours: {np.median(heights):.3f} (includes any difference in crouch)")
    say(f"BEST --pitch {pitch:.1f} --yaw {','.join(f'{v:.0f}' for v in yaw)}")
    return log


# --- posture report ----------------------------------------------------------

def outline(mask):
    m = mask
    edge = m & ~(np.roll(m, 1, 0) & np.roll(m, -1, 0) & np.roll(m, 1, 1) & np.roll(m, -1, 1))
    return edge


def report(sil, charts, bank, table, args, out):
    from PIL import Image

    pitch, scale, yaws = args["pitch"], args["scale"], args["yaw"]
    sil.set_pitch(math.radians(pitch))
    stride = args.get("stride", 1)
    rows, lines, summary = {}, [], {}
    for name in args["anims"]:
        n = frame_count(charts, table[name])
        m = sil.frames(name)
        tiles = []
        stats = []
        loop = getattr(sil.rig, "animations", creatures.ANIMATIONS)[name][1]
        phase = 0
        if loop:
            # A looping animation can start at another point of its cycle:
            # find the shift of our frames that fits best.
            probe = range(0, n, max(1, n // 5))
            fits = []
            for k in range(m):
                fits.append((float(np.mean([iou(original_frame(charts, bank, table[name], d, f),
                                                scaled(sil.mask(name, m, (int(f / n * m) + k) % m, d,
                                                                math.radians(yaws[d])), scale))
                                            for f in probe for d in (0, 2, 4, 6)])), k))
            phase = max(fits)[1]
            lines.append(f"{name}: best phase shift of our frames {phase} of {m} "
                         f"(IoU {max(fits)[0]:.3f}; unshifted {fits[0][0]:.3f})")
            print(lines[-1], flush=True)
        if m != n:
            lines.append(f"{name}: the original has {n} frames, ours {m}")
            print(lines[-1], flush=True)
        for f in range(0, n, stride):
            if loop:
                ours_f = (int(f / n * m) + phase) % m
            else:
                ours_f = int(round(f * (m - 1) / max(1, n - 1)))
            for d in range(8):
                orig = original_frame(charts, bank, table[name], d, f)
                mine = scaled(sil.mask(name, m, ours_f, d, math.radians(yaws[d])), scale)
                c = compare(orig, mine)
                if not c:
                    continue
                delta = c[0]
                stats.append(delta)
                lines.append(f"{name:7s} f{f:02d}->{ours_f:02d} d{d}  IoU {delta['iou']:.2f}  "
                             f"height {delta['height']:+5.1f}  width L{delta['left']:+5.1f} R{delta['right']:+5.1f}  "
                             f"lean {delta['lean']:+5.1f}  feet {delta['feet']:+5.1f}  "
                             f"centre {delta['cx']:+5.1f},{delta['cy']:+5.1f}")
                if args.get("sheet"):
                    tiles.append((f, d, orig, mine))
        keys = ["iou", "height", "left", "right", "lean", "feet", "cx", "cy"]
        summary[name] = {k: round(float(np.mean([s[k] for s in stats])), 2) for k in keys}
        summary[name].update({f"abs_{k}": round(float(np.mean([abs(s[k]) for s in stats])), 2)
                              for k in keys if k != "iou"})
        lines.append(f"== {name}: " + "  ".join(f"{k} {v}" for k, v in summary[name].items()))
        print(lines[-1], flush=True)
        if tiles:
            rows[name] = tiles
    for name, tiles in rows.items():
        # One row per frame: per direction, original | ours | overlay.
        frames = sorted({t[0] for t in tiles})
        cw, ch = 110, 130  # crop around the feet
        x0, y0 = FEET[0] - cw // 2, FEET[1] - ch + 20
        sheet = Image.new("RGB", (8 * 3 * cw, len(frames) * ch), (70, 74, 80))
        for f, d, orig, mine in tiles:
            r = frames.index(f)
            col = np.array(original_frame(charts, bank, table[name], d, f, colour=True))
            a = Image.fromarray(col).crop((x0, y0, x0 + cw, y0 + ch))
            bg = Image.new("RGBA", a.size, (70, 74, 80, 255))
            bg.alpha_composite(a)
            sheet.paste(bg.convert("RGB"), ((d * 3) * cw, r * ch))
            ours_img = np.zeros((SIZE, SIZE, 3), np.uint8) + np.array([70, 74, 80], np.uint8)
            ours_img[mine] = (200, 200, 205)
            sheet.paste(Image.fromarray(ours_img).crop((x0, y0, x0 + cw, y0 + ch)), ((d * 3 + 1) * cw, r * ch))
            ov = np.zeros((SIZE, SIZE, 3), np.uint8) + np.array([30, 30, 34], np.uint8)
            ov[orig] = (120, 40, 40)
            ov[mine & ~orig] = (40, 90, 120)
            ov[mine & orig] = (150, 150, 150)
            ov[outline(orig)] = (255, 80, 80)
            ov[outline(mine)] = (90, 210, 255)
            sheet.paste(Image.fromarray(ov).crop((x0, y0, x0 + cw, y0 + ch)), ((d * 3 + 2) * cw, r * ch))
        sheet = sheet.resize((sheet.width * 2, sheet.height * 2), Image.NEAREST)
        sheet.save(out / f"{sil.kind}_{name}.png")
    (out / "report.txt").write_text("\n".join(lines) + "\n")
    (out / "report.json").write_text(json.dumps({"pitch": pitch, "scale": scale, "yaw": yaws,
                                                 "summary": summary, "notes": [x for x in lines if x.split(":")[0] in args["anims"]]},
                                                indent=1))
    return summary


# --- fitting a pose ----------------------------------------------------------
#
# Searches our joint angles for the pose whose silhouettes best overlap the
# original's in all eight directions at once (which settles what is in front
# and what behind). It moves one angle at a time by a step, keeps what
# helps, and halves the step when nothing does. A small pull back to the
# starting pose keeps it from contorting to win a few pixels. The result is
# a starting point to tidy by hand, not a final pose.

FIT_AXES = {"spine": "xy", "head": "y", "arm_l": "xy", "arm_r": "xy", "elbow_l": "x", "elbow_r": "y",
            "leg_l": "xy", "leg_r": "xy", "knee_l": "y", "knee_r": "y", "shift": "x"}


def fit(sil, charts, bank, table, args, out):
    name, frame = args["fit"].split(":")
    frame = int(frame)
    n = frame_count(charts, table[name])
    m = sil.frames(name)
    ours_f = int(round(frame * (m - 1) / max(1, n - 1)))
    sil.set_pitch(math.radians(args["pitch"]))
    yaws, scale = args["yaw"], args["scale"]
    origs = [original_frame(charts, bank, table[name], d, frame) for d in range(8)]
    start = sil.captured(name, m, ours_f)
    pose = {k: list(v) for k, v in start.items()}

    def score(p):
        overlap = np.mean([iou(origs[d], scaled(sil.mask_pose(p, d, math.radians(yaws[d])), scale))
                           for d in range(8)])
        pull = sum((a - b) ** 2 for j in FIT_AXES for a, b in zip(p[j], start[j]))
        return float(overlap) - 0.002 * pull

    best = score(pose)
    first = best
    print(f"fit {name} frame {frame} (ours {ours_f}): start {best:.3f}", flush=True)
    for step in (0.3, 0.15, 0.08):
        improved = True
        while improved:
            improved = False
            for joint, axes in FIT_AXES.items():
                for axis in axes:
                    i = "xyz".index(axis)
                    size = step * (1.5 if joint == "shift" else 1)
                    for sign in (1, -1):
                        trial = {k: list(v) for k, v in pose.items()}
                        trial[joint][i] += sign * size
                        sc = score(trial)
                        if sc > best + 1e-4:
                            pose, best, improved = trial, sc, True
                            break
        print(f"  step {step}: {best:.3f}", flush=True)
    text = ", ".join(f"{j}=({', '.join(f'{x:.2f}' for x in pose[j])})" for j in FIT_AXES)
    line = f"{name} frame {frame}: mean IoU {first:.3f} -> {best:.3f}\n  hero_key(..., {text})"
    print(line)
    with open(out / "fit.txt", "a") as f:
        f.write(line + "\n")


def main():
    argv = sys.argv[1:]

    def option(name, default=None, convert=str):
        if name in argv:
            k = argv.index(name)
            value = convert(argv[k + 1])
            del argv[k:k + 2]
            return value
        return default

    args = {
        "anims": option("--anims", ["idle", "walk", "attack", "hit"], lambda v: v.split(",")),
        "pitch": option("--pitch", math.degrees(studio.ELEVATION), float),
        "scale": option("--scale", 1.0, float),
        "yaw": option("--yaw", [0] * 8, lambda v: [float(x) for x in v.split(",")]),
        "stride": option("--stride", 1, int),
        "pitches": option("--pitches", None, lambda v: [float(x) for x in v.split(",")]),
        "fit": option("--fit", None),
    }
    if args["pitches"] is None:
        del args["pitches"]
    cal = "--calibrate" in argv
    args["sheet"] = "--sheet" in argv
    argv = [a for a in argv if a not in ("--calibrate", "--sheet")]
    kind, folder, out = argv[0], argv[1], Path(argv[2])
    out.mkdir(parents=True, exist_ok=True)
    charts, bank = load_original(folder)
    table = PLAYER if kind in ("hero", "heroine") else ENEMY
    if kind not in ("hero", "heroine"):
        BODY[0] = ALL
    sil = Silhouettes(kind, out)
    if args["fit"]:
        fit(sil, charts, bank, table, args, out)
    elif cal:
        (out / "calibration.txt").write_text("\n".join(calibrate(sil, charts, bank, table, args)) + "\n")
    else:
        report(sil, charts, bank, table, args, out)


if __name__ == "__main__":
    main()
