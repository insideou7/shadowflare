#!/usr/bin/env python3
"""Paint the wilds' cave entrance as a flat picture, the way the original's
map pieces look: a hump of crumbly earth rising gently from the left to a
rounded top and dropping steeply on the right, with a small arched black
opening in its face, its edges fading into the ground around it.

    python3 tools/art/cave_mouth_2d.py <out folder>

Writes <out>/cave_mouth.png (384 x 384) and adds its anchor to
<out>/wilds.txt, for tools/art/add_props.py. Needs numpy and Pillow.
"""

import sys
from pathlib import Path

import numpy as np
from PIL import Image

SIZE = 384
ANCHOR = (192, 278)  # the map position: the middle of the hill's foot
EARTH = np.array([86, 64, 38.0])  # lit earth
SHADE = np.array([26, 19, 10.0])  # earth in shadow
LIGHT = np.array([-0.55, -0.65, 0.55])  # from the top left of the screen


def blur(a, r):
    """Gaussian blur of a float array, r pixels across."""
    x = np.arange(-int(3 * r) - 1, int(3 * r) + 2)
    k = np.exp(-x * x / (2 * r * r))
    k /= k.sum()
    a = np.apply_along_axis(lambda v: np.convolve(v, k, "same"), 0, a)
    return np.apply_along_axis(lambda v: np.convolve(v, k, "same"), 1, a)


def noise(rng, scales):
    """Fractal value noise, -1 to 1: random grids blurred and summed."""
    out = np.zeros((SIZE, SIZE))
    for r, w in scales:
        out += w * blur(rng.standard_normal((SIZE, SIZE)), r) * r
    return out / (np.abs(out).max() + 1e-6)


def smooth(e0, e1, v):
    t = np.clip((v - e0) / (e1 - e0), 0, 1)
    return t * t * (3 - 2 * t)


def paint(rng):
    yy, xx = np.mgrid[0:SIZE, 0:SIZE].astype(float)
    left, right = 94.0, 292.0
    u = (xx - left) / (right - left)
    # The foot: a shallow curve, nearer the viewer in the middle.
    foot = 270 + 6 * (u - 0.5) + 14 * np.sin(np.pi * np.clip(u, 0, 1))
    # The top, as sketched: a long gentle rise, a rounded top, a steep drop.
    rise = smooth(0.0, 0.48, u) ** 0.9 * (1 - smooth(0.72, 0.93, u))
    rough = noise(rng, [(2, 1.0), (5, 0.6)])
    top = foot - 82 * rise - 3 * rough * (rise > 0.05)
    inside = (yy > top) & (yy < foot) & (u > 0) & (u < 1)
    # A rounded height over the shape, for light and shade.
    h = blur(inside.astype(float), 15) * inside
    h = blur(h, 4)
    clods = noise(rng, [(1, 1.0), (2.5, 0.8)])
    hb = h + 0.08 * clods + 0.02 * noise(rng, [(8, 1.0)])
    gy, gx = np.gradient(hb * 110)
    n = np.stack([-gx, -gy, np.ones_like(gx)], -1)
    n /= np.linalg.norm(n, axis=-1, keepdims=True)
    lit = np.clip(n @ (LIGHT / np.linalg.norm(LIGHT)), 0, 1)
    # Lighter higher up, as the sun catches the top; grit and specks.
    down = np.clip((yy - top) / np.maximum(foot - top, 1), 0, 1)  # 0 at the top, 1 at the foot
    tone = np.clip(0.1 + 0.95 * lit + 0.25 * clods - 0.35 * down ** 1.5, 0, 1.15)
    rgb = SHADE + (EARTH - SHADE) * tone[..., None]
    specks = rng.random((SIZE, SIZE)) < 0.02
    rgb[specks] *= rng.uniform(0.6, 1.4, (specks.sum(), 1))
    # The opening: an arch with a rounded top, black inside, its rim lit.
    cx, base, w, ht = 170.0, foot[0, 170] - 5, 30.0, 43.0
    arch_top = base - ht + w / 2
    dx = (xx - cx) / (w / 2)
    in_arch = (np.abs(dx) < 1) & (yy < base) & (
        (yy > arch_top) | ((yy - arch_top) ** 2 + (xx - cx) ** 2 < (w / 2) ** 2))
    in_arch &= (np.abs(dx) < 1) & (yy > arch_top - w / 2)
    soft = blur(in_arch.astype(float), 2.5)
    rim = np.clip(blur(in_arch.astype(float), 6) - soft, 0, 1)
    rgb *= (1 - 0.7 * rim)[..., None]  # the earth darkens round the edge
    rgb = rgb * (1 - soft[..., None]) + np.array([4, 3, 2.0]) * soft[..., None]
    lip = np.clip(blur(((yy < arch_top) & (yy > arch_top - w / 2 - 6) & (np.abs(dx) < 1.3)).astype(float), 3)
                  - soft * 2, 0, 1)
    rgb += 22 * lip[..., None] * (0.5 + 0.5 * clods[..., None])
    # Edges that crumble into the ground, and a soft shadow at the foot.
    alpha = np.clip(blur(inside.astype(float), 1.5) * 1.6 - 0.3 + 0.25 * rough, 0, 1)
    shadow = np.clip(blur(((yy > foot - 6) & (yy < foot + 8) & (u > 0.02) & (u < 0.98)).astype(float), 5), 0, 1)
    shadow *= (1 - alpha)
    out = np.zeros((SIZE, SIZE, 4))
    out[..., :3] = rgb * alpha[..., None]
    out[..., 3] = alpha + 0.45 * shadow
    a = np.maximum(out[..., 3:], 1e-6)
    out[..., :3] = np.where(a > 1e-5, out[..., :3] / a, 0)
    out[..., 3] = np.clip(out[..., 3], 0, 1) * 255
    return Image.fromarray(np.clip(out, 0, 255).astype(np.uint8), "RGBA")


def main():
    main_into(Path(sys.argv[1]))


def main_into(out):
    out.mkdir(parents=True, exist_ok=True)
    paint(np.random.default_rng(7)).save(out / "cave_mouth.png")
    listing = out / "wilds.txt"
    anchors = dict(l.split(" ", 1) for l in listing.read_text().split("\n") if l) if listing.exists() else {}
    anchors["cave_mouth"] = f"{ANCHOR[0]} {ANCHOR[1]}"
    listing.write_text("".join(f"{k} {v}\n" for k, v in anchors.items()))
    print("cave_mouth painted")


if __name__ == "__main__":
    main()
