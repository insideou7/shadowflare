#!/usr/bin/env python3
"""The magic/attack bar's icons, for the open content pack.

    python3 tools/art/magic_icons.py <content pack folder>

Writes two sheets numbered like the original's, so the engine draws the
bar the same way with either (src/game/spells.cpp):

- System/Game/Pattern/MagicIcon: 26 x 26, the chosen icon. 0 is the
  attack, 1 an empty slot, 2-23 the spells in the original's order.
- System/Game/Pattern/MagicBarIcon: 16 x 16, the others. 0 and 1 are
  unused plain slots, 2 the attack, 3 an empty slot, 4-25 the spells.

Each spell is a small picture of its effect painted from scratch with
glows, strokes and rings: a fire ball with its tail, a moon, a ring of
shield light and so on. Needs numpy and Pillow.
"""

import sys
from pathlib import Path

import numpy as np
from PIL import Image

from ui import write_sheet

N = 64  # painted at this size, then shrunk
y, x = (np.mgrid[0:N, 0:N].astype(float) + 0.5) / N  # 0..1


class Canvas:
    def __init__(self, ground=(14, 14, 22)):
        self.rgb = np.zeros((N, N, 3)) + np.array(ground, float)
        r = np.hypot(x - 0.5, y - 0.5)
        self.rgb *= (1.2 - 0.6 * r)[..., None]

    def add(self, amount, colour):
        self.rgb += np.clip(amount, 0, None)[..., None] * np.array(colour, float)

    def paint(self, mask, colour):
        self.rgb = np.where(mask[..., None] > 0, (1 - mask[..., None]) * self.rgb + mask[..., None] * np.array(colour, float),
                            self.rgb)

    def image(self, size):
        img = Image.fromarray(np.clip(self.rgb, 0, 255).astype(np.uint8), "RGB")
        return img.resize((size, size), Image.LANCZOS).convert("RGBA")


def glow(cx, cy, r):
    return np.exp(-((x - cx) ** 2 + (y - cy) ** 2) / (r * r))


def ring(cx, cy, r, w, squash=1.0):
    d = np.hypot(x - cx, (y - cy) / squash)
    return np.exp(-((d - r) ** 2) / (w * w))


def stroke(x0, y0, x1, y1, w):
    dx, dy = x1 - x0, y1 - y0
    t = np.clip(((x - x0) * dx + (y - y0) * dy) / (dx * dx + dy * dy), 0, 1)
    d = np.hypot(x - (x0 + t * dx), y - (y0 + t * dy))
    return np.exp(-(d * d) / (w * w))


def star(cx, cy, points, r, sharp=6.0, turn=0.0):
    a = np.arctan2(y - cy, x - cx) + turn
    d = np.hypot(x - cx, y - cy)
    spikes = np.abs(np.cos(a * points / 2)) ** sharp
    return np.clip(1 - d / (r * (0.25 + 0.75 * spikes)), 0, 1)


def spell_icon(n):
    c = Canvas()
    rng = np.random.default_rng(n)
    if n == 0:  # Transport: a blue whirl
        a = np.arctan2(y - 0.5, x - 0.5)
        d = np.hypot(x - 0.5, y - 0.5)
        c.add(np.clip(np.cos(a * 3 + d * 18), 0, 1) * np.exp(-((d - 0.25) ** 2) / 0.02), (40, 120, 255))
        c.add(glow(0.5, 0.5, 0.12), (180, 220, 255))
    elif n == 1:  # Fire Ball: an orb with a trail
        for k in range(6):
            c.add(0.5 * glow(0.62 - k * 0.08, 0.38 + k * 0.08, 0.10 - k * 0.01), (255, 110, 20))
        c.add(1.4 * glow(0.62, 0.38, 0.14), (255, 170, 60))
        c.add(glow(0.62, 0.38, 0.06), (255, 255, 200))
    elif n == 2:  # Ice Bolt: a pale shard
        c.add(1.6 * stroke(0.2, 0.8, 0.8, 0.2, 0.05), (150, 210, 255))
        c.add(stroke(0.25, 0.75, 0.8, 0.2, 0.015), (255, 255, 255))
    elif n == 3:  # Plasma: a crackling white orb
        c.add(1.3 * glow(0.5, 0.5, 0.18), (160, 230, 255))
        for k in range(7):
            a = rng.uniform(0, 2 * np.pi)
            c.add(0.9 * stroke(0.5, 0.5, 0.5 + 0.4 * np.cos(a), 0.5 + 0.4 * np.sin(a), 0.02), (220, 240, 255))
    elif n == 4:  # Hell Fire: tongues of flame
        for k, cx in enumerate((0.3, 0.5, 0.7)):
            c.add(1.2 * np.exp(-((x - cx) ** 2) / 0.006) * np.clip((y - 0.15 - 0.1 * (k % 2)) * 1.5, 0, 1), (255, 90, 20))
        c.add(0.8 * glow(0.5, 0.85, 0.2), (255, 200, 60))
    elif n == 5:  # Ice Blast: crystals bursting up
        for k in range(5):
            cx = 0.2 + 0.15 * k
            c.add(1.2 * stroke(cx, 0.85, cx + rng.uniform(-0.08, 0.08), 0.25 + rng.uniform(0, 0.2), 0.04), (100, 180, 255))
        c.add(0.6 * glow(0.5, 0.8, 0.2), (200, 240, 255))
    elif n == 6:  # Heal: a gold cross of light
        c.add(1.3 * star(0.5, 0.5, 4, 0.42, 10), (255, 220, 110))
        c.add(glow(0.5, 0.5, 0.1), (255, 255, 230))
    elif n == 7:  # Moon: a pale disc
        d = np.hypot(x - 0.5, y - 0.5)
        c.paint(np.clip((0.3 - d) * 40, 0, 1), (220, 216, 196))
        c.add(-0.5 * glow(0.44, 0.42, 0.06) - 0.4 * glow(0.6, 0.6, 0.05), (90, 90, 90))
        c.add(0.4 * ring(0.5, 0.5, 0.33, 0.04), (200, 200, 160))
    elif n == 8:  # Berserker: a red starburst
        c.add(1.5 * star(0.5, 0.5, 8, 0.45, 8), (255, 60, 50))
        c.add(glow(0.5, 0.5, 0.08), (255, 230, 220))
    elif n == 9:  # Energy Shield: a ring of blue light
        c.add(1.4 * ring(0.5, 0.5, 0.3, 0.05), (80, 160, 255))
        c.add(0.5 * glow(0.5, 0.5, 0.25), (40, 80, 200))
    elif n == 10:  # Earth Spears: brown spikes
        for cx, top in ((0.3, 0.3), (0.5, 0.15), (0.7, 0.35)):
            spike = np.clip(1 - np.abs(x - cx) / (0.09 * np.clip((y - top) / 0.6, 0, 1) + 1e-3), 0, 1) * (y > top)
            c.paint(spike * (y < 0.9), (180, 120, 70))
    elif n == 11:  # Flame Strike: three slashes of fire
        for k in range(3):
            c.add(1.3 * stroke(0.25 + 0.15 * k, 0.15, 0.4 + 0.15 * k, 0.85, 0.035), (255, 140, 40))
    elif n == 12:  # Dread Deathscythe: a violet crescent
        c.add(1.6 * ring(0.5, 0.5, 0.3, 0.05) * (x < 0.62), (170, 80, 230))
        c.add(stroke(0.3, 0.85, 0.7, 0.15, 0.02), (120, 100, 90))
    elif n == 13:  # Lightning Storm: forked bolts
        pts = [(0.55, 0.1), (0.4, 0.45), (0.6, 0.5), (0.42, 0.9)]
        for (x0, y0), (x1, y1) in zip(pts, pts[1:]):
            c.add(1.5 * stroke(x0, y0, x1, y1, 0.03), (250, 240, 120))
        c.add(0.8 * stroke(0.4, 0.45, 0.2, 0.7, 0.02), (250, 240, 160))
    elif n == 14:  # Medusa: a grey stone eye
        eye = np.clip(1 - np.hypot((x - 0.5) / 0.38, (y - 0.5) / 0.2), 0, 1)
        c.paint(np.clip(eye * 6, 0, 1), (170, 168, 160))
        c.paint(np.clip((0.08 - np.hypot(x - 0.5, y - 0.5)) * 50, 0, 1), (30, 30, 30))
    elif n == 15:  # Sonic Blade: a white crescent wave
        c.add(1.6 * ring(0.35, 0.5, 0.35, 0.04) * (x > 0.45), (230, 236, 255))
        c.add(0.7 * ring(0.25, 0.5, 0.35, 0.03) * (x > 0.38), (150, 170, 220))
    elif n == 16:  # Mud Javelin: a brown spear of earth
        c.add(1.3 * stroke(0.15, 0.85, 0.8, 0.2, 0.05), (150, 100, 50))
        c.add(0.8 * glow(0.8, 0.2, 0.08), (220, 170, 90))
    elif n == 17:  # Identify: a blue eye of light
        c.add(1.3 * ring(0.5, 0.5, 0.28, 0.03, 0.55), (90, 170, 255))
        c.add(1.2 * glow(0.5, 0.5, 0.08), (200, 230, 255))
    elif n == 18:  # Magic Shield: rings within rings
        for r in (0.12, 0.22, 0.32):
            c.add(1.2 * ring(0.5, 0.5, r, 0.025), (60, 200, 230))
    elif n == 19:  # Counter Burst: a gold ring throwing sparks
        c.add(1.3 * ring(0.5, 0.5, 0.25, 0.04), (255, 200, 80))
        c.add(0.9 * star(0.5, 0.5, 12, 0.45, 12), (255, 170, 60))
    elif n == 20:  # Explosion: a blast
        c.add(1.2 * star(0.5, 0.55, 10, 0.45, 4, 0.3), (255, 120, 40))
        c.add(glow(0.5, 0.55, 0.15), (255, 240, 160))
    elif n == 21:  # Elemental Strike: a four-coloured star
        for k, colour in enumerate(((255, 110, 40), (90, 170, 255), (170, 130, 70), (250, 240, 110))):
            a = k * np.pi / 2
            c.add(1.3 * stroke(0.5, 0.5, 0.5 + 0.38 * np.cos(a), 0.5 + 0.38 * np.sin(a), 0.05), colour)
        c.add(glow(0.5, 0.5, 0.1), (255, 255, 255))
    return c


def attack_icon():
    c = Canvas((18, 30, 60))
    c.paint(np.clip(stroke(0.2, 0.8, 0.78, 0.22, 0.04) * 3, 0, 1), (210, 214, 224))  # the blade
    c.paint(np.clip(stroke(0.24, 0.62, 0.38, 0.76, 0.03) * 3, 0, 1), (160, 124, 66))  # the guard
    c.paint(np.clip(stroke(0.12, 0.88, 0.24, 0.76, 0.035) * 3, 0, 1), (90, 60, 40))  # the grip
    return c


def framed(img, frame):
    """An icon in a bronze frame of `frame` pixels."""
    w = img.width + 2 * frame
    out = Image.new("RGBA", (w, w), (0, 0, 0, 255))
    for k in range(frame):
        shade = (176, 140, 80, 255) if k == 0 else (90, 70, 40, 255)
        for i in range(k, w - k):
            for p in ((i, k), (i, w - 1 - k), (k, i), (w - 1 - k, i)):
                out.putpixel(p, shade)
    out.alpha_composite(img, (frame, frame))
    return out


def empty(size, frame):
    img = Image.new("RGBA", (size - 2 * frame, size - 2 * frame), (34, 30, 26, 255))
    return framed(img, frame)


def main():
    out = Path(sys.argv[1]) / "System/Game/Pattern"
    spells = [spell_icon(n) for n in range(22)]
    big = [framed(attack_icon().image(22), 2), empty(26, 2)] + [framed(s.image(22), 2) for s in spells]
    small = [empty(16, 1), empty(16, 1), framed(attack_icon().image(14), 1), empty(16, 1)]
    small += [framed(s.image(14), 1) for s in spells]
    for name, icons, comment in (("MagicIcon", big, "The chosen icon on the magic bar: attack, empty, then the 22 spells."),
                                 ("MagicBarIcon", small, "The magic bar's other icons: two plain, attack, empty, then the 22 spells.")):
        size = icons[0].width
        sheet = Image.new("RGBA", (size * 8, size * ((len(icons) + 7) // 8)), (0, 0, 0, 0))
        frames = []
        for k, icon in enumerate(icons):
            px, py = (k % 8) * size, (k // 8) * size
            sheet.alpha_composite(icon, (px, py))
            frames.append((px, py, size, size, 0, 0))
        write_sheet(out, name, sheet, frames, comment)
        sheet_file = out / f"{name}.sheet"
        sheet_file.write_text(sheet_file.read_text().replace("tools/art/ui.py", "tools/art/magic_icons.py"))
    print("wrote the magic bar's icons")


if __name__ == "__main__":
    main()
