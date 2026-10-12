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


def crumbs(rng, res, per_tile, keep, sharp=2.5):
    """A wrapping res x res height field of rounded lumps, PERIOD tiles
    across: one jittered point per grid cell (per_tile cells to a tile),
    height = distance to the cell's border, and only `keep` of the cells
    raised."""
    g = PERIOD * per_tile
    pts = (np.mgrid[0:g, 0:g].transpose(1, 2, 0) + rng.uniform(0.15, 0.85, (g, g, 2))) / g
    raised = rng.random((g, g)) < keep
    yy, xx = (np.mgrid[0:res, 0:res] + 0.5) / res
    cy, cx = np.floor(yy * g).astype(int), np.floor(xx * g).astype(int)
    d, idx = [], []
    for dy in (-1, 0, 1):
        for dx in (-1, 0, 1):
            ny, nx = (cy + dy) % g, (cx + dx) % g
            py = pts[ny, nx, 0] + np.floor((cy + dy) / g)
            px = pts[ny, nx, 1] + np.floor((cx + dx) / g)
            d.append((yy - py) ** 2 + (xx - px) ** 2)
            idx.append(raised[ny, nx])
    d, idx = np.stack(d), np.stack(idx)
    order = np.argsort(d, axis=0)
    d = np.take_along_axis(d, order, 0)
    up = np.take_along_axis(idx, order[:1], 0)[0]
    h = np.clip((np.sqrt(d[1]) - np.sqrt(d[0])) * g * sharp, 0, 1) ** 0.5
    return h * up


def plates(rng, res, per_tile):
    """Flat stone plates: a wrapping res x res grid of Voronoi cells
    (per_tile to a tile edge, PERIOD tiles across). Returns each pixel's
    distance to its cell's border (in cell sizes) and its cell's random
    shade (-1 to 1)."""
    g = PERIOD * per_tile
    pts = (np.mgrid[0:g, 0:g].transpose(1, 2, 0) + rng.uniform(0.1, 0.9, (g, g, 2))) / g
    shade = rng.uniform(-1, 1, (g, g))
    yy, xx = (np.mgrid[0:res, 0:res] + 0.5) / res
    cy, cx = np.floor(yy * g).astype(int), np.floor(xx * g).astype(int)
    d, val = [], []
    for dy in (-1, 0, 1):
        for dx in (-1, 0, 1):
            ny, nx = (cy + dy) % g, (cx + dx) % g
            py = pts[ny, nx, 0] + np.floor((cy + dy) / g)
            px = pts[ny, nx, 1] + np.floor((cx + dx) / g)
            d.append((yy - py) ** 2 + (xx - px) ** 2)
            val.append(shade[ny, nx])
    d, val = np.stack(d), np.stack(val)
    order = np.argsort(d, axis=0)
    d = np.take_along_axis(d, order, 0)
    return (np.sqrt(d[1]) - np.sqrt(d[0])) * g, np.take_along_axis(val, order[:1], 0)[0]


class Ground:
    RES = 128  # samples per tile of the crumb relief

    def __init__(self, seed=1):
        self.big = Field([(0.9, 1.0), (0.35, 0.6)], seed)        # blotches
        self.fine = Field([(0.03, 1.0), (0.012, 0.8)], seed + 1)  # grain
        self.edge = Field([(0.25, 1.0), (0.08, 0.5)], seed + 2)  # ragged borders
        self.tuft = Field([(0.06, 1.0)], seed + 3)               # grass tufts
        self.rng = np.random.default_rng(seed + 4)               # per-pixel speckle
        self._relief = None
        self.seed = seed

    def relief(self):
        """The dirt's crumbly relief, built on first use: (height, light)
        grids. Lit from the top of the screen, each crumb has a bright
        upper rim and a dark shadow below."""
        if self._relief is None:
            rng = np.random.default_rng(self.seed + 7)
            res = self.RES * PERIOD
            h = np.zeros((res, res))
            for per_tile, keep in ((9, 0.45), (16, 0.55), (26, 0.6)):
                h = np.maximum(h, crumbs(rng, res, per_tile, keep) * (0.55 + 0.45 * 9 / per_tile))
            # Screen up is -x -y in world space.
            light = np.roll(h, (1, 1), (0, 1)) - np.roll(h, (-1, -1), (0, 1))
            self._relief = h, light
        return self._relief

    def sample(self, grid, u, v):
        res = grid.shape[0]
        x = (np.asarray(u) % PERIOD) / PERIOD * res
        y = (np.asarray(v) % PERIOD) / PERIOD * res
        return grid[y.astype(int) % res, x.astype(int) % res]

    def texture(self, u, v):
        """Grass and dirt colours and the edge noise at tile coordinates."""
        big = self.big(u, v)
        # Speckle: mostly per-pixel noise, with a little of the finest field.
        fine = 0.85 * self.rng.standard_normal(np.shape(u)) + 0.5 * self.fine(u, v)
        # Dark earth broken into crumbs and clods: lit rims, dark hollows
        # between them, and broad darker and lighter patches.
        height, light = self.relief()
        h, l = self.sample(height, u, v), self.sample(light, u, v)
        shade = np.clip(1.8 * l, -1, 1) * 0.42 + 0.05 * big + 0.15 * (h - 0.35) + 0.1 * self.fine(u + 1.7, v + 0.9)
        dirt = colour(np.array([46, 33, 18.0]), np.array([20, 13, 6.0]), np.array([82, 66, 48.0]),
                      shade, fine, 0.24)
        # Hollows between the clods, dark and a little deeper in damp patches.
        hollow = np.clip(0.18 - h, 0, 0.18) / 0.18 * np.clip(0.6 - 0.4 * big, 0, 1)
        dirt = dirt * (1 - 0.45 * hollow)[..., None]
        tuft = np.clip(self.tuft(u, v) * 0.8, -1, 1)
        # Grass: an even green lawn, finely grained, with only gentle patches.
        # Blades: speckle drawn out sideways, as the original's lawns are.
        blades = self.rng.standard_normal(np.shape(u))
        blades = (blades + np.roll(blades, 1, -1) + np.roll(blades, -1, -1) + 0.5 * np.roll(blades, 2, -1)) / 1.9
        grass = colour(np.array([44, 53, 15.0]), np.array([22, 30, 6.0]), np.array([64, 76, 22.0]),
                       0.1 * big + 0.2 * tuft, 0.6 * blades + 0.55 * fine, 0.26)
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
    # Brick colour (mid, dark, light), mortar and rubble for each kind.
    KINDS = {"dungeon": ((104, 88, 60), (60, 50, 34), (146, 128, 96), (34, 28, 18), (96, 84, 58)),
             "fort": ((106, 100, 84), (64, 60, 50), (138, 132, 112), (36, 34, 28), (80, 76, 64))}

    def __init__(self, seed=1, kind="dungeon"):
        super().__init__(seed)
        self.palette = [np.array(c, dtype=float) for c in self.KINDS[kind]]
        self.kind = kind
        self.pitting = 0.07
        if kind == "fort":
            self.pitting = 0.12  # more pitted paving stones
        else:
            # The dungeon's bricks are bigger and laid in staggered rows
            # (see bond()), with thin soft joints and no pits.
            self.pitting = -1.0
        self.wear = Field([(0.5, 1.0), (0.15, 0.7)], seed + 5)  # dust and damp
        self.grime = Field([(0.4, 1.0), (0.15, 0.8), (0.05, 0.4)], seed + 8)  # smudges of dirt

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
        m = 0.09 if self.kind == "fort" else 0.07
        mortar = np.where(flat, (fy < m) | (fy > 1 - m) | ((t == 0) & (fx < m)) | ((t == 1) & (fx > 1 - m)),
                          (fx < m) | (fx > 1 - m) | ((t == 2) & (fy < m)) | ((t == 3) & (fy > 1 - m)))
        return shade, mortar.astype(float)

    def bond(self, u, v, length=1 / 3, width=1 / 6):
        """Brick shade (-1 to 1) and a 0-1 mortar mask for bricks laid in
        rows along x, each row shifted half a brick. Bricks are `length` by
        `width` tiles, each a whole number to the period."""
        x, y = np.asarray(u) / length, np.asarray(v) / width
        j = np.floor(y).astype(int)
        x = x + 0.5 * (j % 2)
        i = np.floor(x).astype(int)
        fx, fy = x - i, y - j
        h = (i * 73856093 ^ j * 19349663) & 0xFFFF
        shade = h / 0x7FFF - 1
        mx, my = 0.06, 0.1
        mortar = (fx < mx) | (fx > 1 - mx) | (fy < my) | (fy > 1 - my)
        return shade, mortar.astype(float)

    def texture(self, u, v):
        big = self.big(u, v)
        wear = self.wear(u, v)
        fine = 0.85 * self.rng.standard_normal(np.shape(u)) + 0.5 * self.fine(u, v)
        shade, mortar = self.bricks(u, v) if self.kind == "fort" else self.bond(u, v)
        mid, dark, light, joint_colour, rubble_colour = self.palette
        if self.kind == "fort":
            brick = colour(mid, dark, light, 0.45 * shade + 0.15 * big, fine, 0.12)
            # Dust lightens some stretches and damp darkens others.
            brick = brick * (1 + np.clip(0.18 * wear, -0.08, 0.25))[..., None]
            joint = joint_colour * (1 + 0.2 * fine[..., None])
        else:
            # Flat, evenly worn bricks under broad smudges of grime: dark
            # damp stains and paler dusty patches running across the joints.
            grime = self.grime(u, v)
            brick = colour(mid, dark, light, 0.55 * shade * np.abs(shade) + 0.1 * big, fine, 0.16)
            brick = brick * (1 + np.clip(0.2 * grime, -0.2, 0.18))[..., None]
            joint = brick * 0.6
        floor = brick * (1 - mortar[..., None]) + joint * mortar[..., None]
        # Pits and chips: scattered near-black specks, thicker where it is damp.
        pits = self.rng.random(np.shape(u)) < self.pitting + 0.05 * np.clip(-wear, 0, 1)
        floor = np.where(pits[..., None], floor * 0.3, floor)
        rubble = colour(rubble_colour, rubble_colour * 0.6, rubble_colour * 1.38, 0.25 * big, fine, 0.32)
        return floor, rubble, self.edge(u, v)


class Wild(Ground):
    """Open country for the third area. "shore": sand with pools of water
    where the corners are 0; "desert": sand with patches of brown earth;
    "heath": olive grass with patches of dark soil; "coast": grass giving
    way to sand. Grass and sand are the same in every kind, so the kinds
    can sit side by side on one map."""

    # (mid, dark, light, speckle) for the corner-1 and corner-0 surfaces.
    KINDS = {"shore": (((150, 122, 82), (112, 88, 56), (182, 156, 112), 0.20),
                       ((38, 86, 146), (20, 52, 100), (70, 124, 184), 0.14)),
             "desert": (((150, 122, 82), (112, 88, 56), (182, 156, 112), 0.20),
                        ((92, 66, 42), (62, 44, 28), (120, 90, 60), 0.26)),
             "heath": (((50, 55, 16), (32, 37, 9), (68, 73, 25), 0.24),
                       ((40, 32, 26), (24, 19, 15), (58, 47, 38), 0.24)),
             "coast": (((50, 55, 16), (32, 37, 9), (68, 73, 25), 0.24),
                       ((150, 122, 82), (112, 88, 56), (182, 156, 112), 0.20))}

    def __init__(self, seed=1, kind="shore"):
        super().__init__(seed)
        self.kind = kind
        self.surfaces = [([np.array(c, dtype=float) for c in spec[:3]], spec[3]) for spec in self.KINDS[kind]]
        self.ripple = Field([(0.05, 1.0), (0.02, 0.6)], seed + 6)

    def texture(self, u, v):
        big = self.big(u, v)
        fine = 0.85 * self.rng.standard_normal(np.shape(u)) + 0.5 * self.fine(u, v)
        (a, ga), (b, gb) = self.surfaces
        one = colour(*a, 0.25 * big, fine, ga)
        if self.kind == "shore":
            # Water: smoother, with soft ripples instead of grit.
            zero = colour(*b, 0.2 * big + 0.5 * self.ripple(u, v), fine * 0.3, gb)
        else:
            zero = colour(*b, 0.25 * big, fine, gb)
        return one, zero, self.edge(u, v)


class Cave(Ground):
    """A cave or mine floor. "cave": packed earth broken into flat cracked
    slabs, with loose gravel where the corners are 0. "mine": dark trodden
    earth with patches of coal dust. "deep": the same lumpy relief in a deep
    cavern's dark brown earth, with patches of mossy ground."""

    def __init__(self, seed=1, kind="cave"):
        super().__init__(seed)
        self.kind = kind
        # A wrapping height field of rounded clods: one jittered point per
        # grid cell, height = distance to the cell's border, and only some
        # cells raised. Two sizes are layered. Lit from the top of the
        # screen, it gives the floor its lumpy relief.
        rng = np.random.default_rng(seed + 6)
        self.res = res = 64 * PERIOD
        h = np.zeros((res, res))
        for per_tile, keep in ((3, 0.35), (6, 0.45), (11, 0.5)):
            h = np.maximum(h, self.clods(rng, PERIOD * per_tile, keep) * (0.6 + 0.4 * 3 / per_tile))
        self.lump = h
        self.light = np.roll(h, (2, 2), (0, 1)) - np.roll(h, (-1, -1), (0, 1))

    def clods(self, rng, g, keep):
        res = self.res
        pts = (np.mgrid[0:g, 0:g].transpose(1, 2, 0) + rng.uniform(0.15, 0.85, (g, g, 2))) / g
        raised = rng.random((g, g)) < keep
        yy, xx = (np.mgrid[0:res, 0:res] + 0.5) / res
        cy, cx = np.floor(yy * g).astype(int), np.floor(xx * g).astype(int)
        d, idx = [], []
        for dy in (-1, 0, 1):
            for dx in (-1, 0, 1):
                ny, nx = (cy + dy) % g, (cx + dx) % g
                py = pts[ny, nx, 0] + np.floor((cy + dy) / g)
                px = pts[ny, nx, 1] + np.floor((cx + dx) / g)
                d.append((yy - py) ** 2 + (xx - px) ** 2)
                idx.append(raised[ny, nx])
        d, idx = np.stack(d), np.stack(idx)
        order = np.argsort(d, axis=0)
        d = np.take_along_axis(d, order, 0)
        up = np.take_along_axis(idx, order[:1], 0)[0]
        h = np.clip((np.sqrt(d[1]) - np.sqrt(d[0])) * g * 2.5, 0, 1) ** 0.5
        return h * up

    def texture(self, u, v):
        big = self.big(u, v)
        fine = 0.85 * self.rng.standard_normal(np.shape(u)) + 0.5 * self.fine(u, v)
        if self.kind == "mine":
            earth = colour(np.array([62, 46, 30.0]), np.array([38, 28, 17.0]), np.array([84, 64, 42.0]),
                           0.2 * big, fine, 0.26)
            coal = colour(np.array([30, 26, 24.0]), np.array([14, 12, 11.0]), np.array([50, 44, 40.0]),
                          0.3 * big, fine, 0.35)
            return earth, coal, self.edge(u, v)
        lump, light = self.sample(self.lump, u, v), self.sample(self.light, u, v)
        if self.kind == "deep":
            earth = colour(np.array([38, 26, 18.0]), np.array([12, 8, 6.0]), np.array([72, 52, 38.0]),
                           np.clip(2.0 * light, -1, 1) * 0.6 + 0.15 * big, fine, 0.18)
            earth = earth * (0.7 + 0.3 * lump)[..., None]
            moss = colour(np.array([32, 31, 21.0]), np.array([13, 13, 9.0]), np.array([50, 50, 34.0]),
                          np.clip(2.0 * light, -1, 1) * 0.5 + 0.2 * big, fine, 0.3)
            return earth, moss * (0.75 + 0.25 * lump)[..., None], self.edge(u, v)
        # Chunks of flat rock with bevelled edges, lit from the top of the
        # screen; a few lower chunks between them sit in shadow.
        if not hasattr(self, "chunk"):
            edge, val = plates(np.random.default_rng(self.seed + 9), 128 * PERIOD, 9)
            # Each chunk stands at its own height; only steps between
            # neighbours show, as a lit lip above and a shadow below.
            level = np.clip((val + 0.6) * 2.5, 0, 1)
            h = level * (0.75 + 0.25 * np.clip(edge / 0.12, 0, 1))
            lit = np.roll(h, (2, 2), (0, 1)) - np.roll(h, (-2, -2), (0, 1))
            self.chunk = h, val, lit
        # Wobble the lookup so the chunks' sides are ragged, not straight.
        wu, wv = u + 0.035 * self.edge(u * 4, v * 4), v + 0.035 * self.edge(v * 4 + 1.3, u * 4 + 0.7)
        h, val, lit = (self.sample(g, wu, wv) for g in self.chunk)
        rock = colour(np.array([90, 75, 58.0]), np.array([46, 37, 28.0]), np.array([136, 118, 94.0]),
                      0.5 * np.clip(1.5 * lit, -1, 1) + 0.1 * val + 0.12 * big, fine, 0.1)
        rock = rock * (0.7 + 0.3 * h)[..., None]
        gravel = colour(np.array([72, 60, 48.0]), np.array([40, 33, 26.0]), np.array([104, 90, 72.0]),
                        0.2 * big, fine, 0.40)
        return rock, gravel, self.edge(u, v)


def corner_patterns():
    """All 16 corner combinations, as (top-left, top-right, bottom-left, bottom-right)."""
    return [((k >> 0) & 1, (k >> 1) & 1, (k >> 2) & 1, (k >> 3) & 1) for k in range(16)]


def pattern_index(corners, tx, ty):
    """Frame number of a tile in the sheet build_tiles() writes."""
    k = corners[0] | corners[1] << 1 | corners[2] << 2 | corners[3] << 3
    return (k * PERIOD + ty % PERIOD) * PERIOD + tx % PERIOD


def build_tiles(seed=1, kind="meadow"):
    """Every tile, in pattern_index order: (image, anchor x, anchor y).
    kind is "meadow" (grass and dirt), "dungeon" (brick floor and rubble),
    "fort" (grey stone paving and grit), "shore" (sand and water),
    "desert" (sand and brown earth), "heath" (grass and dark soil),
    "coast" (grass and sand), "deep" (dark cavern earth and moss),
    "cave" (cracked rock and gravel) or "mine" (earth and coal dust)."""
    g = {"dungeon": lambda: Floor(seed), "fort": lambda: Floor(seed, "fort"), "cave": lambda: Cave(seed, "cave"),
         "mine": lambda: Cave(seed, "mine"), "shore": lambda: Wild(seed, "shore"),
         "desert": lambda: Wild(seed, "desert"), "heath": lambda: Wild(seed, "heath"),
         "coast": lambda: Wild(seed, "coast"), "deep": lambda: Cave(seed, "deep")}.get(kind, lambda: Ground(seed))()
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
