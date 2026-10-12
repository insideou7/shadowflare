#!/usr/bin/env python3
"""Lay out the open content's first area on the routes of the original's:
the opening fields, Remote Town, the Dusty Ruins, Cold Svalt Town and the
forests and four dungeons round it (29 maps). Needs the player's own copy
of the game.

    python3 tools/content/first_area.py <game folder> <content pack>

Only the shape of each original map is kept: which blocks of tiles (six
tiles square outdoors, four in dungeons) can be walked through, which
blocks are grass or dirt, where the exits lead and where the player
arrives, which monsters and townspeople stand where (with our own
monster stats, from the demo's scenarios), and where its landmarks
(buildings, stairs, fences, pipes) stand, drawn with the props skin.txt
gives them. The walls, paths and forest edges inside those blocks are our
own: outdoors the edges follow our own random noise, dungeons are built
from square rooms, any exit the noise cuts off gets a dirt path, trees
fill the ground that cannot be walked on and walls run along the dungeon
floors. No original picture, tile, sprite or walkability grid is copied;
players who want the original maps exactly play them from their own copy
(content/skin.txt draws them with our art).

Writes Map/<original map name>.txt and Scenario/<10000000 + original
number>/ for each, apart from the demo's scenarios 1 to 7. Exits that lead
out of the first area are left out. Needs Pillow and numpy.
"""

import heapq
import math
import random
import struct
import sys
from pathlib import Path

import numpy as np

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE.parent / "re"))
sys.path.insert(0, str(HERE.parent / "art"))
sys.path.insert(0, str(HERE))
import ground  # noqa: E402
import gnd  # noqa: E402
import obl  # noqa: E402
import render  # noqa: E402
import upd  # noqa: E402
from rclib import unpack_at  # noqa: E402
from skin_preview import read_skin  # noqa: E402

TILE = 320   # world units per tile edge
CELL = 160   # world units per walkability cell, as in the original
PER = TILE // CELL
PROJ = (15, 10)

# The first area: scenario number -> kind of map.
FIELDS = [0, 1, 3, 4, 5, 6, 1000000, 1000001, 1000002, 1000003, 1000004]
DUNGEONS = [10000, 10001, 10002, 10003, 10004, 10005, 1020000, 1020001, 1020002, 1030000, 1030001, 1030002,
            1040000, 1040001, 1040002, 1050000, 1050001, 1050002]

# Original banks whose pieces are landmarks, drawn where the original has
# them, with the props skin.txt gives them (None = every piece of the bank).
LANDMARKS = {
    "00f_city00.njp": None, "00f_city01.njp": None, "00f_obj00.njp": None,
    "00f_obj02.njp": {0, 4, 5, 6, 9},
    "00d_obj00.njp": set(range(0, 24)) | {49, 51, 52, 53, 54, 55, 56},
    "00d_obj01.njp": None,
}
# The village's stone walls have no prop of their own in skin.txt.
EXTRA = {"00f_city02.njp": {0: "pillar", 1: "pillar", 2: "pillar", 3: "pillar", 6: "pillar",
                            4: "wall_x", 5: "wall_y"}}

FOREST = ["tree1", "tree2", "tree3", "tree1", "tree2", "oak"]
FROZEN = ["conifer_blue", "tree_dead", "conifer_green"]   # Frozen Forest
BARE = ["tree_dead", "tree_dead", "oak"]                  # the wastelands

# Banks whose pieces stand on ground that cannot be walked on.
SOLID = {"00f_city00.njp", "00f_city01.njp", "00f_city02.njp"}

# Added to the original scenario numbers, to keep clear of the demo's.
OFFSET = 10000000


def find(folder, stem, ext):
    for p in folder.iterdir():
        if p.stem.lower() == stem.lower() and p.suffix.lower() == ext.lower():
            return p
    return None


# --- Reading the original ----------------------------------------------------

def read_mct(data):
    """Map path, title, placements (objects, people, enemies) and entry
    points of a Scenario.Mct, as in src/formats/scenario.cpp."""
    o = 16

    def i32():
        nonlocal o
        v = struct.unpack_from("<i", data, o)[0]
        o += 4
        return v

    def cstr(n):
        nonlocal o
        s = data[o:o + n].split(b"\0")[0].decode("cp932", "replace")
        o += n
        return s

    cstr(0x104)
    path = cstr(0x104)
    i32(), i32(), i32()
    title = cstr(0x100)
    for _ in range(3):
        n = i32()
        o += 4 * n
    groups = []
    for tail in (13, 11, 79, 4):
        group = []
        for _ in range(i32()):
            c = dict(id=i32(), kind=i32())
            n = i32()
            if n:
                c["name"] = cstr(n)
                i32()
            i32()
            c["x"], c["y"] = i32(), i32()
            c["rect"] = [i32() for _ in range(4)]
            c["direction"] = i32()
            n = i32()
            o += 4 * n
            if i32() == 1:
                n = i32()
                o += 10 * n
            i32()
            c["words"] = [i32() for _ in range(tail)]
            group.append(c)
        groups.append(group)
    entries = [[i32() for _ in range(4)] for _ in range(i32())]
    return path, title, groups, entries


def read_scs(data):
    """Triggers and sentences of a Scenario.Scs (see scs.py)."""
    o = 16

    def i32():
        nonlocal o
        v = struct.unpack_from("<i", data, o)[0]
        o += 4
        return v

    n = i32()
    o += 8 * n
    n = i32()
    o += 8 * n
    for _ in range(i32()):
        i32()
        n = i32()
        o += n
    triggers = [tuple(i32() for _ in range(4)) for _ in range(i32())]
    sentences = []
    for _ in range(i32()):
        commands = []
        for _ in range(i32()):
            op, n = i32(), i32()
            commands.append((op, [(i32(), i32()) for _ in range(n)]))
        sentences.append(commands)
    return triggers, sentences


def exits_of(scs, objects):
    """(left, top, right, bottom, scenario, entry) for each object whose
    ONJUDGE trigger runs CHANGESCENARIO, following IF jumps."""
    triggers, sentences = scs
    by_id = {10000000 + c["id"]: c for c in objects}
    out = []
    for _, event, character, sentence in triggers:
        if event != 3 or character not in by_id:
            continue
        seen, todo, target = set(), [sentence], None
        while todo and target is None:
            s = todo.pop(0)
            if s in seen or not 0 <= s < len(sentences):
                continue
            seen.add(s)
            for op, operands in sentences[s]:
                if op == 17 and len(operands) >= 2:
                    target = (operands[0][1], operands[1][1])
                    break
                todo += [v for t, v in operands if t == 0 and op == 0]
        if target:
            c = by_id[character]
            r = c["rect"]
            out.append((c["x"] + r[0], c["y"] + r[1], c["x"] + r[2], c["y"] + r[3], *target))
    return out


class Original:
    """One original map and scenario: walkability, ground colour, objects."""

    def __init__(self, game, number):
        sc = game / "Scenario" / f"{number:08d}"
        self.path, self.title, self.groups, self.entries = read_mct(find(sc, "Scenario", ".mct").read_bytes())
        self.name = self.path.replace("\\", "/").split("/")[-1].rsplit(".", 1)[0]
        scs = find(sc, "Scenario", ".scs")
        self.exits = exits_of(read_scs(scs.read_bytes()), self.groups[0]) if scs else []
        m = game / "Map"
        d = find(m / "Ground", self.name, ".gnd").read_bytes()
        g = gnd.parse(d)
        self.chips = (g["w"], g["h"], g["chip_w"], g["chip_h"])
        self.planes = g["planes"]
        o = g["rest_offset"]
        width, height = g["w"] * g["chip_w"], g["h"] * g["chip_h"]
        p1, p2 = 15 * g["mag"][0] / 100, 10 * g["mag"][1] / 100

        def jx(sx, sy):
            return math.floor((p1 * sy + p2 * sx) / (2 * p1 * p2))

        def jy(sx, sy):
            return math.floor((p1 * sy - p2 * sx) / (2 * p1 * p2))

        self.jx0, self.jy0 = jx(0, 0) - 1, jy(width, 0) - 1
        jw, jh = jx(width, height) - self.jx0 + 1, jy(0, height) - self.jy0 + 1
        if d[o]:
            raw = unpack_at(d, o + 1)[0]
        else:
            raw = d[o + 1:o + 1 + jw * jh * 2]
        self.judge = np.frombuffer(raw[:jw * jh * 2], "<i2").reshape(jh, jw)
        self.banks = [s.strip() for s in find(m / "Pattern", self.name, ".lst").read_text("latin1").split("\n")]
        self.objects = obl.parse(find(m / "Object", self.name, ".obl").read_bytes())["objects"]
        self._banks, self._chips = {}, {}
        self.pattern_dir = m / "Pattern"

    def bank(self, index):
        if index not in self._banks:
            name = self.banks[index] if 0 <= index < len(self.banks) else "?"
            stem, _, ext = name.rpartition(".")
            p = find(self.pattern_dir, stem, "." + ext) if stem else None
            self._banks[index] = upd.parse(p.read_bytes()) if p else None
        return self._banks[index]

    def chip(self, cx, cy):
        """The RGB picture of ground chip (cx, cy), or None where nothing is drawn."""
        w, h, cw, ch = self.chips
        if not (0 <= cx < w and 0 <= cy < h):
            return None
        i = cy * w + cx
        b, p = self.planes[1][i], self.planes[2][i]
        if b < 0 or p < 0:
            return None
        if (b, p) not in self._chips:
            bank = self.bank(b)
            r = render.render_pattern(bank, p) if bank and p < len(bank["patterns"]) else None
            self._chips[(b, p)] = None if not r else np.frombuffer(bytes(r[2]), np.uint8).reshape(r[1], r[0], 4)
        return self._chips[(b, p)]

    def colour(self, x, y):
        """Average colour of the original ground round world point (x, y),
        or None off the drawn map."""
        sx, sy = (x - y) * PROJ[0] / 100, (x + y) * PROJ[1] / 100
        w, h, cw, ch = self.chips
        total, n = np.zeros(3), 0
        for ox, oy in ((0, 0), (-12, -8), (12, -8), (-12, 8), (12, 8)):
            px, py = int(sx + ox), int(sy + oy)
            pic = self.chip(px // cw, py // ch)
            if pic is None or py % ch >= pic.shape[0] or px % cw >= pic.shape[1]:
                continue
            c = pic[py % ch, px % cw]
            if c[3]:
                total += c[:3]
                n += 1
        return total / n if n else None

    def blocked(self, x, y):
        jx, jy = int(x // CELL) - self.jx0, int(y // CELL) - self.jy0
        if 0 <= jy < self.judge.shape[0] and 0 <= jx < self.judge.shape[1]:
            return bool(self.judge[jy, jx] & 1)
        return True


# --- Laying out our map --------------------------------------------------------

def foot(bank, pattern):
    """The original picture's foot (bottom middle) relative to its object
    position, in screen pixels, as src/game/skin.cpp finds it."""
    r = render.render_pattern(bank, pattern)
    if not r:
        return None
    w, h, rgba = r
    a = np.frombuffer(bytes(rgba), np.uint8).reshape(h, w, 4)[..., 3]
    ys, xs = np.nonzero(a)
    if not len(xs):
        return None
    pat = bank["patterns"][pattern]
    x0, y0 = pat["rect"][0], pat["rect"][1]
    if pat["rect"][2] <= 0 or pat["rect"][3] <= 0:
        es = [e for e in pat["parts"] if 0 <= e["part"] < len(bank["parts"])]
        x0, y0 = min(e["x"] for e in es), min(e["y"] for e in es)
    return x0 + (xs.min() + xs.max() + 1) // 2, y0 + ys.max() + 1


def screen_to_world(sx, sy):
    u, v = sx * 100 / PROJ[0], sy * 100 / PROJ[1]
    return (u + v) / 2, (v - u) / 2


def upsample(coarse, block, shape):
    """Bilinear blow-up of a per-block grid to per-tile, block tiles a block."""
    h, w = shape
    ys = np.clip((np.arange(h) + 0.5) / block - 0.5, 0, coarse.shape[0] - 1)
    xs = np.clip((np.arange(w) + 0.5) / block - 0.5, 0, coarse.shape[1] - 1)
    rows = np.array([np.interp(xs, np.arange(coarse.shape[1]), r) for r in coarse])
    return np.array([np.interp(ys, np.arange(coarse.shape[0]), c) for c in rows.T]).T


def noise(shape, scale, rng):
    """Smooth random values in 0..1, varying over about `scale` tiles."""
    gh, gw = shape[0] // scale + 2, shape[1] // scale + 2
    grid = np.array([[rng.random() for _ in range(gw)] for _ in range(gh)])
    return upsample(grid, scale, shape)


def components(open_):
    """Label of the 4-connected open region of each tile (-1 = closed)."""
    h, w = open_.shape
    label = np.full((h, w), -1, int)
    n = 0
    for sy, sx in zip(*np.nonzero(open_)):
        if label[sy, sx] >= 0:
            continue
        label[sy, sx] = n
        todo = [(sy, sx)]
        while todo:
            y, x = todo.pop()
            for ny, nx in ((y - 1, x), (y + 1, x), (y, x - 1), (y, x + 1)):
                if 0 <= ny < h and 0 <= nx < w and open_[ny, nx] and label[ny, nx] < 0:
                    label[ny, nx] = n
                    todo.append((ny, nx))
        n += 1
    return label


def cheapest_path(start, goal, cost):
    """Tiles of the cheapest 4-connected path from start to any goal tile."""
    h, w = cost.shape
    best = {start: 0}
    came = {}
    heap = [(0, start)]
    while heap:
        d, (y, x) = heapq.heappop(heap)
        if goal[y, x]:
            path = [(y, x)]
            while path[-1] in came:
                path.append(came[path[-1]])
            return path
        if d > best[(y, x)]:
            continue
        for ny, nx in ((y - 1, x), (y + 1, x), (y, x - 1), (y, x + 1)):
            if 0 <= ny < h and 0 <= nx < w and cost[ny, nx] < math.inf:
                nd = d + cost[ny, nx]
                if nd < best.get((ny, nx), math.inf):
                    best[(ny, nx)] = nd
                    came[(ny, nx)] = (y, x)
                    heapq.heappush(heap, (nd, (ny, nx)))
    return []


def nearest_open(open_, tx, ty):
    h, w = open_.shape
    tx, ty = min(max(tx, 0), w - 1), min(max(ty, 0), h - 1)
    for r in range(max(h, w)):
        ys, xs = np.nonzero(open_[max(0, ty - r):ty + r + 1, max(0, tx - r):tx + r + 1])
        if len(xs):
            k = np.argmin((xs + max(0, tx - r) - tx) ** 2 + (ys + max(0, ty - r) - ty) ** 2)
            return xs[k] + max(0, tx - r), ys[k] + max(0, ty - r)
    return tx, ty


# Names that a scenario's script.txt calls people by, by original scenario
# and placement id: Remote Town's commander, smith and healer.
PEOPLE_NAMES = {(0, 0): "commander", (0, 1): "smith", (0, 2): "healer"}


def our_number(number):
    """The open pack's scenario number for an original one: the first area
    sits apart from the demo scenarios (1 to 7)."""
    return OFFSET + number


def build(game, out, number, frames, sizes, rules, people_kinds, enemy_kinds, rng):
    o = Original(game, number)
    dungeon = number in DUNGEONS
    jh, jw = o.judge.shape
    # The drawn part of the original map, cell by cell.
    drawn = np.zeros((jh, jw), bool)
    w_, h_, cw, ch = o.chips
    for jy in range(0, jh, 2):
        for jx in range(0, jw, 2):
            x, y = (jx + o.jx0) * CELL + CELL, (jy + o.jy0) * CELL + CELL
            sx, sy = (x - y) * PROJ[0] / 100, (x + y) * PROJ[1] / 100
            if o.chip(int(sx // cw), int(sy // ch)) is not None:
                drawn[jy:jy + 2, jx:jx + 2] = True
    ys, xs = np.nonzero(drawn)
    x0 = ((xs.min() + o.jx0) * CELL // TILE - 1) * TILE
    y0 = ((ys.min() + o.jy0) * CELL // TILE - 1) * TILE
    x1 = ((xs.max() + o.jx0) * CELL // TILE + 2) * TILE
    y1 = ((ys.max() + o.jy0) * CELL // TILE + 2) * TILE
    tw, th = (x1 - x0) // TILE, (y1 - y0) // TILE

    def world(tx, ty):
        return x0 + tx * TILE, y0 + ty * TILE

    def tile_of(wx, wy):
        return int((wx - x0) // TILE), int((wy - y0) // TILE)

    def walk_grid(x0, y0, tw, th):
        """How much of each tile the original lets you walk on."""
        cx = np.arange(tw * PER)
        cy = np.arange(th * PER)
        jxs = (x0 + cx * CELL + CELL // 2) // CELL - o.jx0
        jys = (y0 + cy * CELL + CELL // 2) // CELL - o.jy0
        valid = np.outer((jys >= 0) & (jys < jh), (jxs >= 0) & (jxs < jw))
        JX, JY = np.meshgrid(np.clip(jxs, 0, jw - 1), np.clip(jys, 0, jh - 1))
        walk = (o.judge[JY, JX] & 1 == 0) & drawn[JY, JX] & valid
        return walk.reshape(th, PER, tw, PER).mean(axis=(1, 3))

    # Most of an original map is empty ground nobody can reach: keep the
    # part that can be walked to from its entries and exits.
    def reachable(walk_frac, x0, y0, tw, th):
        walkable = walk_frac > 0

        def entry_tiles():
            """Tiles of the entries that stand on ground the original lets
            you walk on (some wait in rock for routes opened later)."""
            for _, x, y, _ in o.entries:
                tx, ty = int((x - x0) // TILE), int((y - y0) // TILE)
                if 1 <= tx < tw - 1 and 1 <= ty < th - 1 and walk_frac[ty - 1:ty + 2, tx - 1:tx + 2].any():
                    yield tx, ty

        def round_entries(label):
            return {label[ny, nx] for nx, ny in (nearest_open(label >= 0, tx, ty) for tx, ty in entry_tiles())}
        for left, top, right, bottom, _, _ in o.exits:   # exits are gaps out to that empty ground
            ax, ay = int((left - x0) // TILE) - 1, int((top - y0) // TILE) - 1
            bx, by = int((right - x0) // TILE) + 2, int((bottom - y0) // TILE) + 2
            walkable[max(0, ay):max(0, by), max(0, ax):max(0, bx)] = False
        label = components(walkable)
        keep = round_entries(label)
        reach = np.isin(label, list(keep))
        # Some routes run on into that empty ground: keep to the box round
        # the rock or forest that walls the map in.
        rock = components(~walkable)
        walls = set()
        for tx, ty in entry_tiles():
            nx, ny = nearest_open(rock >= 0, tx, ty)
            walls.add(rock[ny, nx])
        ys, xs = np.nonzero(np.isin(rock, list(walls)))
        box = np.zeros_like(reach)
        box[ys.min():ys.max() + 1, xs.min():xs.max() + 1] = True
        reach &= box
        label = components(reach)
        keep = round_entries(label)
        return np.isin(label, list(keep))

    reach = reachable(walk_grid(x0, y0, tw, th), x0, y0, tw, th)
    ys, xs = np.nonzero(reach)
    margin = 6
    x0, y0 = x0 + (xs.min() - margin) * TILE, y0 + (ys.min() - margin) * TILE
    tw, th = xs.max() - xs.min() + 1 + 2 * margin, ys.max() - ys.min() + 1 + 2 * margin
    walk_frac = walk_grid(x0, y0, tw, th)
    reach = reachable(walk_frac, x0, y0, tw, th)
    walk_frac = walk_frac * reach
    inside = reach.copy()   # where ground is drawn: round the reachable part
    for _ in range(8):
        grown = inside.copy()
        grown[1:] |= inside[:-1]
        grown[:-1] |= inside[1:]
        grown[:, 1:] |= inside[:, :-1]
        grown[:, :-1] |= inside[:, 1:]
        inside = grown

    # The routes: only which blocks of tiles can be walked through is kept
    # from the original. The walls and paths inside them are our own.
    block = 4 if dungeon else 6
    bh, bw = -(-th // block), -(-tw // block)
    padded = np.zeros((bh * block, bw * block))
    padded[:th, :tw] = walk_frac
    coarse = padded.reshape(bh, block, bw, block).mean(axis=(1, 3))
    if dungeon:
        open_ = np.kron(coarse >= 0.2, np.ones((block, block), bool))[:th, :tw]
    else:
        field = upsample(coarse, block, (th, tw))
        n = noise((th, tw), 4, rng) * 0.6 + noise((th, tw), 12, rng) * 0.4
        open_ = field + 0.5 * (n - 0.5) > 0.4
    open_ &= inside

    # Landmarks where the original has them; the village's buildings stand
    # on ground that cannot be walked on.
    landmarks = []
    for ob in o.objects:
        bank_name = o.banks[ob["file"]].lower() if 0 <= ob["file"] < len(o.banks) else "?"
        pieces = []
        if bank_name in LANDMARKS and (LANDMARKS[bank_name] is None or ob["pattern"] in LANDMARKS[bank_name]):
            pieces = rules.get(bank_name, {}).get(ob["pattern"], [])
        elif bank_name in EXTRA and ob["pattern"] in EXTRA[bank_name]:
            pieces = [(EXTRA[bank_name][ob["pattern"]], 0, 0)]
        if not pieces:
            continue
        bank = o.bank(ob["file"])
        f = foot(bank, ob["pattern"]) if bank else None
        if not f:
            continue
        fx, fy = screen_to_world(*f)
        for name, dx, dy in pieces:
            landmarks.append((name.rstrip("!"), ob["x"] + fx + dx, ob["y"] + fy + dy, bank_name in SOLID))
    solid = np.zeros((th, tw), bool)
    for name, wx, wy, is_solid in landmarks:
        if not is_solid or name not in sizes:
            continue
        side = sizes[name] * 0.8 / 0.3   # a footprint about as wide as the picture
        ax, ay = tile_of(wx - side, wy - side)
        bx, by = tile_of(wx, wy)
        solid[max(0, ay):max(0, by), max(0, ax):max(0, bx)] = True
    # Gates stand over exits: never wall in an exit or an entry.
    for left, top, right, bottom, _, _ in o.exits:
        ax, ay = tile_of(left, top)
        bx, by = tile_of(right, bottom)
        solid[max(0, ay - 1):max(0, by + 2), max(0, ax - 1):max(0, bx + 2)] = False
    for _, x, y, _ in o.entries:
        tx, ty = tile_of(x, y)
        solid[max(0, ty - 1):max(0, ty + 2), max(0, tx - 1):max(0, tx + 2)] = False
    open_ &= ~solid

    # Every entry and exit is open, and they all join up.
    targets = [tile_of(x, y) for _, x, y, _ in o.entries]
    targets += [tile_of((left + right) / 2, (top + bottom) / 2) for left, top, right, bottom, _, _ in o.exits]
    targets = [(min(max(tx, 1), tw - 2), min(max(ty, 1), th - 2)) for tx, ty in targets]
    targets = [(tx, ty) for tx, ty in targets if reach[max(0, ty - 4):ty + 5, max(0, tx - 4):tx + 5].any()]
    for tx, ty in targets:
        open_[ty - 1:ty + 2, tx - 1:tx + 2] |= inside[ty - 1:ty + 2, tx - 1:tx + 2] & ~solid[ty - 1:ty + 2, tx - 1:tx + 2]
    for left, top, right, bottom, _, _ in o.exits:   # the whole of each exit can be stepped on
        ax, ay = tile_of(left, top)
        bx, by = tile_of(right, bottom)
        area = (slice(max(0, ay), max(0, by + 1)), slice(max(0, ax), max(0, bx + 1)))
        if reach[max(0, ay - 4):by + 5, max(0, ax - 4):bx + 5].any():
            open_[area] |= inside[area]
    cost = np.where(open_, 1.0, np.where(inside & ~solid, 6.0, math.inf))
    carved = np.zeros((th, tw), bool)
    label = components(open_)
    joined = label == label[targets[0][1], targets[0][0]] if targets else np.zeros((th, tw), bool)
    for tx, ty in targets[1:]:
        if joined[ty, tx]:
            continue
        for y, x in cheapest_path((ty, tx), joined, cost):
            for yy, xx in ((y, x), (y + 1, x), (y, x + 1)):
                if yy < th and xx < tw and cost[yy, xx] < math.inf:
                    open_[yy, xx] = carved[yy, xx] = True
                    cost[yy, xx] = 1.0
        label = components(open_)
        joined = label == label[targets[0][1], targets[0][0]]

    # Ground: grass or dirt per block from the original's colour, with our
    # own edges; carved paths are dirt. Dungeons are brick with rubble.
    terrain = np.ones((th + 1, tw + 1), int)
    if dungeon:
        for ty in range(th + 1):
            for tx in range(tw + 1):
                terrain[ty, tx] = 0 if rng.random() < 0.08 else 1
    else:
        grass = np.zeros((bh, bw))
        for by in range(bh):
            for bx in range(bw):
                c = o.colour(*world(bx * block + block // 2, by * block + block // 2))
                grass[by, bx] = 1.0 if c is None or c[1] > c[0] * 1.02 else 0.0
        g = upsample(grass, block, (th + 1, tw + 1)) + 0.6 * (noise((th + 1, tw + 1), 3, rng) - 0.5)
        terrain = (g > 0.5).astype(int)
        for ty, tx in zip(*np.nonzero(carved)):
            terrain[ty:ty + 2, tx:tx + 2] = 0
    rows = []
    for ty in range(th):
        row = []
        for tx in range(tw):
            if not inside[ty, tx]:
                row.append(".")
                continue
            corners = (terrain[ty, tx], terrain[ty, tx + 1], terrain[ty + 1, tx], terrain[ty + 1, tx + 1])
            row.append(f"0:{ground.pattern_index(corners, tx, ty)}")
        rows.append(" ".join(row))

    objects = []
    taken = solid.copy()

    def place(name, wx, wy):
        if name in frames:
            objects.append((wy + wx, f"{int(wx - x0)},{int(wy - y0)},1,{frames[name]}"))
            tx, ty = tile_of(wx, wy)
            taken[max(0, ty - 1):ty + 2, max(0, tx - 1):tx + 2] = True

    for name, wx, wy, _ in landmarks:
        place(name, wx, wy)

    if dungeon:
        # Walls along every edge between floor and rock, two tiles a piece
        # where they can be, a pillar where only one tile is left.
        def edge_x(tx, ty):   # the edge along x at the top of tile (tx, ty)
            above = ty > 0 and open_[ty - 1, tx]
            below = ty < th and open_[ty, tx]
            return above != below

        def edge_y(tx, ty):
            left = tx > 0 and open_[ty, tx - 1]
            right = tx < tw and open_[ty, tx]
            return left != right

        k = 0
        for ty in range(th + 1):
            tx = 0
            while tx < tw:
                if edge_x(tx, ty):
                    if tx + 1 < tw and edge_x(tx + 1, ty):
                        fit = "" if k % 6 else rng.choice(["_pipes", "_grate"])
                        place(f"dwall{fit}_x", *world(tx + 1, ty))
                        tx += 2
                    else:
                        place("dpillar", x0 + tx * TILE + TILE // 2, y0 + ty * TILE)
                        tx += 1
                    k += 1
                else:
                    tx += 1
        for tx in range(tw + 1):
            ty = 0
            while ty < th:
                if edge_y(tx, ty):
                    if ty + 1 < th and edge_y(tx, ty + 1):
                        fit = "" if k % 6 else rng.choice(["_pipes", "_grate"])
                        place(f"dwall{fit}_y", *world(tx, ty + 1))
                        ty += 2
                    else:
                        place("dpillar", x0 + tx * TILE, y0 + ty * TILE + TILE // 2)
                        ty += 1
                    k += 1
                else:
                    ty += 1
    else:
        trees = FROZEN if "Frozen" in o.title else BARE if "Wasteland" in o.title or "Pillars" in o.title else FOREST
        for ty in range(th):
            for tx in range(tw):
                if not inside[ty, tx] or open_[ty, tx] or taken[ty, tx]:
                    continue
                near = open_[max(0, ty - 1):ty + 2, max(0, tx - 1):tx + 2].any()
                if rng.random() < (0.8 if near else 0.45):
                    j = TILE // 4
                    place(rng.choice(trees), x0 + tx * TILE + TILE // 2 + rng.randrange(-j, j),
                          y0 + ty * TILE + TILE // 2 + rng.randrange(-j, j))
                elif near and rng.random() < 0.15:
                    place(rng.choice(["rock1", "rock2", "stones", "grass_tall"]),
                          x0 + tx * TILE + TILE // 2, y0 + ty * TILE + TILE // 2)

    objects.sort()
    judge = ["".join("." if v else "x" for v in line) for line in open_]
    sheet = "Map/floor.sheet" if dungeon else "Map/ground.sheet"
    with open(out / "Map" / f"{o.name}.txt", "w", newline="\n") as f:
        f.write(f"# {o.title}: our own layout on the routes of the original's {o.name}, "
                "generated by tools/content/first_area.py\n")
        f.write(f"size={tw},{th}\ntiles={TILE}\ncell={TILE},{TILE}\njudge=0,0,{tw},{th}\n")
        f.write(f"bank={sheet}\nbank=Map/props.sheet\n\n[ground]\n" + "\n".join(rows))
        f.write("\n\n[judge]\n" + "\n".join(judge) + "\n\n[objects]\n" + "\n".join(t for _, t in objects) + "\n")

    def spot(x, y):
        """(x, y) moved to the middle of the nearest open tile if it is not on one."""
        tx, ty = tile_of(x, y)
        if 0 <= ty < th and 0 <= tx < tw and open_[ty, tx]:
            return int(x - x0), int(y - y0)
        tx, ty = nearest_open(open_, tx, ty)
        return int(tx * TILE + TILE // 2), int(ty * TILE + TILE // 2)

    lines = [f"# generated by tools/content/first_area.py after the original's scenario {number}",
             f"title={o.title}", f"map={o.name}"]
    for eid, x, y, d in o.entries:
        lines.append(f"entry={eid},{','.join(map(str, spot(x, y)))},{d % 8}")
    for left, top, right, bottom, target, entry in o.exits:
        if target in FIELDS + DUNGEONS:   # exits out of the first area are left out
            lines.append(f"exit={left - x0},{top - y0},{right - x0},{bottom - y0},{our_number(target)},{entry}")
    for c in o.groups[2]:
        if c["kind"] not in enemy_kinds:
            continue
        x, y = spot(c["x"], c["y"])
        stats = enemy_kinds[c["kind"]]
        lines.append(f"enemy={c['kind']},{x},{y},{c['direction'] % 8},{stats}")
    for c in o.groups[1]:
        if c["kind"] in people_kinds:
            x, y = spot(c["x"], c["y"])
            name = PEOPLE_NAMES.get((number, c["id"]))
            lines.append(f"person={c['kind']},{x},{y},{c['direction'] % 8}" + (f",{name}" if name else ""))
    folder = out / "Scenario" / f"{our_number(number):08d}"
    folder.mkdir(parents=True, exist_ok=True)
    (folder / "scenario.txt").write_text("\n".join(lines) + "\n")
    print(f"{our_number(number)} {o.name} {o.title}: {tw}x{th} tiles, {len(objects)} objects, {len(o.exits)} exits")


def our_stats(out, kinds):
    """Our own stats for each kind of monster (hp to element, as an enemy=
    line after kind, x, y, direction): the weakest of that kind in the
    demo's scenarios, or the demo's goblin for kinds the demo leaves out."""
    best = {}
    for f in sorted((out / "Scenario").glob("0000000[1-9]/scenario.txt")):
        for line in f.read_text().split("\n"):
            if line.startswith("enemy="):
                v = line[6:].split("#")[0].strip().split(",")
                kind, level = int(v[0]), int(v[8]) if len(v) > 8 else 0
                if kind not in best or level < best[kind][0]:
                    best[kind] = (level, ",".join(v[4:]))
    fallback = best.get(0, (0, "15,39,960,goblin"))[1]
    return {k: best.get(k, (0, fallback))[1] for k in kinds}


def main():
    game, out = Path(sys.argv[1]), Path(sys.argv[2])
    if not (game / "Map").is_dir():
        game = next(p.parent for p in game.rglob("Map") if (p / "Pattern").is_dir())
    frames, sizes = {}, {}
    for line in (out / "Map" / "props.sheet").read_text().split("\n"):
        if line.startswith("frame="):
            v = line[6:].split(",")
            name = v[6].strip() if len(v) == 7 else f"#{len(frames)}"
            frames[name] = len(frames)
            sizes[name] = int(v[2])
    _, rules = read_skin(out / "skin.txt")
    people = {int(p.name) for p in (out / "Character" / "PEOPLE").iterdir()}
    enemies = our_stats(out, {int(p.name) for p in (out / "Character" / "ENEMY").iterdir()})
    rng = random.Random(1)
    for number in FIELDS + DUNGEONS:
        build(game, out, number, frames, sizes, rules, people, enemies, rng)


if __name__ == "__main__":
    main()
