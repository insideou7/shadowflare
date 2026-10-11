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


def colour(base, dark, light, t, fine, grain):
    """Mix a base colour towards dark or light by noise t, plus grain."""
    t = np.clip(t, -1, 1)[..., None]
    c = np.where(t < 0, base + (base - dark) * t, base + (light - base) * t)
    return c * (1 + grain * fine[..., None])


class Ground:
    def __init__(self, seed=1):
        self.big = Field([(0.9, 1.0), (0.35, 0.6)], seed)        # blotches
        self.fine = Field([(0.03, 1.0), (0.012, 0.8)], seed + 1)  # grain
        self.edge = Field([(0.25, 1.0), (0.08, 0.5)], seed + 2)  # ragged borders
        self.tuft = Field([(0.06, 1.0)], seed + 3)               # grass tufts
        self.rng = np.random.default_rng(seed + 4)               # per-pixel speckle

    def texture(self, u, v):
        """Grass and dirt colours and the edge noise at tile coordinates."""
        big = self.big(u, v)
        # Speckle: mostly per-pixel noise, with a little of the finest field.
        fine = 0.85 * self.rng.standard_normal(np.shape(u)) + 0.5 * self.fine(u, v)
        # Dark earth with a strong speckle and only faint patches.
        dirt = colour(np.array([47, 35, 19.0]), np.array([32, 23, 12.0]), np.array([62, 48, 27.0]),
                      0.08 * big, fine, 0.22)
        tuft = np.clip(self.tuft(u, v) * 0.8, -1, 1)
        # Olive grass, finely grained, with gentle lighter and darker patches.
        grass = colour(np.array([50, 55, 16.0]), np.array([32, 37, 9.0]), np.array([68, 73, 25.0]),
                       0.15 * big + 0.15 * tuft, fine, 0.24)
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


class Floor(Ground):
    """A dungeon floor: worn bricks laid in a herringbone, with patches of
    dusty rubble where the corners are 0 (instead of grass and dirt)."""

    BRICK = 0.09  # the short side of a brick, in tiles

    def __init__(self, seed=1):
        super().__init__(seed)
        self.wear = Field([(0.5, 1.0), (0.15, 0.7)], seed + 5)  # dust and damp

    def bricks(self, u, v):
        """Brick shade (-1 to 1) and a 0-1 mortar mask, laid in herringbone:
        pairs of cells form bricks, alternating flat and upright."""
        x, y = np.asarray(u) / self.BRICK, np.asarray(v) / self.BRICK
        i, j = np.floor(x).astype(int), np.floor(y).astype(int)
        fx, fy = x - i, y - j
        t = (i + j) % 4
        flat = t < 2
        # Each brick gets an id from its first cell, and a shade from that.
        bi = np.where(flat, i - t, i)
        bj = np.where(flat, j, j - (t - 2))
        h = (bi * 73856093 ^ bj * 19349663 ^ flat.astype(int) * 83492791) & 0xFFFF
        shade = h / 0x7FFF - 1
        m = 0.09
        mortar = np.where(flat, (fy < m) | (fy > 1 - m) | ((t == 0) & (fx < m)) | ((t == 1) & (fx > 1 - m)),
                          (fx < m) | (fx > 1 - m) | ((t == 2) & (fy < m)) | ((t == 3) & (fy > 1 - m)))
        return shade, mortar.astype(float)

    def texture(self, u, v):
        big = self.big(u, v)
        wear = self.wear(u, v)
        fine = 0.85 * self.rng.standard_normal(np.shape(u)) + 0.5 * self.fine(u, v)
        shade, mortar = self.bricks(u, v)
        brick = colour(np.array([104, 88, 60.0]), np.array([60, 50, 34.0]), np.array([146, 128, 96.0]),
                       0.45 * shade + 0.15 * big, fine, 0.12)
        # Dust lightens some stretches and damp darkens others.
        brick = brick * (1 + np.clip(0.18 * wear, -0.08, 0.25))[..., None]
        joint = np.array([34, 28, 18.0]) * (1 + 0.2 * fine[..., None])
        floor = brick * (1 - mortar[..., None]) + joint * mortar[..., None]
        # Pits and chips: scattered near-black specks, thicker where it is damp.
        pits = self.rng.random(np.shape(u)) < 0.07 + 0.05 * np.clip(-wear, 0, 1)
        floor = np.where(pits[..., None], floor * 0.3, floor)
        rubble = colour(np.array([96, 84, 58.0]), np.array([58, 50, 34.0]), np.array([132, 118, 86.0]),
                        0.25 * big, fine, 0.32)
        return floor, rubble, self.edge(u, v)


def corner_patterns():
    """All 16 corner combinations, as (top-left, top-right, bottom-left, bottom-right)."""
    return [((k >> 0) & 1, (k >> 1) & 1, (k >> 2) & 1, (k >> 3) & 1) for k in range(16)]


def pattern_index(corners, tx, ty):
    """Frame number of a tile in the sheet build_tiles() writes."""
    k = corners[0] | corners[1] << 1 | corners[2] << 2 | corners[3] << 3
    return (k * PERIOD + ty % PERIOD) * PERIOD + tx % PERIOD


def build_tiles(seed=1, kind="meadow"):
    """Every tile, in pattern_index order: (image, anchor x, anchor y).
    kind is "meadow" (grass and dirt) or "dungeon" (brick floor and rubble)."""
    g = Floor(seed) if kind == "dungeon" else Ground(seed)
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
    tiles = build_tiles(kind=sys.argv[2] if len(sys.argv) > 2 else "meadow")
    out = Image.new("RGBA", ((size + 2) * 2 * HALF_W, (size + 2) * 2 * HALF_H), (0, 0, 0, 255))
    ox = (size + 1) * HALF_W
    for ty in range(size):
        for tx in range(size):
            cs = (corner[ty, tx], corner[ty, tx + 1], corner[ty + 1, tx], corner[ty + 1, tx + 1])
            im, ax, ay = tiles[pattern_index(cs, tx, ty)]
            cx, cy = ox + (tx - ty) * HALF_W, (tx + ty + 1) * HALF_H
            out.alpha_composite(im, (cx - ax, cy - ay))
    out.save(sys.argv[1] if len(sys.argv) > 1 else "ground_preview.png")
