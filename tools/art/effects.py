#!/usr/bin/env python3
"""The spell effects for the open content pack.

    python3 tools/art/effects.py <content pack folder> [effect number ...]

Draws, from scratch, the animations the engine plays for spells: fire,
ice, lightning, earth and light, as Character/OPTION/<number>/Animation
(.anim, .sheet and .png). The numbers are the original's, so the engine
finds our art where it would find ShadowFlare's own, and each effect has
the original's frame count, directions, size, anchor and blending, so it
plays for as long and sits in the same place. The pictures themselves are
painted here from noise, glows, streaks and shaded facets; nothing is
taken from the original's sprites.

Most effects are light: they are drawn on black and added to the screen
(a `*` line in the .anim), as the original adds its own. Solid things
(earth spikes, mud javelins, fire ball cores) are drawn with transparency.
Needs numpy and Pillow.
"""

import math
import sys
from pathlib import Path

import numpy as np
from PIL import Image, ImageDraw, ImageFilter

HEADER = "# Drawn by tools/art/effects.py (our own art, see content/README.md).\n"

# Directions as the engine numbers them: 0 = south (down the screen), then
# counter-clockwise on screen. Unit vectors in screen space.
DIRS = [(math.sin(d * math.pi / 4), math.cos(d * math.pi / 4)) for d in range(8)]
GROUND = 10 / 15  # how flat a circle on the ground looks (projection 15/10)


# --- drawing on a light canvas (float RGB, added together) -----------------

class Canvas:
    """A float RGB picture of light; (ax, ay) is the effect's position."""

    def __init__(self, w, h, ax, ay):
        self.w, self.h, self.ax, self.ay = w, h, ax, ay
        self.c = np.zeros((h, w, 3), np.float32)
        ys, xs = np.mgrid[0:h, 0:w].astype(np.float32)
        self.x = xs - ax  # coordinates relative to the anchor
        self.y = ys - ay

    def add(self, field, colour, k=1.0):
        self.c += field[..., None] * (np.asarray(colour, np.float32) / 255.0 * k)

    def blob(self, x, y, rx, ry, colour, k=1.0, sharp=2.0):
        d = ((self.x - x) / max(rx, 0.5)) ** 2 + ((self.y - y) / max(ry, 0.5)) ** 2
        self.add(np.exp(-d * sharp), colour, k)

    def ring(self, x, y, rx, ry, width, colour, k=1.0, arc=None, tilt=0.0):
        px, py = self.x - x, self.y - y
        if tilt:
            px, py = px * math.cos(tilt) + py * math.sin(tilt), -px * math.sin(tilt) + py * math.cos(tilt)
        r = np.sqrt((px / max(rx, 0.5)) ** 2 + (py / max(ry, 0.5)) ** 2)
        f = np.exp(-((r - 1) / max(width, 0.01)) ** 2)
        if arc is not None:  # (start, length) in radians, fading at the ends
            a = np.arctan2(py / ry, px / rx)
            t = np.mod(a - arc[0], 2 * math.pi) / arc[1]
            f = f * np.clip(np.sin(np.clip(t, 0, 1) * math.pi), 0, 1) * (t <= 1)
        self.add(f, colour, k)

    def streak(self, x0, y0, x1, y1, width, colour, k=1.0, taper=False):
        dx, dy = x1 - x0, y1 - y0
        ll = max(dx * dx + dy * dy, 1e-6)
        t = np.clip(((self.x - x0) * dx + (self.y - y0) * dy) / ll, 0, 1)
        px, py = x0 + t * dx, y0 + t * dy
        d2 = (self.x - px) ** 2 + (self.y - py) ** 2
        w = width * (1 - 0.8 * t) if taper else width
        self.add(np.exp(-d2 / (w * w + 1e-6)), colour, k)

    def rgb(self, gain=1.0):
        """Tone-mapped 8-bit colour; bright light runs to white as fire does."""
        c = self.c * gain
        over = np.clip(c.max(axis=2, keepdims=True) - 1, 0, None)
        c = c + over * 0.6  # spill into the other channels
        return (np.clip(c, 0, 1) * 255).astype(np.uint8)


def noise(w, h, cell, seed, octaves=3):
    """Smooth value noise in 0..1, `cell` pixels per lump."""
    rng = np.random.default_rng(seed)
    out = np.zeros((h, w), np.float32)
    amp, total = 1.0, 0.0
    for o in range(octaves):
        c = max(cell / (2 ** o), 1.5)
        gw, gh = int(w / c) + 3, int(h / c) + 3
        g = Image.fromarray((rng.random((gh, gw)) * 255).astype(np.uint8))
        g = g.resize((int(gw * c), int(gh * c)), Image.BICUBIC)
        out += np.asarray(g, np.float32)[:h, :w] / 255 * amp
        total += amp
        amp *= 0.5
    return out / total


def heat(t, cool=False):
    """Fire colours for 0..1: dark red, orange, yellow, white."""
    t = np.clip(t, 0, 1)[..., None]
    stops = np.array([[0, 0, 0], [110, 20, 0], [230, 90, 10], [255, 190, 60], [255, 250, 220]], np.float32)
    if cool:
        stops = np.array([[0, 0, 0], [10, 30, 90], [40, 110, 200], [150, 210, 255], [240, 250, 255]], np.float32)
    x = t * (len(stops) - 1)
    i = np.clip(x.astype(int), 0, len(stops) - 2)
    f = x - i
    return (stops[i[..., 0]] * (1 - f) + stops[i[..., 0] + 1] * f) / 255


def fire(cv, x, y, w, h, t, seed, k=1.0, cool=False, lean=0.0, tongues=9):
    """A bush of licking flame standing on (x, y), `w` wide and `h` tall, at
    time `t` (frames): tongues of flame streaked by noise that drifts up."""
    rng = np.random.default_rng(seed)
    # Noise stretched upwards and drifting up gives the flame its streaks.
    n = noise(cv.w, cv.h // 3 + 2, 4, seed + 2, 3)
    n = np.asarray(Image.fromarray((n * 255).astype(np.uint8)).resize((cv.w, (cv.h // 3 + 2) * 3), Image.BICUBIC),
                   np.float32) / 255
    streaks = np.roll(n, -int(t * 5), axis=0)[:cv.h]
    d = np.zeros((cv.h, cv.w), np.float32)
    for i in range(tongues):
        u = (i + 0.5) / tongues * 2 - 1
        tx = x + u * w * 0.42 + rng.normal(0, w * 0.05)
        th = h * (1 - 0.55 * abs(u)) * rng.uniform(0.7, 1.05) * (0.85 + 0.15 * math.sin(t * 1.3 + i * 2.1))
        tw = w * 0.16 * rng.uniform(0.8, 1.3)
        ry = (y - cv.y) / max(th, 1)  # 0 at the foot, 1 at the tip
        across = (cv.x - tx - lean * ry * th - math.sin(t * 0.9 + i) * ry * tw * 0.6) / tw
        body = np.exp(-across ** 2 * (1 + 3 * ry.clip(0, 1) ** 2)) * np.clip(1 - ry, 0, 1) ** 0.8 \
            * np.clip(1 + ry * 6, 0, 1)
        d = np.maximum(d, body * rng.uniform(0.75, 1.0))
    d = np.clip(d * (0.55 + 0.75 * streaks) - 0.12, 0, 1)
    cv.c += heat(d * 1.25, cool) * np.clip(d * 3, 0, 1)[..., None] * k


def smoke(cv, x, y, rx, ry, seed, colour, k=1.0, hole=0.0, bits=0.0):
    """A mottled cloud; `hole` eats its middle, `bits` breaks it into lumps."""
    n = noise(cv.w, cv.h, 9, seed, 3)
    r = np.sqrt(((cv.x - x) / rx) ** 2 + ((cv.y - y) / ry) ** 2)
    f = np.clip(1 - r, 0, 1) ** 0.7
    if hole:
        f *= np.clip((r - hole) * 4, 0, 1)
    f = np.clip(f * (0.6 + n) - bits * (1.2 - n), 0, 1)
    cv.add(f, colour, k)


def sparks(cv, rng, x, y, count, reach, angle, spread, length, colour, k=1.0, t=1.0, width=1.2):
    """Streaks thrown from (x, y) in a fan, `t` of the way out (0..1)."""
    for _ in range(count):
        a = angle + rng.normal(0, spread)
        r = reach * (0.4 + 0.6 * rng.random()) * t
        sx, sy = math.cos(a), math.sin(a) * GROUND
        ex, ey = x + sx * r, y + sy * r - (1 - (1 - t) ** 2) * reach * 0.15
        ln = length * (0.5 + rng.random())
        cv.streak(ex - sx * ln, ey - sy * ln, ex, ey, width, colour, k * (0.5 + rng.random()))


def bolt(rng, x0, y0, x1, y1, detail=6, rough=0.25):
    """A jagged line from (x0, y0) to (x1, y1) by midpoint displacement."""
    pts = [(x0, y0), (x1, y1)]
    for _ in range(detail):
        out = [pts[0]]
        for (ax, ay), (bx, by) in zip(pts, pts[1:]):
            ln = math.hypot(bx - ax, by - ay)
            nx, ny = -(by - ay) / max(ln, 1e-6), (bx - ax) / max(ln, 1e-6)
            o = rng.normal(0, rough * ln * 0.5)
            out += [((ax + bx) / 2 + nx * o, (ay + by) / 2 + ny * o), (bx, by)]
        pts = out
    return pts


def draw_bolt(cv, pts, width, glow, core, k=1.0):
    for (ax, ay), (bx, by) in zip(pts, pts[1:]):
        cv.streak(ax, ay, bx, by, width * 3, glow, k * 0.6)
        cv.streak(ax, ay, bx, by, width, core, k)


# --- solid things, drawn with transparency ----------------------------------

def facet_shard(draw, ox, oy, x, y, length, width, angle, base, light=(-0.6, -0.8), sides=3):
    """A pointed crystal (or spike) from (x, y) along `angle`, shaded by facet."""
    ca, sa = math.cos(angle), math.sin(angle)
    tip = (x + ca * length, y + sa * length)
    nx, ny = -sa, ca
    faces = []
    for s in range(sides):
        a0 = -1 + 2 * s / sides
        a1 = -1 + 2 * (s + 1) / sides
        p0 = (x + nx * width * a0, y + ny * width * a0 * 0.8 + abs(a0) * width * 0.3)
        p1 = (x + nx * width * a1, y + ny * width * a1 * 0.8 + abs(a1) * width * 0.3)
        mid = (a0 + a1) / 2
        shade = 0.55 + 0.45 * (mid * light[0] * nx + mid * light[1] * ny + 0.4)
        faces.append(([p0, p1, tip], shade))
    for pts, shade in faces:
        col = tuple(int(np.clip(c * shade, 0, 255)) for c in base) + (255,)
        draw.polygon([(px + ox, py + oy) for px, py in pts], fill=col)
    return tip


def spike_frames(count, grow, fade, colour, seed, height=110, width=18):
    """A stone spike breaking out of the ground: RGBA frames with anchors."""
    rng = np.random.default_rng(seed)
    lean = rng.normal(0, 0.12)
    out = []
    for f in range(count):
        g = min(1.0, (f + 1) / grow)
        w, h = width * 2 + 6, height + 16
        im = Image.new("RGBA", (w, h))
        dr = ImageDraw.Draw(im)
        ax, ay = w // 2, h - 8
        facet_shard(dr, ax, ay, 0, 0, height * g, width / 2 * (0.5 + 0.5 * g), -math.pi / 2 + lean, colour, sides=3)
        # Broken earth around the foot.
        for _ in range(4):
            px, py = rng.normal(0, width * 0.5), rng.normal(2, 2)
            dr.ellipse([ax + px - 3, ay + py - 2, ax + px + 3, ay + py + 2], fill=tuple(int(c * 0.6) for c in colour) + (255,))
        out.append((im, ax, ay))
    return out


def ice_lump(draw, ox, oy, x, y, height, width, base, seed, light=(-0.6, -0.8), points=12):
    """A lumpy block of ice cut into many facets, its foot on (x, y)."""
    rng = np.random.default_rng(seed)
    if height < 2 or width < 2:
        return
    cx, cy = x, y - height / 2
    rim = []
    for i in range(points):
        a = 2 * math.pi * i / points + rng.normal(0, 0.15)
        r = rng.uniform(0.78, 1.05)
        rim.append((cx + math.cos(a) * width / 2 * r, cy + math.sin(a) * height / 2 * r))
    inner = [(cx + (px - cx) * 0.55 + rng.normal(0, width * 0.06), cy + (py - cy) * 0.55 + rng.normal(0, height * 0.05))
             for px, py in rim]
    centre = (cx + rng.normal(0, width * 0.08), cy + rng.normal(0, height * 0.06))
    lx, ly, lz = light[0], light[1], 0.8
    ln = math.sqrt(lx * lx + ly * ly + lz * lz)
    faces = []
    for i in range(points):
        j = (i + 1) % points
        faces.append(([rim[i], rim[j], inner[j]], 0.35))
        faces.append(([rim[i], inner[j], inner[i]], 0.6))
        faces.append(([inner[i], inner[j], centre], 1.2))
    for pts, z in faces:
        mx = sum(px for px, _ in pts) / 3 - cx
        my = sum(py for _, py in pts) / 3 - cy
        nx, ny = mx / (width / 2), my / (height / 2)
        nl = math.sqrt(nx * nx + ny * ny + z * z)
        lit = max(0.0, (nx * lx + ny * ly + z * lz) / (nl * ln))
        shade = np.clip(0.42 + 0.68 * lit + rng.normal(0, 0.07), 0.3, 1.15)
        col = tuple(int(np.clip(c * shade, 0, 255)) for c in base) + (255,)
        draw.polygon([(px + ox, py + oy) for px, py in pts], fill=col)


def grain(cv, seed, fine=1.5, floor=0.25):
    """Breaks light into specks, like the original's dithered glows."""
    n = noise(cv.w, cv.h, fine, seed, 1)
    cv.c *= np.clip(floor + (1 - floor) * (n * 1.8 - 0.4), 0, 1.4)[..., None]


# --- effects ---------------------------------------------------------------

class Effect:
    """One Character/OPTION folder: frames plus the .anim layout."""

    def __init__(self, number):
        self.number = number
        self.frames = []  # (RGBA image, anchor x, anchor y)
        self.charts = []  # [(loop, {direction: [(additive, priority, [frame or None, ...], [trans])]})]

    def frame(self, image, ax, ay):
        """Adds a frame cropped to what is drawn; returns its number."""
        bbox = image.getbbox()
        if bbox is None:
            image, ax, ay = Image.new("RGBA", (1, 1)), 0, 0
        else:
            image = image.crop(bbox)
            ax, ay = ax - bbox[0], ay - bbox[1]
        self.frames.append((image, ax, ay))
        return len(self.frames) - 1

    def light(self, cv, gain=1.0):
        """A light canvas as an additive frame: colour on black."""
        rgb = cv.rgb(gain)
        alpha = (rgb.max(axis=2) > 6).astype(np.uint8) * 255
        return self.frame(Image.fromarray(np.dstack([rgb, alpha]), "RGBA"), cv.ax, cv.ay)

    def chart(self, loop, directions):
        self.charts.append((loop, directions))

    def write(self, folder):
        folder.mkdir(parents=True, exist_ok=True)
        # Pack frames in rows no wider than 1024.
        x = y = row = 0
        places = []
        for im, _, _ in self.frames:
            if x + im.width > 1024:
                x, y, row = 0, y + row + 1, 0
            places.append((x, y))
            x += im.width + 1
            row = max(row, im.height)
        sheet = Image.new("RGBA", (1024 if y else max(x, 1), y + row + 1))
        for (im, _, _), (px, py) in zip(self.frames, places):
            sheet.paste(im, (px, py))
        save_png(sheet, folder / "Animation.png")
        lines = [HEADER, "image=Animation.png\n"]
        for (im, ax, ay), (px, py) in zip(self.frames, places):
            lines.append(f"frame={px},{py},{im.width},{im.height},{ax},{ay}\n")
        (folder / "Animation.sheet").write_text("".join(lines))
        lines = [HEADER, "sheet=Animation.sheet\n"]
        for c, (loop, directions) in enumerate(self.charts):
            lines.append(f"\n[chart {c} {'loop' if loop else 'once'}]\n")
            for d, layers in directions.items():
                for li, (add, prio, frames, trans) in enumerate(layers):
                    key = f"{d}" + (f".{li}" if li else "") + (f"@{prio}" if prio else "") + ("*" if add else "")
                    cells = []
                    for i, fr in enumerate(frames):
                        cell = "-" if fr is None else str(fr)
                        if trans and trans[i] != 1000 and fr is not None:
                            cell += f"%{trans[i]}"
                        cells.append(cell)
                    lines.append(f"{key}={','.join(cells)}\n")
        (folder / "Animation.anim").write_text("".join(lines))


def save_png(image, path):
    """256 colours with index 0 see-through, like the original's sprites."""
    arr = np.asarray(image)
    solid = arr[..., 3] > 0
    rgb = Image.fromarray(np.ascontiguousarray(arr[..., :3]))
    q = rgb.quantize(255, method=Image.Quantize.MEDIANCUT, dither=Image.Dither.NONE)
    idx = np.asarray(q).astype(np.uint8) + 1
    idx[~solid] = 0
    pal = [0, 0, 0] + q.getpalette()[:255 * 3]
    out = Image.fromarray(idx, "P")
    out.putpalette(pal + [0] * (768 - len(pal)))
    # Keep part-see-through pixels (spike fades) as the alpha of each colour
    # would be lost; those effects fade by the .anim's per-frame alpha instead.
    out.save(path, transparency=0, optimize=True)


def ease(t):
    return t * t * (3 - 2 * t)


def fade_in_out(f, n, fin, fout):
    a = min(1.0, (f + 1) / max(fin, 1))
    if f >= n - fout:
        a = min(a, (n - f) / (fout + 1))
    return a


# Fire Ball and Flame Strike: flames gathering in the hands as the spell is cast.
def cast_flame(e):
    n = 13
    frames = []
    for f in range(n):
        t = f / (n - 1)
        cv = Canvas(120, 140, 60, 100)
        g = 0.6 + 0.4 * ease(min(1, t * 1.6))
        a = 1 - max(0, (t - 0.75) / 0.25) * 0.7
        # A bouquet of flame: tongues leaning outwards from a hot heart.
        fire(cv, 0, 22, 60 * g, 100 * g + 10, f, 1200 + f // 2, 0.9 * a, tongues=8)
        fire(cv, -10, 18, 44 * g, 60 * g, f + 3, 1300 + f // 2, 0.7 * a, lean=-1.0, tongues=5)
        fire(cv, 10, 18, 44 * g, 60 * g, f + 5, 1400 + f // 2, 0.7 * a, lean=1.0, tongues=5)
        cv.blob(0, -25, 30 * g, 26 * g, (255, 130, 30), 0.35 * a)
        frames.append(e.light(cv))
    e.chart(False, {8: [(True, 0, frames, None)]})


def missile_frames(e, n, draw, size=(90, 90)):
    """Frames for a missile in 8 directions; draw(cv, f, dx, dy)."""
    dirs = {}
    for d, (dx, dy) in enumerate(DIRS):
        frames = []
        for f in range(n):
            cv = Canvas(size[0], size[1], size[0] // 2, size[1] // 2)
            draw(cv, f, dx, dy)
            frames.append(e.light(cv))
        dirs[d] = frames
    return dirs


# Fire Ball: a ball of fire, its head on the anchor, trailing flame and embers.
def fire_ball(e):
    n = 9

    def draw(cv, f, dx, dy):
        rng = np.random.default_rng(f)
        along = -(cv.x * dx + cv.y * dy)          # distance behind the head
        across = cv.x * -dy + cv.y * dx
        body = np.exp(-(np.clip(along, 0, None) / 40) ** 2 - (np.clip(-along, 0, None) / 6) ** 2) \
            * np.exp(-(across / (15 + 0.15 * np.clip(along, 0, None))) ** 2)
        tex = noise(cv.w, cv.h, 4, 77 + f, 3)
        d = np.clip(body * (0.5 + 0.9 * tex) - 0.1, 0, 1)
        cv.c += heat(d * 1.35) * np.clip(d * 2.5, 0, 1)[..., None]
        cv.blob(-dx * 7, -dy * 7, 9, 9, (255, 240, 200), 0.9, sharp=2.5)
        for _ in range(7):  # embers falling behind
            u = rng.uniform(18, 46)
            cv.blob(-dx * u + rng.normal(0, 6), -dy * u + rng.normal(0, 6), 1.8, 1.8, (255, 140, 40), 0.9)
    dirs = missile_frames(e, n, draw)
    e.chart(True, {d: [(True, 0, fr, None)] for d, fr in dirs.items()})


# Fire Ball hits: a burst of flame, a glowing core, then fire burning out.
def fire_burst(e):
    frames = []
    for f in range(29):
        cv = Canvas(130, 160, 65, 110)
        rng = np.random.default_rng(f)
        if f < 10:
            t = f / 9
            smoke(cv, 0, -5, 38 + 14 * ease(min(1, t * 2)), 36 + 14 * ease(min(1, t * 2)), 500 + f,
                  (255, 175, 70), 1.8 * (1 - 0.5 * t), bits=0.45 - 0.3 * t)
            cv.blob(0, -5, 18, 15, (255, 240, 200), 1.2 * (1 - t))
        elif f < 15:
            t = (f - 10) / 4
            cv.blob(0, -8, 20 + 6 * t, 18 + 6 * t, (255, 160, 50), 0.9 * (1 - 0.5 * t))
            cv.blob(0, -8, 7, 7, (255, 240, 200), 1.1 * (1 - 0.7 * t))
        else:
            t = (f - 15) / 13
            fire(cv, 0, 4, 46 * (1 - 0.6 * t), 80 * (1 - 0.5 * t) + 10, f, 600 + f // 3, 1.0 * (1 - t))
        frames.append(e.light(cv))
    e.chart(False, {8: [(True, 0, frames, None)]})


# Ice Bolt and Dread Deathscythe: cold air swirling round the caster.
def ice_swirl(e):
    # Two thin, speckled comets of frost sweeping round the caster and
    # widening out.
    n = 16
    frames, trans = [], []
    for f in range(n):
        t = f / (n - 1)
        cv = Canvas(150, 100, 75, 60)
        r = 10 + 52 * ease(min(1, t * 1.3))
        head = f * 0.75
        for k, (lag, ry, col) in enumerate([(0.0, 0.42, (225, 235, 255)), (math.pi, 0.5, (175, 200, 240))]):
            # A bright head with a tail fading back along the arc.
            for step in range(14):
                a = head + lag - step * 0.16
                fade = (1 - step / 14) ** 1.5
                px, py = math.cos(a) * r, math.sin(a) * r * ry - 18
                qx, qy = math.cos(a - 0.17) * r, math.sin(a - 0.17) * r * ry - 18
                cv.streak(px, py, qx, qy, 1.6 + 1.8 * fade, col, 1.0 * fade)
        grain(cv, 2700 + f, 1.5, 0.35)
        frames.append(e.light(cv))
        trans.append(int(1000 * fade_in_out(f, n, 2, 6)))
    e.chart(False, {8: [(True, 0, frames, trans)]})


# Ice Bolt: a white-hot shard of ice, its head on the anchor, spraying frost.
def ice_bolt(e):
    def draw(cv, f, dx, dy):
        rng = np.random.default_rng(f + 40)
        along = -(cv.x * dx + cv.y * dy)
        across = cv.x * -dy + cv.y * dx
        body = np.exp(-(np.clip(along, 0, None) / 36) ** 2 - (np.clip(-along, 0, None) / 4) ** 2) \
            * np.exp(-(across / (10 + 0.1 * np.clip(along, 0, None))) ** 2)
        tex = noise(cv.w, cv.h, 1.5, 4000 + f, 2)
        d = np.clip((body * (0.6 + 0.9 * tex) - 0.3) * 2.5, 0, 1.3)
        # White at the head, going blue-green and speckled down the tail.
        w = np.clip((along - 10) / 26, 0, 1)[..., None]
        cv.c += d[..., None] * (np.array([1.25, 1.3, 1.33]) * (1 - w) + np.array([0.55, 1.0, 0.95]) * w)
        # A ragged fringe of blue-green frost round the white core.
        fringe = np.exp(-(np.clip(along - 4, 0, None) / 34) ** 2 - (np.clip(4 - along, 0, None) / 4) ** 2) \
            * np.exp(-(across / 16) ** 2) * np.clip(1.2 - body, 0, 1)
        cv.add(np.clip((fringe * (0.3 + tex) - 0.45) * 4, 0, 1), (120, 230, 215), 1.0)
        for _ in range(16):  # frost thrown off behind
            u = rng.uniform(6, 34)
            sx = -dx * u + -dy * rng.normal(0, 3 + u * 0.3)
            sy = -dy * u + dx * rng.normal(0, 3 + u * 0.3)
            cv.blob(sx, sy, 1.3, 1.3, (130, 235, 215), rng.uniform(0.5, 1.1) * (1 - u / 60))
    dirs = missile_frames(e, 11, draw)
    e.chart(True, {d: [(True, 0, fr, None)] for d, fr in dirs.items()})


# Ice hits: frost gathering, a block of ice forming and shattering into needles.
def frost_hit(e):
    n = 20
    rng = np.random.default_rng(55)
    shards = [(rng.uniform(-math.pi * 0.95, -math.pi * 0.05), rng.uniform(12, 30), rng.uniform(5, 10)) for _ in range(9)]
    needles = [(rng.uniform(-math.pi, 0.4), rng.uniform(15, 50)) for _ in range(46)]
    mist, block, bits = [], [], []
    for f in range(n):
        # Grey freezing haze all through.
        cv = Canvas(120, 130, 60, 80)
        smoke(cv, 0, -14 - f * 0.6, 40 + f * 0.7, 32 + f * 0.8, 900 + f // 2, (165, 170, 185), 1.2, bits=0.3)
        grain(cv, 950 + f, 1.5, 0.4)
        mist.append(e.light(cv))
        # A white block of ice, faceted, that cracks and goes grey.
        if 2 <= f < 15:
            im = Image.new("RGBA", (110, 110))
            dr = ImageDraw.Draw(im)
            g = ease(min(1, (f - 1) / 3))
            grey = 1 - 0.35 * max(0, (f - 9) / 5)
            col = tuple(int(c * grey) for c in (232, 238, 250))
            for k, (a, ln, wd) in enumerate(shards):  # splinters standing out
                facet_shard(dr, 55, 70, 0, -16, ln * 1.4 * g, wd * g * 0.7, a, tuple(int(c * 0.8) for c in col), sides=3)
            ice_lump(dr, 55, 70, 0, 10, 56 * g, 64 * g, col, 560 + f // 4, points=13)
            block.append(e.frame(im, 55, 70))
        else:
            block.append(None)
        # Frost needles flying apart and falling.
        cv = Canvas(130, 140, 65, 90)
        if f >= 8:
            u = (f - 8) / 11
            for a, r in needles:
                x, y = math.cos(a) * r * (0.35 + u), math.sin(a) * r * (0.35 + u) - 14 + 34 * u * u
                cv.streak(x, y, x - math.cos(a) * 5, y - math.sin(a) * 5 + 3 * u, 0.9, (215, 225, 240), 0.9)
            grain(cv, 990 + f, 1.5, 0.5)
        bits.append(e.light(cv))
    mt = [min(1000, 120 * (f + 1)) if f < 9 else (1000 if f < 16 else 1000 - 200 * (f - 15)) for f in range(n)]
    bt = [1000 if f < 11 else max(200, 1000 - 200 * (f - 10)) for f in range(n)]
    nt = [1000 if f < 15 else 1000 - 180 * (f - 14) for f in range(n)]
    e.chart(False, {8: [(True, 0, mist, mt), (False, 1, block, bt), (True, 2, bits, nt)]})


# Plasma and Lightning Storm: a bolt from the sky, in four shapes, that
# frays into blue foam as it goes.
def lightning(e):
    for c in range(4):
        rng = np.random.default_rng(300 + c)
        frames = []
        pts = bolt(rng, rng.normal(0, 8), -478, 0, 0, detail=2, rough=0.3)
        pts = [q for a_, b_ in zip(pts, pts[1:]) for q in bolt(rng, *a_, *b_, detail=3, rough=0.1)[:-1]] + [pts[-1]]
        pts = [(max(-38, min(38, x)), y) for x, y in pts]
        for f in range(6):
            cv = Canvas(110, 486, 55, 482)
            if f < 3:
                jit = [(x + rng.normal(0, 0.8), y) for x, y in pts]
                draw_bolt(cv, jit, 2.6, (30, 110, 255), (240, 250, 255), 1.1)
                cv.blob(0, 0, 14, 7, (150, 190, 255), 0.6)
            else:
                # Foam: lumps along the bolt, thinning and drifting.
                u = (f - 2) / 3
                for (x, y), (x2, y2) in zip(pts, pts[1:]):
                    for _ in range(4):
                        if rng.random() < 1 - 0.45 * u:
                            m = rng.random()
                            cv.blob(x + (x2 - x) * m + rng.normal(0, 3 + 6 * u), y + (y2 - y) * m, 3, 3,
                                    (90, 160, 255) if rng.random() < 0.7 else (220, 235, 255), 0.9 - 0.3 * u)
            frames.append(e.light(cv))
        e.chart(False, {8: [(True, 0, frames, None)]})


# Where a lightning bolt strikes: a tongue of cold fire leaping up.
def lightning_splash(e):
    frames = []
    for f in range(6):
        cv = Canvas(70, 90, 35, 78)
        t = (f + 1) / 6
        fire(cv, 0, 2, 16 + 10 * t, 30 + 40 * t, f, 3100, 1.5, cool=True, tongues=4)
        cv.blob(0, -14 - 12 * t, 6 + 3 * t, 10 + 10 * t, (200, 230, 255), 0.7)
        frames.append(e.light(cv))
    e.chart(False, {8: [(True, 0, frames, None)]})


# Glitter rising after a lightning strike.
def lightning_glitter(e):
    frames, trans = [], []
    rng = np.random.default_rng(5)
    motes = rng.normal(0, 1, (45, 3))
    for f in range(11):
        cv = Canvas(90, 90, 45, 78)
        t = (f + 1) / 11
        for mx, my, ms in motes:
            cv.blob(mx * 22 * t, -abs(my) * 45 * t - 4, 0.9, 0.9, (235, 245, 255), 1.3)
        frames.append(e.light(cv))
        trans.append(1000 if f < 5 else 1000 - 100 * (f - 4))
    e.chart(False, {8: [(True, 0, frames, trans)]})


# Plasma hits: crackling over the target, flickering on and off.
def shock(e):
    frames = []
    trans = []
    for f in range(29):
        rng = np.random.default_rng(f + 70)
        cv = Canvas(80, 110, 40, 85)
        smoke(cv, 0, -40, 28, 42, 3300 + f, (20, 40, 150), 0.6, bits=0.35)
        for _ in range(12):
            x0, y0 = rng.normal(0, 13), rng.uniform(-78, -4)
            x1, y1 = x0 + rng.normal(0, 14), y0 + rng.normal(0, 14)
            draw_bolt(cv, bolt(rng, x0, y0, x1, y1, 4, 0.4), 0.5, (30, 70, 220), (170, 200, 240), 0.5)
        frames.append(e.light(cv))
        trans.append(1000 if f % 2 == 0 else 500)
    e.chart(True, {8: [(True, 0, frames, trans)]})


# Hell Fire: a ring of fire spreading over the ground round the caster.
def fire_ring(e):
    frames = []
    for f in range(5):
        t = (f + 1) / 5
        cv = Canvas(220, 180, 110, 90)
        rx = 56 + 44 * ease(t)
        n = noise(cv.w, cv.h, 7, 40 + f, 2)
        r = np.sqrt((cv.x / rx) ** 2 + (cv.y / (rx * 0.8)) ** 2)
        band = np.exp(-((r - 0.85) / 0.2) ** 2) * (0.35 + n)
        cv.c += heat(band * 0.62) * np.clip(band * 1.3, 0, 1)[..., None] * (1.0 - 0.35 * max(0, t - 0.6) / 0.4)
        frames.append(e.light(cv))
    e.chart(False, {8: [(True, 0, frames, None)]})


# Hell Fire: golden flame thrown out in fans of rays that break into sparks.
def fire_spray(e):
    def fan(angle, seed):
        rng = np.random.default_rng(seed)
        out = []
        rays = [(rng.normal(0, 0.2), rng.uniform(0.45, 1.0), rng.uniform(0.5, 1.0)) for _ in range(60)]
        for f in range(16):
            t = (f + 1) / 16
            cv = Canvas(330, 330, 165, 200)
            grow = ease(min(1, t * 1.8))
            fade = 1 if t < 0.5 else max(0, 1 - (t - 0.5) / 0.5)
            for da, reach, bright in rays:
                a = angle + da
                r = 155 * reach * grow
                ca, sa = math.cos(a), math.sin(a) * 0.85
                if t < 0.55:  # rays from the heart
                    cv.streak(ca * r * 0.25, sa * r * 0.25, ca * r, sa * r, 1.3, (255, 190, 90), bright * 0.55, taper=True)
                else:  # breaking up into sparks
                    m = rng.uniform(0.4, 1.0)
                    cv.streak(ca * r * m, sa * r * m, ca * r * (m + 0.06), sa * r * (m + 0.06), 1.1,
                              (255, 190, 90), bright * fade)
            out.append(e.light(cv))
        return out
    # Chart 0 throws up and to the right and down to the left; chart 1 up
    # and up to the left.
    e.chart(False, {8: [(True, 0, fan(-math.pi * 0.2, 11), None), (True, 1, fan(math.pi * 0.78, 12), None)]})
    e.chart(False, {8: [(True, 0, fan(-math.pi * 0.55, 13), None), (True, 1, fan(-math.pi * 0.82, 14), None)]})


# Hell Fire hits: a golden splash of shards.
def gold_burst(e):
    frames = []
    rng = np.random.default_rng(21)
    shards = [(rng.uniform(-math.pi, 0), rng.uniform(20, 55), rng.uniform(0.5, 1)) for _ in range(16)]
    for f in range(25):
        t = (f + 1) / 25
        cv = Canvas(110, 150, 55, 100)
        g = ease(min(1, t * 2.2))
        if t < 0.5:
            cv.blob(0, -20, 14, 22, (255, 200, 90), 1.2 * (1 - t * 2))
        for a, r, b in shards:
            x, y = math.cos(a) * r * g, math.sin(a) * r * g * 1.4 - 20 + 40 * t * t
            cv.streak(x, y, x - math.cos(a) * 7, y - math.sin(a) * 7, 1.6, (255, 190, 80), b * (1 - t))
        frames.append(e.light(cv))
    e.chart(False, {8: [(True, 0, frames, None)]})


# Ice Blast: a storm of cold streaks drawing in, knotting into rings of frost.
def ice_gather(e, colour=(200, 215, 240), ring=(225, 235, 255), n=15):
    frames = []
    rng = np.random.default_rng(31)
    rays = [(rng.uniform(0, 2 * math.pi), rng.uniform(0.45, 1.0), rng.uniform(10, 34)) for _ in range(170)]
    for f in range(n):
        t = f / (n - 1)
        cv = Canvas(200, 190, 100, 95)
        if t < 0.45:
            u = t / 0.45
            for a, rr, ln in rays:
                r = 66 * rr * (1 - u) + 24 * rr
                x, y = math.cos(a) * r, math.sin(a) * r * 0.9
                cv.streak(x, y, x - math.cos(a) * ln * (1 - u * 0.5), y - math.sin(a) * ln * 0.9 * (1 - u * 0.5), 0.4,
                          colour, 0.75)
            cv.blob(0, 0, 6 + 10 * u, 6 + 10 * u, colour, 0.25 * u)
            grain(cv, 3100 + f, 1.5, 0.45)
        else:
            u = (t - 0.45) / 0.55
            for k in range(4):
                r = 26 + 32 * ease(u)
                cv.ring(0, 0, r, r * (0.3 + 0.17 * k), 0.07 + 0.04 * u, ring, 1.0, tilt=k * math.pi / 4 + u * 1.6)
            lumps = noise(cv.w, cv.h, 3, 3000 + f, 2)
            cv.c *= (0.35 + lumps)[..., None]
            grain(cv, 3200 + f, 1.5, 0.5)
        frames.append(e.light(cv))
    e.chart(False, {8: [(True, 0, frames, None)]})


# Ice Blast: great blocks of ice heaving up from the ground.
def ice_block(draw, ox, oy, x, y, height, width, colour, seed):
    rng = np.random.default_rng(seed)
    # A tall lumpy column of ice: a long block with a second grown into
    # its upper half.
    lean = rng.normal(0, width * 0.1)
    ice_lump(draw, ox, oy, x, y, height * 0.7, width, colour, seed * 7, points=16)
    ice_lump(draw, ox, oy, x + lean, y - height * 0.32, height * 0.68, width * rng.uniform(0.8, 0.95), colour,
             seed * 7 + 1, points=16)


def ice_crystals(e):
    layers = []
    # Back and front rows of columns: (x, y, height, delay) for each. They
    # start as a scatter of small lumps and heave up together.
    rows = [[(-50, -28, 150, 1), (2, -40, 160, 0), (54, -26, 150, 2)],
            [(-46, 30, 110, 3), (4, 40, 120, 2), (52, 32, 108, 4)]]
    for li, row in enumerate(rows):
        frames = [None]
        for f in range(1, 21):
            im = Image.new("RGBA", (340, 320))
            dr = ImageDraw.Draw(im)
            for bi, (x, y, height, delay) in enumerate(row):
                g = ease(min(1, max(0, (f - delay) / 10)))
                if g <= 0:
                    continue
                ice_block(dr, 170, 220, x, y, height * g, 52 * (0.35 + 0.65 * g), (226, 232, 248), li * 7 + bi + 1)
            frames.append(e.frame(im, 170, 220 - 60))
        layers.append((False, li, frames, [1000] * 18 + [800, 600, 400]))
    e.chart(True, {8: layers})


# Ice Blast: freezing mist rolling out round the crystals.
def ice_mist(e):
    # A lumpy, uneven ring of freezing fog that thickens and whitens.
    outer, inner = [], []
    for f in range(21):
        k = 0.9 + 0.9 * min(1, f / 8)
        cv = Canvas(250, 190, 125, 100)
        smoke(cv, 4, -14, 112, 80, 1100 + f // 3, (210, 216, 236), k * 1.3, hole=0.3, bits=0.05)
        lumps = noise(cv.w, cv.h, 30, 1150, 2)
        cv.c *= np.clip((lumps - 0.3) * 3.0, 0, 1.4)[..., None]
        grain(cv, 1160 + f, 1.5, 0.85)
        outer.append(e.light(cv))
        cv = Canvas(130, 110, 65, 95)
        smoke(cv, -6, -40, 46, 38, 1200 + f // 3, (175, 182, 205), 0.8 * k, bits=0.3)
        inner.append(e.light(cv))
    trans = [500, 600, 700, 800, 900] + [1000] * 13 + [900, 600, 300]
    e.chart(False, {8: [(True, 0, outer, trans), (True, 1, inner, trans)]})


# Earth Spears: a burst of dust, a spike of stone thrust up, and grit.
def earth_spear(e):
    flash, dust = [], []
    for f in range(15):
        cv = Canvas(260, 260, 130, 180)
        rng = np.random.default_rng(f + 90)
        if f < 6:
            u = f / 5
            smoke(cv, 0, -40 - 10 * u, 60 - 10 * u, 70 - 10 * u, 1600 + f, (235, 230, 220), 1.6 * (1 - 0.5 * u), bits=0.2)
        else:
            u = (f - 6) / 8
            sparks(cv, rng, 0, -20, 22, 70, -math.pi / 2, 0.5, 14, (240, 220, 190), 0.8, t=0.6 + 0.4 * u)
        flash.append(e.light(cv))
        cv = Canvas(240, 210, 120, 120)
        smoke(cv, 0, -10 - f, 85 + f * 2.2, 62 + f * 1.8, 1500 + f // 3, (170, 165, 160), 0.9, hole=0.15, bits=0.3)
        dust.append(e.light(cv))
    spikes = [e.frame(im, ax, ay) for im, ax, ay in spike_frames(15, 3, 9, (140, 100, 72), 5, 112, 36)]
    e.chart(False, {8: [
        (True, 0, flash, [1000] * 8 + [800, 600, 400, 200, 10, 10, 10]),
        (False, 1, spikes, [1000] * 6 + [900, 800, 700, 600, 500, 400, 300, 200, 100]),
        (True, 2, dust, [200, 400, 600, 800, 1000, 1000, 900, 800, 700, 600, 500, 400, 300, 200, 100]),
    ]})


# Elemental magic gathering: grit swirling in, then bursting.
def debris_swirl(e):
    frames = []
    rng = np.random.default_rng(61)
    bits = [(rng.uniform(0, 2 * math.pi), rng.uniform(0.5, 1.0)) for _ in range(40)]
    for f in range(18):
        t = f / 17
        cv = Canvas(190, 160, 95, 110)
        if t < 0.4:
            r = 90 * (1 - t / 0.4) + 10
            for a, rr in bits:
                a2 = a + t * 4
                x, y = math.cos(a2) * r * rr, math.sin(a2) * r * rr * 0.7 - 20
                cv.streak(x, y, x + math.sin(a2) * 6, y - math.cos(a2) * 4, 1.2, (170, 170, 170), 0.8)
        else:
            u = (t - 0.4) / 0.6
            cv.blob(0, -30, 20 + 20 * u, 22 + 18 * u, (230, 230, 230), 1.1 * (1 - u))
            sparks(cv, rng, 0, -30, 26, 70, -math.pi / 2, 1.8, 6, (170, 170, 170), 0.8 * (1 - u), t=u)
        frames.append(e.light(cv))
    e.chart(False, {8: [(True, 0, frames, None)]})


# Dread Deathscythe: grey crystal blades, turning as they fly.
def blade_missile(e, n, seed, length):
    # A lumpy shard of grey-green ice tumbling along, point first.
    dirs = {}
    for d, (dx, dy) in enumerate(DIRS):
        frames = []
        for f in range(n):
            im = Image.new("RGBA", (110, 110))
            dr = ImageDraw.Draw(im)
            spin = f / n * 2 * math.pi
            wd = 15 + 6 * abs(math.sin(spin))
            ice_lump(dr, 55, 55 + length / 2, 0, 0, length, wd, (190, 205, 205), seed * 100 + f % 4,
                     light=(math.cos(spin) * 0.8, -0.7), points=10)
            frames.append(e.frame(im, 55, 55))
        dirs[d] = frames
    e.chart(True, {d: [(True, 0, fr, None)] for d, fr in dirs.items()})


# Sonic Blade: a white wedge of cut air with a long ragged wake.
def sonic_blade(e):
    def draw(cv, f, dx, dy):
        rng = np.random.default_rng(f % 9)
        along = -(cv.x * dx + cv.y * dy)
        across = cv.x * -dy + cv.y * dx
        body = np.exp(-(np.clip(along, 0, None) / 22) ** 2 - (np.clip(-along, 0, None) / 4) ** 2) \
            * np.exp(-(across / (14 - 0.2 * np.clip(along, 0, 40))) ** 2)
        cv.add(body * (0.6 + 0.6 * noise(cv.w, cv.h, 4, 900 + f % 9, 2)), (235, 235, 240), 1.3)
        for _ in range(30):  # the wake
            u = rng.uniform(10, 75)
            sx, sy = -dx * u - dy * rng.normal(0, 4 + u * 0.12), -dy * u + dx * rng.normal(0, 4 + u * 0.12)
            cv.streak(sx, sy, sx - dx * 6, sy - dy * 6, 1.0, (150, 150, 155), rng.uniform(0.3, 0.8) * (1 - u / 90))
    dirs = missile_frames(e, 34, draw, (180, 180))
    e.chart(True, {d: [(True, 0, fr, [800] * len(fr))] for d, fr in dirs.items()})


# Sonic Blade hits: a spray of torn air shooting up from a heap of grit.
def air_burst(e):
    frames, trans = [], []
    rng = np.random.default_rng(130)
    rays = [(rng.normal(-math.pi / 2, 0.3), rng.uniform(30, 95), rng.uniform(0.4, 1.0)) for _ in range(40)]
    lumps = [(rng.normal(0, 20), rng.normal(0, 6), rng.uniform(3, 7)) for _ in range(16)]
    for f in range(26):
        t = f / 25
        cv = Canvas(150, 210, 75, 150)
        g = ease(min(1, t * 2.5))
        for a, r, b in rays:
            if t < 0.45:
                cv.streak(0, 0, math.cos(a) * r * g, math.sin(a) * r * g, 1.0, (220, 220, 215), b * 0.8, taper=True)
            else:
                m = rng.uniform(0.5, 1.0)
                x, y = math.cos(a) * r * m, math.sin(a) * r * m + 30 * (t - 0.45)
                cv.streak(x, y, x + math.cos(a) * 5, y + math.sin(a) * 5, 0.9, (200, 200, 195), b * (1 - t))
        for x, y, s in lumps:
            cv.blob(x * g, y, s, s * 0.7, (140, 135, 125), 0.6 * (1 - t))
        frames.append(e.light(cv))
        trans.append(1000 if f < 19 else 1000 - 100 * (f - 18))
    e.chart(False, {8: [(True, 0, frames, trans)]})


# Mud Javelin: a glowing spear of earth, its head on the anchor, shedding clods.
def mud_javelin(e):
    def draw(cv, f, dx, dy):
        rng = np.random.default_rng(f)
        along = -(cv.x * dx + cv.y * dy)
        across = cv.x * -dy + cv.y * dx
        body = np.exp(-(np.clip(along, 0, None) / 45) ** 4 - (np.clip(-along, 0, None) / 4) ** 2) \
            * np.exp(-(across / (3 + 0.06 * np.clip(along, 0, None))) ** 2)
        tex = noise(cv.w, cv.h, 4, 500 + f, 2)
        d = np.clip(body * (0.5 + 0.8 * tex), 0, 1)
        cv.c += heat(d * 0.95) * np.clip(d * 2, 0, 1)[..., None]
        for _ in range(10):
            u = rng.uniform(30, 70)
            cv.blob(-dx * u + rng.normal(0, 4), -dy * u + rng.normal(0, 4), 2.5, 2.5, (110, 60, 30), 0.7)
    dirs = missile_frames(e, 21, draw, (150, 150))
    e.chart(True, {d: [(True, 0, fr, None)] for d, fr in dirs.items()})


# Mud Javelin lands: a cloud of dust, spikes breaking out and a ring of grit.
def mud_burst(e):
    dust, ring = [], []
    for f in range(15):
        cv = Canvas(240, 210, 120, 120)
        u = f / 14
        smoke(cv, 0, -20 - f, 80 + f * 2, 64 + f * 1.6, 1900 + f // 3, (235, 232, 225),
              1.7 * (1 - 0.8 * u), hole=0.3 * u, bits=0.1 + 0.4 * u)
        dust.append(e.light(cv))
        cv = Canvas(330, 250, 165, 130)
        if 3 <= f < 13:
            t = (f - 2) / 10
            cv.ring(0, 0, 40 + 70 * t, (40 + 70 * t) * 0.6, 0.06, (230, 230, 225), 0.9)
            cv.c *= (0.3 + noise(cv.w, cv.h, 3, 4000 + f, 1))[..., None]
        ring.append(e.light(cv))
    layers = [(True, 0, dust, None), (True, 1, ring, [1000] * 11 + [800, 600, 1000, 1000])]
    for li, (sx, sy) in enumerate([(-30, 14), (24, 14), (-42, -24), (-4, -44), (38, -24)]):
        sp = [e.frame(im, ax - sx, ay - sy) for im, ax, ay in spike_frames(15, 2, 4, (140, 100, 72), 70 + li, 112, 36)]
        layers.append((False, 2 + li, sp, [1000] * 11 + [800, 600, 400, 200]))
    e.chart(False, {8: layers})


# Heal: soft golden light rising round the caster.
def heal(e):
    rng = np.random.default_rng(81)
    motes = [(rng.normal(0, 30), rng.uniform(-150, 0), rng.uniform(0.6, 1.4)) for _ in range(40)]
    frames, trans = [], []
    for f in range(25):
        t = f / 24
        cv = Canvas(170, 190, 85, 120)
        cv.ring(0, 0, 50, 34, 0.12, (150, 160, 70), 0.6 * (1 - t))
        for mx, my, ms in motes:
            y = (my - t * 70) % -150
            x = mx * (1 - 0.3 * (y / -150))
            cv.blob(x, y, 1.6 * ms, 1.6 * ms, (255, 250, 170), 1.0)
            cv.blob(x, y, 5 * ms, 5 * ms, (120, 140, 40), 0.35)
        cv.blob(0, -50, 40, 60, (90, 90, 30), 0.3)
        frames.append(e.light(cv))
        trans.append([200, 400, 600, 800][f] if f < 4 else (1000 if f < 14 else max(10, 1000 - 100 * (f - 13))))
    e.chart(True, {8: [(True, 0, frames, trans)]})


# Magic Shield soaks a spell: a blue sphere closing in, then ripples.
def magic_shield(e):
    frames = []
    for f in range(20):
        cv = Canvas(100, 100, 50, 50)
        if f < 10:
            t = f / 9
            r = 42 * (1 - t) + 8
            cv.ring(0, 0, r, r, 0.25 + 0.4 * t, (20, 110, 220), 1.0)
        else:
            t = (f - 10) / 9
            cv.blob(0, 0, 8, 8, (40, 140, 240), 0.8 * (1 - t))
            for k in range(3):
                r = 10 + 34 * t - k * 9
                if r > 2:
                    cv.ring(0, 0, r, r, 0.15, (20, 110, 220), 0.8 * (1 - t))
        frames.append(e.light(cv))
    e.chart(True, {8: [(True, 0, frames, None)]})


# Counter Burst sends a blow back: an arc of golden fire.
def counter_burst(e):
    arc, sparks_l = [], []
    rng = np.random.default_rng(91)
    for f in range(20):
        t = f / 19
        cv = Canvas(160, 120, 70, 60)
        n = noise(cv.w, cv.h, 6, 140 + f, 2)
        r = np.sqrt((cv.x / (40 + 20 * t)) ** 2 + ((cv.y + 10) / (24 + 10 * t)) ** 2)
        a = np.arctan2(cv.y + 10, cv.x)
        band = np.exp(-((r - 1) / 0.25) ** 2) * (np.sin(a - 0.3 + t * 2) > -0.2) * (0.5 + n)
        cv.c += heat(band * 0.8) * np.clip(band * 1.5, 0, 1)[..., None]
        arc.append(e.light(cv))
        cv = Canvas(120, 90, 50, 70)
        sparks(cv, rng, 0, -30, 8, 40, -math.pi / 2, 1.2, 6, (255, 200, 90), 0.8, t=t)
        sparks_l.append(e.light(cv))
    trans = [1000] * 16 + [800, 600, 400, 200]
    e.chart(True, {8: [(True, 0, arc, trans), (True, 1, sparks_l, trans)]})


# Elemental Strike: a blue orb of power settling into a ring.
def power_orb(e):
    frames, trans = [], []
    for f in range(24):
        t = f / 23
        cv = Canvas(110, 100, 50, 50)
        if f < 10:
            u = f / 9
            r = 44 * (1 - u) + 10
            cv.ring(0, 0, r, r * 0.9, 0.2, (20, 90, 230), 1.0)
            cv.ring(0, 0, r * 0.7, r * 0.4, 0.15, (60, 150, 255), 0.8, arc=(u * 6, 4))
        else:
            u = (f - 10) / 13
            cv.blob(0, 0, 6 + 4 * (1 - u), 6, (100, 200, 255), 1.0 * (1 - u))
            cv.ring(0, 0, 14 + 26 * u, (14 + 26 * u) * 0.7, 0.35 - 0.2 * u, (10, 80, 220), 1.0)
        frames.append(e.light(cv))
        trans.append(min(1000, 100 * (f + 1)))
    e.chart(True, {8: [(True, 0, frames, trans)]})


# Elemental Strike's casting: violet light drawn in, then wound into rings.
def element_cast(e):
    ice_gather(e, (200, 180, 255), (215, 195, 255), 16)


# Elemental Strike's orb: a violet flame over a ring on the ground that
# blows away into motes.
def element_orb(e):
    n = 25
    frames = []
    rng = np.random.default_rng(71)
    motes = [(rng.normal(0, 11), rng.normal(-40, 11)) for _ in range(90)]
    for f in range(n):
        t = f / (n - 1)
        cv = Canvas(110, 130, 55, 90)
        if t < 0.62:
            g = ease(min(1, t / 0.5))
            tex = noise(cv.w, cv.h, 3, 7100 + f, 2)
            body = np.exp(-(cv.x / (4 + 8 * g)) ** 2 - ((cv.y + 6 + 24 * g) / (5 + 15 * g)) ** 2)
            d = np.clip(body * (0.3 + 0.9 * tex) - 0.15, 0, 1)
            cv.c += d[..., None] * np.array([205, 175, 255]) * 0.95
            if f >= 4:
                u = min(1, (f - 4) / 10)
                r = 8 + 26 * ease(u)
                cv.ring(0, 0, r, r * 0.55, 0.18, (190, 160, 255), 0.9 * (1 - max(0, (t - 0.45) / 0.17)))
        else:
            u = (t - 0.62) / 0.38
            for x, y in motes:
                cv.blob(x * (1 + 0.5 * u), y - 10 * u, 2.2, 2.2, (215, 190, 255), 0.9 * (1 - u))
        frames.append(e.light(cv))
    e.chart(True, {8: [(True, 0, frames, None)]})


# Elemental Strike's water: a white gush in a ring of foam, breaking into shards.
def water_burst(e):
    n = 21
    rng = np.random.default_rng(81)
    shards = [(rng.uniform(-math.pi, 0.3), rng.uniform(20, 55), rng.uniform(4, 9)) for _ in range(26)]
    frames, trans = [], []
    for f in range(n):
        t = f / (n - 1)
        cv = Canvas(140, 150, 70, 100)
        if f < 9:
            r = 10 + 46 * ease(f / 8)
            cv.ring(0, 0, r, r * 0.4, 0.12, (190, 200, 230), 0.8)
        g = ease(min(1, f / 5))
        if f < 14:
            cv.blob(0, -24 * g, 26 * g + 4, 34 * g + 4, (235, 240, 255), 1.0, sharp=2.5)
            smoke(cv, 0, -26 * g, 30 * g + 6, 36 * g + 6, 8100 + f // 2, (200, 210, 240), 0.9)
        u = max(0, (f - 3) / 17)
        for a, r, wd in shards:
            x, y = math.cos(a) * r * (0.4 + u), math.sin(a) * r * (0.4 + u) - 20 + 25 * u * u
            cv.streak(x, y, x + math.cos(a) * wd, y + math.sin(a) * wd, 2.2, (215, 225, 250), 0.8)
        frames.append(e.light(cv))
        trans.append(1000 if f < 14 else 1000 - 110 * (f - 13))
    e.chart(True, {8: [(True, 0, frames, trans)]})


# A blow landing: yellow chips struck off and falling.
def hit_spark(e):
    n = 11
    rng = np.random.default_rng(91)
    chips = [(rng.uniform(-2.6, -0.4), rng.uniform(8, 26)) for _ in range(9)]
    frames = []
    for f in range(n):
        t = f / (n - 1)
        cv = Canvas(70, 70, 35, 60)
        for a, r in chips:
            d = r * (0.2 + t)
            x, y = math.cos(a) * d, math.sin(a) * d - 36 + 30 * t * t
            col = (255, int(230 - 120 * t), int(80 - 60 * t))
            cv.streak(x, y, x - math.cos(a) * 3, y - math.sin(a) * 3 + 2 * t, 2.2 - t, col, 1.0 - 0.6 * t)
        frames.append(e.light(cv))
    e.chart(False, {8: [(True, 0, frames, None)]})


# Elemental Strike: a great column of fire.
def fire_pillar(e):
    frames, trans = [], []
    for f in range(24):
        t = f / 23
        cv = Canvas(120, 150, 60, 110)
        g = ease(min(1, t * 2.2))
        fire(cv, 0, 6, 40 + 40 * g, 40 + 70 * g, f, 2100 + f // 2, 1.2)
        if t > 0.6:
            cv.c *= np.clip(noise(cv.w, cv.h, 6, 2200 + f, 2) * 2.4 - (t - 0.6) * 3, 0, 1)[..., None]
        frames.append(e.light(cv))
        trans.append([200, 300, 400, 500, 600, 700, 800, 900][f] if f < 8 else (1000 if f < 15 else 1000 - 100 * (f - 14)))
    e.chart(False, {8: [(True, 0, frames, trans)]})


EFFECTS = {
    10000000: fire_spray,
    10000001: gold_burst,
    10000002: fire_ring,
    10000010: fire_ball,
    10000011: fire_burst,
    10000012: cast_flame,
    10000030: lightning,
    10000031: lightning_splash,
    10000032: lightning_glitter,
    10000040: ice_bolt,
    10000050: ice_crystals,
    10000051: ice_gather,
    10000052: ice_mist,
    10000060: earth_spear,
    10000080: lambda e: blade_missile(e, 11, 1, 40),
    10000081: lambda e: blade_missile(e, 10, 2, 46),
    10000090: sonic_blade,
    10000100: element_orb,
    10000110: mud_javelin,
    10000111: mud_burst,
    11000000: hit_spark,
    11000003: shock,
    11000026: frost_hit,
    11000027: ice_swirl,
    11000033: water_burst,
    11000060: heal,
    11000100: debris_swirl,
    11000200: air_burst,
    11000210: element_cast,
    11000230: power_orb,
    11000241: magic_shield,
    11000251: counter_burst,
    12000000: fire_pillar,
}


def main():
    if len(sys.argv) < 2:
        sys.exit(__doc__)
    pack = Path(sys.argv[1])
    wanted = [int(a) for a in sys.argv[2:]] or list(EFFECTS)
    for number in wanted:
        e = Effect(number)
        EFFECTS[number](e)
        e.write(pack / "Character" / "OPTION" / f"{number:08d}")
        print(f"{number:08d}: {len(e.frames)} frames")


if __name__ == "__main__":
    main()
