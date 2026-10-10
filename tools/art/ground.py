"""Ground tiles in ShadowFlare's style, drawn procedurally.

Grass and dirt textures are noise fields that repeat every PERIOD tiles,
so neighbouring tiles always join up; each tile samples the texture at its
own position within that period. Where grass meets dirt, the tile blends
the two along a ragged edge decided by which of its four corners are grass
(16 corner patterns), and that edge is also computed in world space, so it
continues across tile borders.

Tiles are TILE world units square and drawn for the original's 15/10
projection, so each is a 96x64 pixel diamond.
"""

import numpy as np
from PIL import Image

TILE = 320        # world units per tile edge
PROJ = (15, 10)   # screen pixels per 100 world units
PERIOD = 4        # textures repeat every PERIOD tiles
HALF_W = TILE * PROJ[0] // 100  # 48: half the diamond's width
HALF_H = TILE * PROJ[1] // 100  # 32


def periodic_noise(size, scales, seed):
    """A size x size field that wraps around, summed from blurred noise."""
    rng = np.random.default_rng(seed)
    out = np.zeros((size, size))
    f = np.fft.fftfreq(size)
    fx, fy = np.meshgrid(f, f)
    r2 = fx ** 2 + fy ** 2
    for scale, weight in scales:
        white = rng.standard_normal((size, size))
        blur = np.exp(-r2 * (np.pi * scale) ** 2 * 2)
        layer = np.real(np.fft.ifft2(np.fft.fft2(white) * blur))
        out += weight * layer / (layer.std() + 1e-9)
    return out


class Field:
    """A wrapping noise field addressed in tile units."""

    def __init__(self, scales, seed, res=64):
        self.res = res
        self.size = PERIOD * res
        self.data = periodic_noise(self.size, [(s * res, w) for s, w in scales], seed)

    def __call__(self, u, v):
        x = (np.asarray(u) * self.res) % self.size
        y = (np.asarray(v) * self.res) % self.size
        x0, y0 = np.floor(x).astype(int), np.floor(y).astype(int)
        fx, fy = x - x0, y - y0
        x1, y1 = (x0 + 1) % self.size, (y0 + 1) % self.size
        d = self.data
        return ((d[y0, x0] * (1 - fx) + d[y0, x1] * fx) * (1 - fy) +
                (d[y1, x0] * (1 - fx) + d[y1, x1] * fx) * fy)


def colour(base, dark, light, t, fine):
    """Mix a base colour towards dark or light by noise t, plus grain."""
    t = np.clip(t, -1, 1)[..., None]
    c = np.where(t < 0, base + (base - dark) * t, base + (light - base) * t)
    return c * (1 + 0.10 * fine[..., None])


class Ground:
    def __init__(self, seed=1):
        self.big = Field([(0.9, 1.0), (0.35, 0.6)], seed)        # blotches
        self.fine = Field([(0.03, 1.0), (0.012, 0.8)], seed + 1)  # grain
        self.edge = Field([(0.25, 1.0), (0.08, 0.5)], seed + 2)  # ragged borders
        self.tuft = Field([(0.06, 1.0)], seed + 3)               # grass tufts

    def texture(self, u, v):
        """Grass and dirt colours and the edge noise at tile coordinates."""
        big, fine = self.big(u, v), self.fine(u, v)
        dirt = colour(np.array([96, 72, 48.0]), np.array([58, 42, 28.0]), np.array([120, 96, 66.0]),
                      0.45 * big, fine)
        tuft = np.clip(self.tuft(u, v) * 0.8, -1, 1)
        grass = colour(np.array([58, 82, 30.0]), np.array([34, 54, 18.0]), np.array([84, 108, 44.0]),
                       0.35 * big + 0.35 * tuft, fine * 1.3)
        return grass, dirt, self.edge(u, v)

    def tile(self, tx, ty, corners):
        """The tile at (tx, ty) of the period; corners = grass at
        (top-left, top-right, bottom-left, bottom-right) in tile space."""
        w, h = 2 * HALF_W, 2 * HALF_H
        px, py = np.meshgrid(np.arange(w) - HALF_W + 0.5, np.arange(h) - HALF_H + 0.5)
        # Screen offset from the tile centre to world, in tiles.
        a = px * 100 / PROJ[0] / TILE   # x - y
        b = py * 100 / PROJ[1] / TILE   # x + y
        u = (a + b) / 2 + 0.5
        v = (b - a) / 2 + 0.5
        inside = (u >= 0) & (u < 1) & (v >= 0) & (v < 1)
        grass, dirt, edge = self.texture(u + tx, v + ty)
        c00, c10, c01, c11 = corners
        mix = (c00 * (1 - u) * (1 - v) + c10 * u * (1 - v) + c01 * (1 - u) * v + c11 * u * v)
        # A ragged, slightly soft border around the 0.5 level.
        t = np.clip((mix - 0.5 + 0.22 * edge * (mix * (1 - mix) * 4)) * 9 + 0.5, 0, 1)[..., None]
        rgb = np.clip(dirt * (1 - t) + grass * t, 0, 255)
        alpha = np.where(inside, 255, 0)[..., None]
        return Image.fromarray(np.concatenate([rgb, alpha], axis=2).astype(np.uint8), "RGBA")


def corner_patterns():
    """All 16 corner combinations, as (top-left, top-right, bottom-left, bottom-right)."""
    return [((k >> 0) & 1, (k >> 1) & 1, (k >> 2) & 1, (k >> 3) & 1) for k in range(16)]


def pattern_index(corners, tx, ty):
    """Frame number of a tile in the sheet build_tiles() writes."""
    k = corners[0] | corners[1] << 1 | corners[2] << 2 | corners[3] << 3
    return (k * PERIOD + ty % PERIOD) * PERIOD + tx % PERIOD


def build_tiles(seed=1):
    """Every tile, in pattern_index order: (image, anchor x, anchor y)."""
    g = Ground(seed)
    tiles = []
    for corners in corner_patterns():
        for ty in range(PERIOD):
            for tx in range(PERIOD):
                tiles.append((g.tile(tx, ty, corners), HALF_W, HALF_H))
    return tiles


if __name__ == "__main__":
    # Preview: a patch of ground with a dirt clearing in grass.
    import sys
    size = 10
    corner = np.ones((size + 1, size + 1), dtype=int)
    corner[3:8, 2:7] = 0
    corner[5:7, 6:10] = 0
    tiles = build_tiles()
    out = Image.new("RGBA", ((size + 2) * 2 * HALF_W, (size + 2) * 2 * HALF_H), (0, 0, 0, 255))
    ox = (size + 1) * HALF_W
    for ty in range(size):
        for tx in range(size):
            cs = (corner[ty, tx], corner[ty, tx + 1], corner[ty + 1, tx], corner[ty + 1, tx + 1])
            im, ax, ay = tiles[pattern_index(cs, tx, ty)]
            cx, cy = ox + (tx - ty) * HALF_W, (tx + ty + 1) * HALF_H
            out.alpha_composite(im, (cx - ax, cy - ay))
    out.save(sys.argv[1] if len(sys.argv) > 1 else "ground_preview.png")
