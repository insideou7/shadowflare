#!/usr/bin/env python3
"""Lay out the open content's first and second areas on the routes of the
original's: the opening fields, Remote Town, the Dusty Ruins, Cold Svalt
Town and the forests and four dungeons round it (29 maps), then the
Caravan, Kanfore, Fanann and the forests, mines, forts and caves round
them (33 maps). Needs the player's own copy of the game.

    python3 tools/content/first_area.py <game folder> <content pack> [area ...]

With area numbers, only those areas are laid out (the others' files are
left as they are).

Each map is walked on like the original's: the ground that can be walked
on from the entries is the original's, cell for cell (a cell is 160 world
units, about the width of a character), so laid over the original the map
looks a little different but plays the same. Also kept: which ground is
grass or dirt, where the exits lead and where the player arrives, which
monsters and townspeople stand where (enemy_stats.py then gives the
monsters the original's numbers), and where its landmarks (buildings, stairs,
fences, pipes) stand, drawn with the props skin.txt gives them. What is
drawn round the walkable ground is our own: trees and rocks outdoors,
walls along the dungeon floors, of the kind the original's walls there
are (WALLS). No original picture, tile or sprite is
copied; players who want the original maps exactly as drawn play them
from their own copy (content/skin.txt draws them with our art).

Writes Map/<original map name>.txt and Scenario/<10000000 + original
number>/ for each. Then run
enemy_stats.py to give the monsters the original's numbers, then
enemy_holes.py to put back the burrows (Enemy Holes). Exits to areas the
pack does not have are left out (see exit_targets). Needs Pillow and
numpy.
"""

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

# The second area (the original's episode 2): the Caravan, the forests
# round Kanfore and Fanann, and the mines, forts and caves off them.
FIELDS2 = [2000000, 2000001, 2100000, 2100001, 2100002, 2100003, 2100004, 2100005, 2100006,
           2200000, 2200001, 2200002, 2200003, 2200004, 2200005, 2999999]
DUNGEONS2 = [2010000, 2110000, 2120000, 2120001, 2130000, 2140000, 2210000, 2210001, 2210002,
             2210003, 2210004, 2220000, 2220001, 2230000, 2240000, 2240001, 2250000]

# The third area (episode 3): the wide lands round Losmans, Antalusia,
# Calado, Ponse and Sena's shrine, and the towers, ruins and caves in them.
FIELDS3 = [3000101, 3000201, 3000202, 3000203, 3000301, 3000302, 3000303, 3000304, 3000305, 3000306,
           3000307, 3000402, 3000403, 3000404, 3000405, 3000406, 3000407, 3000503, 3000505, 3000506,
           3000507, 3240000, 3900000, 3900001, 3900002, 3900003, 3900004]
DUNGEONS3 = [3010000, 3010001, 3010002, 3010003, 3010004, 3020000, 3020001, 3020002, 3020003, 3020004,
             3030000, 3040000, 3050000, 3060000, 3070000, 3080000, 3090000, 3100000, 3110000, 3110001,
             3110002, 3110003, 3110004, 3120000, 3130000, 3140000, 3140001, 3150000, 3160000, 3170000,
             3180000, 3190000, 3200000, 3210000, 3220000, 3230000, 3900005]

# Each area's maps, in the order they are laid out.
AREAS = {1: FIELDS + DUNGEONS, 2: FIELDS2 + DUNGEONS2, 3: FIELDS3 + DUNGEONS3}
DUNGEON = set(DUNGEONS + DUNGEONS2 + DUNGEONS3)
ALL = [n for area in AREAS.values() for n in area]

# Dungeon walls by the original's wall bank: our wall pieces (plain, then
# the ones mixed in every sixth piece), pillar, floor sheet and the first
# frame of its kind of floor on it.
WALLS = {
    "00d_obj00.njp": ("dwall", ["_pipes", "_grate"], "dpillar", "Map/floor.sheet", 0),
    "fort01.njp": ("fwall", ["_torch", "_blood"], "fpillar", "Map/fortfloor.sheet", 0),
    "coalmine1.njp": ("mine", ["_broken"], "mine_pillar", "Map/cavefloor.sheet", 256),
    "deep01.njp": ("earth", ["2"], "earth_post", "Map/deepfloor.sheet", 0),
}
WALLS["deep02.njp"] = WALLS["deep03.njp"] = WALLS["deep04.njp"] = WALLS["deep01.njp"]
# The third area's towers, of dark castle stone on paving.
WALLS["yd.njp"] = ("fwall_black", [""], "fpillar_black", "Map/fortfloor.sheet", 240)

# Original banks whose pieces are landmarks, drawn where the original has
# them, with the props skin.txt gives them (None = every piece of the bank).
LANDMARKS = {
    "00f_city00.njp": None, "00f_city01.njp": None, "00f_obj00.njp": None,
    "00f_obj02.njp": {0, 4, 5, 6, 9},
    "00d_obj00.njp": set(range(0, 24)) | {49, 51, 52, 53, 54, 55, 56},
    "00d_obj01.njp": None,
    "00f_city02.njp": None, "mon01.njp": None,     # the town walls and gates
    "jimen_x00.njp": {0, 1, 2, 3, 10, 11, 12, 13},  # spike pits
    # The second area's: mine stairs, rings and lanterns, the forts'
    # stairs and doors, the caves' gates, portals and stairs, the power
    # supply's engine and fences, and cave mouths outdoors.
    "coalmine1.njp": set(range(0, 7)),
    "fort01.njp": {0, 1, 2, 3, 12, 20},
    "fort01_ex.njp": None,
    "deep01.njp": set(range(0, 16)) | {17, 20, 23, 26},
    "tower1.njp": None,
    "caveenter.njp": None,
}
for _deep in ("deep02.njp", "deep03.njp", "deep04.njp"):
    LANDMARKS[_deep] = LANDMARKS["deep01.njp"]
# The third area's castle stone: in the towers, where our walls stand in
# for its walls, only its statues, keeps, stairs and magic circles; in
# Losmans and outdoors every piece, the town's walls and gates among them.
LANDMARKS["yd.njp"] = {0, 1, 2, 3, 4, 5, 6, 29, 30, 31, 32, 33}
LANDMARKS["tower2.njp"] = None
OUTDOOR_LANDMARKS = {"yd.njp": None}

FOREST = ["tree1", "tree2", "tree3", "tree1", "tree2", "oak"]
FROZEN = ["conifer_blue", "tree_dead", "conifer_green"]   # Frozen Forest
BARE = ["tree_dead", "tree_dead", "oak"]                  # the wastelands
BARE_PLACES = ("Wasteland", "Pillars", "Wilds", "Desert", "Valley", "Plain", "Atonement", "Burial", "Peninsula",
               "Beach", "Gulf", "Hiding Place")

# Banks whose pieces stand on ground that cannot be walked on.
SOLID = {"00f_city00.njp", "00f_city01.njp", "00f_city02.njp"}

MARGIN = 4   # tiles of ground round what can be walked on

# Added to the original scenario numbers (once to keep clear of the old demo's).
OFFSET = 10000000


def find(folder, stem, ext):
    for p in folder.iterdir():
        if p.stem.lower() == stem.lower() and p.suffix.lower() == ext.lower():
            return p
    return None


# --- Reading the original ----------------------------------------------------

def read_mct(data):
    """Map path, title, placements (objects, people, enemies), entry
    points, light, town and whether Transport may be cast, of a
    Scenario.Mct, as in src/formats/scenario.cpp."""
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
    town = i32()  # the area's town (scenario + 0x60), where Transport leads
    i32(), i32()
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
            c["light"] = i32()  # a Dark.njp light (0-5) or -1
            c["words"] = [i32() for _ in range(tail)]
            group.append(c)
        groups.append(group)
    entries = [[i32() for _ in range(4)] for _ in range(i32())]
    light = 2000  # how light the area is: 2000 lit, 1000 dark
    transport = False
    if len(data) - o >= 12:
        i32()
        transport = i32() != 0  # scenario + 0x70
        light = i32()
    return path, title, groups, entries, light, town, transport


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
    """(left, top, right, bottom, scenario, entry, conditional) for each
    object whose ONJUDGE trigger runs CHANGESCENARIO, following IF jumps;
    conditional when it is reached only through an IF (a way that opens
    later in the story)."""
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
            conditional = not any(op == 17 for op, _ in sentences[sentence]) if 0 <= sentence < len(sentences) else False
            out.append((c["x"] + r[0], c["y"] + r[1], c["x"] + r[2], c["y"] + r[3], *target, conditional))
    return out


class Original:
    """One original map and scenario: walkability, ground colour, objects."""

    @staticmethod
    def map_of(game, number):
        """The original map a scenario is laid on."""
        path = read_mct(find(game / "Scenario" / f"{number:08d}", "Scenario", ".mct").read_bytes())[0]
        return path.replace("\\", "/").split("/")[-1].rsplit(".", 1)[0]

    def __init__(self, game, number):
        sc = game / "Scenario" / f"{number:08d}"
        self.path, self.title, self.groups, self.entries, self.light, self.town, self.transport = read_mct(find(sc, "Scenario", ".mct").read_bytes())
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


def flood(open_, seeds):
    """Cells of open_ 4-connected to any of the seed cells (row, column)."""
    h, w = open_.shape
    seen = np.zeros_like(open_)
    todo = [(y, x) for y, x in seeds if 0 <= y < h and 0 <= x < w and open_[y, x]]
    for y, x in todo:
        seen[y, x] = True
    while todo:
        y, x = todo.pop()
        for ny, nx in ((y - 1, x), (y + 1, x), (y, x - 1), (y, x + 1)):
            if 0 <= ny < h and 0 <= nx < w and open_[ny, nx] and not seen[ny, nx]:
                seen[ny, nx] = True
                todo.append((ny, nx))
    return seen


def grow(mask, steps):
    """mask widened by `steps` cells in every direction, corners included."""
    for _ in range(steps):
        g = mask.copy()
        g[1:] |= mask[:-1]
        g[:-1] |= mask[1:]
        g[:, 1:] |= g[:, :-1].copy()
        g[:, :-1] |= g[:, 1:].copy()
        mask = g
    return mask


def distance(walk, limit):
    """How many cells (counting diagonals as one) each cell is from walk, up to limit."""
    d = np.full(walk.shape, limit, int)
    reach = walk.copy()
    d[reach] = 0
    for k in range(1, limit):
        nxt = grow(reach, 1)
        d[nxt & ~reach] = k
        reach = nxt
    return d


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
PEOPLE_NAMES = {(0, 0): "commander", (0, 1): "smith", (0, 2): "healer",
                # The Wasteland of Pillars: a scout and an exiled trader.
                (6, 0): "scout", (6, 1): "trader",
                # The companions waiting in Remote Town's kennel and
                # Cold Svalt Town (people of kind 1000000 + their
                # Character/PARTNER folder).
                (0, 10000): "ash", (0, 10001): "soot", (0, 10002): "amber", (0, 10003): "pip",
                (1000000, 10000): "rust",
                # Cold Svalt Town's people.
                (1000000, 0): "commander", (1000000, 1): "soldier", (1000000, 2): "guard",
                (1000000, 3): "boy", (1000000, 4): "widow", (1000000, 5): "knight",
                (1000000, 6): "mother", (1000000, 7): "merchant", (1000000, 8): "healer",
                (1000000, 9): "wizard", (1000000, 10): "jeweler"}
# The warp gates the pack has (content/gates.txt): the object a click on
# opens the GATE window, which stands on a warp circle.
GATES = {0: [200], 1000000: [200],
         2100000: [200], 2100002: [200], 2100004: [200], 2100005: [200], 2100006: [200],
         2200000: [200], 2200003: [200, 210], 2200005: [200], 2210002: [200],
         **{n: [200] for n in (3000201, 3000302, 3000303, 3000306, 3000307, 3000402, 3000505, 3240000,
                               3900000, 3900001, 3900002, 3900003, 3900004, 3900005)}}


def exit_targets(number):
    """The scenarios a map's exits are written for. Exits to areas the pack
    does not have yet are left out, and the first area's maps keep only
    their exits within it: the way on to the second area, Near the Remote
    Town's road to the Caravan, opens only once the first area's missions
    are done, so its script (Scenario/10000001/script.txt) adds it."""
    return AREAS[1] if number in AREAS[1] else ALL


def written_exits(o, number):
    """The exits scenario.txt gives a map: (left, top, right, bottom,
    scenario, entry). Past the first area, an exit the original opens only
    later in the story is left to the scenario's script (scripts.py)."""
    return [e[:6] for e in o.exits if e[4] in exit_targets(number) and not (e[6] and number not in AREAS[1])]


_MAP_NAMES = {}


def map_name(game, number):
    """Our map file for a scenario: the original's map name, or, where an
    earlier scenario of the pack already has a map of that name (several
    of the original's share one), the name and the scenario's number."""
    if not _MAP_NAMES:
        seen = set()
        for n in ALL:
            name = Original.map_of(game, n)
            _MAP_NAMES[n] = name if name.lower() not in seen else f"{name}_{our_number(n)}"
            seen.add(name.lower())
    return _MAP_NAMES[number]


def our_number(number):
    """The open pack's scenario number for an original one."""
    return OFFSET + number


def walkable(o):
    """The cells of the original map that can be walked on from its entries,
    in the original's judge grid: its open ground that is drawn, joined to
    an entry. Exits are gaps out to the empty ground round the map, so the
    walk stops at them."""
    jh, jw = o.judge.shape
    drawn = np.zeros((jh, jw), bool)
    w_, h_, cw, ch = o.chips
    for jy in range(0, jh, 2):
        for jx in range(0, jw, 2):
            x, y = (jx + o.jx0) * CELL + CELL, (jy + o.jy0) * CELL + CELL
            sx, sy = (x - y) * PROJ[0] / 100, (x + y) * PROJ[1] / 100
            if o.chip(int(sx // cw), int(sy // ch)) is not None:
                drawn[jy:jy + 2, jx:jx + 2] = True
    walk = (o.judge & 1 == 0) & drawn
    exits = np.zeros_like(walk)
    for left, top, right, bottom, *_ in o.exits:
        exits[max(0, int(top // CELL) - o.jy0 - 1):max(0, int(bottom // CELL) - o.jy0 + 2),
              max(0, int(left // CELL) - o.jx0 - 1):max(0, int(right // CELL) - o.jx0 + 2)] = True
    seeds = [(int(y // CELL) - o.jy0, int(x // CELL) - o.jx0) for _, x, y, _ in o.entries]
    reach = flood(walk & ~exits, seeds)
    # The exits themselves, as far as they join what was reached.
    while True:
        more = grow(reach, 1) & walk & exits & ~reach
        if not more.any():
            return reach
        reach |= more


def crop(o, reach):
    """Where our map lies on the original's: the world point (x0, y0) of the
    original that is our map's (0, 0), and our size in tiles. Our map covers
    what can be walked on, with MARGIN tiles of ground round it, on whole tiles.
    The engine moves saved games between the two by it (open_areas.py
    writes the table)."""
    ys, xs = np.nonzero(reach)
    x0 = ((xs.min() + o.jx0) // PER - MARGIN) * TILE
    y0 = ((ys.min() + o.jy0) // PER - MARGIN) * TILE
    tw = (xs.max() + o.jx0) // PER + MARGIN + 1 - x0 // TILE
    th = (ys.max() + o.jy0) // PER + MARGIN + 1 - y0 // TILE
    return int(x0), int(y0), int(tw), int(th)


def wall_kit(o):
    """The WALLS row for a dungeon: the wall bank most of its pieces come from."""
    counts = {}
    for ob in o.objects:
        name = o.banks[ob["file"]].lower() if 0 <= ob["file"] < len(o.banks) else "?"
        if name in WALLS:
            counts[name] = counts.get(name, 0) + 1
    return WALLS[max(counts, key=counts.get)] if counts else WALLS["00d_obj00.njp"]


def build(game, out, number, frames, sizes, rules, people_kinds, enemy_kinds, rng):
    o = Original(game, number)
    dungeon = number in DUNGEON
    marks = LANDMARKS if dungeon else {**LANDMARKS, **OUTDOOR_LANDMARKS}
    reach = walkable(o)
    kit = wall_kit(o) if dungeon else None

    # Its walkability is the original's, cell for cell: the walls, trees and
    # rocks drawn round it are our own.
    x0, y0, tw, th = crop(o, reach)
    margin = MARGIN
    ax, ay = x0 // CELL - o.jx0, y0 // CELL - o.jy0
    walk = np.zeros((th * PER, tw * PER), bool)
    sy, sx = max(0, -ay), max(0, -ax)
    part = reach[max(0, ay):ay + th * PER, max(0, ax):ax + tw * PER]
    walk[sy:sy + part.shape[0], sx:sx + part.shape[1]] = part
    walk_tile = walk.reshape(th, PER, tw, PER).any(axis=(1, 3))
    # Where ground is drawn: round what can be walked on (just under the
    # walls in dungeons, the width of a forest edge outdoors).
    inside = grow(walk_tile, 1 if dungeon else margin)

    def world(tx, ty):
        return x0 + tx * TILE, y0 + ty * TILE

    def tile_of(wx, wy):
        return int((wx - x0) // TILE), int((wy - y0) // TILE)

    def cell_of(wx, wy):
        return int((wx - x0) // CELL), int((wy - y0) // CELL)

    # Landmarks where the original has them.
    landmarks = []
    for ob in o.objects:
        bank_name = o.banks[ob["file"]].lower() if 0 <= ob["file"] < len(o.banks) else "?"
        pieces = []
        if bank_name in marks and (marks[bank_name] is None or ob["pattern"] in marks[bank_name]):
            pieces = rules.get(bank_name, {}).get(ob["pattern"], [])
        if not pieces:
            continue
        bank = o.bank(ob["file"])
        f = foot(bank, ob["pattern"]) if bank else None
        if not f:
            continue
        fx, fy = screen_to_world(*f)
        for name, dx, dy in pieces:
            landmarks.append((name.rstrip("!"), ob["x"] + fx + dx, ob["y"] + fy + dy))
    # Ground the landmarks stand on is theirs: nothing else is put there.
    taken = np.zeros(walk.shape, bool)
    for name, wx, wy in landmarks:
        side = sizes.get(name, 0) * 0.8 / 0.3   # a footprint about as wide as the picture
        ax_, ay_ = cell_of(wx - side, wy - side)
        bx_, by_ = cell_of(wx, wy)
        taken[max(0, ay_):max(0, by_ + 1), max(0, ax_):max(0, bx_ + 1)] = True

    # Ground: grass or dirt from the original's colour round each tile
    # corner, with our own edges. Dungeons are brick with rubble.
    if dungeon:
        terrain = (np.array([[rng.random() for _ in range(tw + 1)] for _ in range(th + 1)]) >= 0.08).astype(int)
    else:
        grass = np.ones((th + 1, tw + 1))
        for ty in range(th + 1):
            for tx in range(tw + 1):
                c = o.colour(*world(tx, ty))
                grass[ty, tx] = 1.0 if c is None or c[1] > c[0] * 1.02 else 0.0
        smooth = grass.copy()
        smooth[1:-1, 1:-1] = (grass[:-2, 1:-1] + grass[2:, 1:-1] + grass[1:-1, :-2] + grass[1:-1, 2:] + 2 * grass[1:-1, 1:-1]) / 6
        n = np.array([[rng.random() for _ in range(tw + 1)] for _ in range(th + 1)])
        terrain = (smooth + 0.3 * (n - 0.5) > 0.5).astype(int)
    rows = []
    for ty in range(th):
        row = []
        for tx in range(tw):
            if not inside[ty, tx]:
                row.append(".")
                continue
            corners = (terrain[ty, tx], terrain[ty, tx + 1], terrain[ty + 1, tx], terrain[ty + 1, tx + 1])
            row.append(f"0:{ground.pattern_index(corners, tx, ty) + (kit[4] if kit else 0)}")
        rows.append(" ".join(row))

    objects = []

    def place(name, wx, wy):
        if name in frames:
            objects.append((wy + wx, f"{int(wx - x0)},{int(wy - y0)},1,{frames[name]}"))

    for name, wx, wy in landmarks:
        place(name, wx, wy)

    blocked = ~walk
    if dungeon:
        # Walls along every edge between floor and rock, on the original's
        # cells: four cells a piece, the last piece of a run overlapping the
        # one before it, a pillar where a run is shorter than three cells.
        # Rock one cell thick between two floors gets one wall down its middle.
        h, w = walk.shape
        k = 0

        def wall(axis, line, a, b):
            """Pieces along a run of cells a..b on edge line `line`."""
            nonlocal k
            n = b - a
            if n <= 2:
                at = a * CELL + n * CELL / 2
                xy = (x0 + at, y0 + line * CELL) if axis == "x" else (x0 + line * CELL, y0 + at)
                place(kit[2], *xy)
                k += 1
                return
            starts = list(range(a, b - 3, 4))
            if not starts or starts[-1] + 4 < b:
                starts.append(b - 4)
            for s in starts:
                fit = "" if k % 6 else rng.choice(kit[1])
                at = (s + 2) * CELL
                xy = (x0 + at, y0 + line * CELL) if axis == "x" else (x0 + line * CELL, y0 + at)
                place(f"{kit[0]}{fit}_{axis}", *xy)
                k += 1

        def runs(flags):
            x = 0
            while x < len(flags):
                if flags[x]:
                    s = x
                    while x < len(flags) and flags[x]:
                        x += 1
                    yield s, x
                else:
                    x += 1

        W = np.pad(walk, 1)          # rows and columns past the map are rock
        B = ~W
        thin_r = B[1:-1, 1:-1] & W[:-2, 1:-1] & W[2:, 1:-1]   # one cell of rock between floors, across rows
        thin_c = B[1:-1, 1:-1] & W[1:-1, :-2] & W[1:-1, 2:]
        for r in range(h + 1):       # edge lines between cell rows r-1 and r
            above, below = W[r, 1:-1], W[r + 1, 1:-1]
            thin_above = thin_r[r - 1] if r > 0 else np.zeros(w, bool)
            thin_below = thin_r[r] if r < h else np.zeros(w, bool)
            for a, b in runs((above != below) & ~thin_above & ~thin_below):
                wall("x", r, a, b)
        for r in range(h):
            for a, b in runs(thin_r[r]):
                wall("x", r + 0.5, a, b)
        for c in range(w + 1):
            left, right = W[1:-1, c], W[1:-1, c + 1]
            thin_left = thin_c[:, c - 1] if c > 0 else np.zeros(h, bool)
            thin_right = thin_c[:, c] if c < w else np.zeros(h, bool)
            for a, b in runs((left != right) & ~thin_left & ~thin_right):
                wall("y", c, a, b)
        for c in range(w):
            for a, b in runs(thin_c[:, c]):
                wall("y", c + 0.5, a, b)
    else:
        # Trees on the ground that cannot be walked on, thick at its edge
        # and thinning out away from it; a rock or a bush where only a little
        # ground is shut off in the open.
        trees = FROZEN if "Frozen" in o.title else BARE if any(w in o.title for w in BARE_PLACES) else FOREST
        near = distance(walk, 2 * margin + 1)
        inside_cells = np.kron(inside, np.ones((PER, PER), bool))
        mass = flood(blocked, [(0, 0)])            # the rock and forest round the map
        free = blocked & inside_cells & ~taken
        if o.town == number and landmarks and number not in AREAS[1] + AREAS[2]:
            # Past the second area, a town's own ground (inside its walls and
            # buildings) stays open: no forest grows in its streets.
            cs = [cell_of(wx, wy) for _, wx, wy in landmarks]
            xs, ys = [c[0] for c in cs], [c[1] for c in cs]
            free[max(0, min(ys)):max(ys) + 1, max(0, min(xs)):max(xs) + 1] = False
        # Ground shut off by a landmark is the landmark's.
        free &= ~flood(blocked & ~mass, list(zip(*np.nonzero(grow(taken, 2) & blocked & ~mass))))
        spot_free = np.ones(walk.shape, bool)
        cells = list(zip(*np.nonzero(free & (near <= 2 * margin))))
        rng.shuffle(cells)
        cells.sort(key=lambda c: near[c])
        for cy, cx in cells:
            if not spot_free[cy, cx]:
                continue
            d = near[cy, cx]
            if mass[cy, cx]:
                if d > 2 and rng.random() < 0.3:
                    continue
                r = 1 if d <= 1 else 2
                j = CELL // 3
                place(rng.choice(trees), x0 + cx * CELL + CELL // 2 + rng.randrange(-j, j),
                      y0 + cy * CELL + CELL // 2 + rng.randrange(-j, j))
            else:
                r = 1
                place(rng.choice(["rock1", "rock2", "stones", "boulder", "grass_tall"]),
                      x0 + cx * CELL + CELL // 2, y0 + cy * CELL + CELL // 2)
            spot_free[max(0, cy - r):cy + r + 1, max(0, cx - r):cx + r + 1] = False

    for c in o.groups[0]:
        if c["id"] in GATES.get(number, []):
            place("warp_circle", c["x"], c["y"])

    objects.sort()
    judge = ["".join("." if v else "x" for v in line) for line in walk]
    sheet = kit[3] if dungeon else "Map/ground.sheet"
    map_file = map_name(game, number)
    with open(out / "Map" / f"{map_file}.txt", "w", newline="\n") as f:
        f.write(f"# {o.title}: walked on like the original's {o.name}, with our own ground, walls and trees, "
                "generated by tools/content/first_area.py\n")
        f.write(f"size={tw},{th}\ntiles={TILE}\ncell={CELL},{CELL}\njudge=0,0,{tw * PER},{th * PER}\n")
        f.write(f"bank={sheet}\nbank=Map/props.sheet\n\n[ground]\n" + "\n".join(rows))
        f.write("\n\n[judge]\n" + "\n".join(judge) + "\n\n[objects]\n" + "\n".join(t for _, t in objects) + "\n")

    def spot(x, y):
        """(x, y) moved to the middle of the nearest open cell if it is not on one."""
        cx, cy = cell_of(x, y)
        if 0 <= cy < walk.shape[0] and 0 <= cx < walk.shape[1] and walk[cy, cx]:
            return int(x - x0), int(y - y0)
        cx, cy = nearest_open(walk, cx, cy)
        return int(cx * CELL + CELL // 2), int(cy * CELL + CELL // 2)

    lines = [f"# generated by tools/content/first_area.py after the original's scenario {number}",
             f"title={o.title.strip()}", f"map={map_file}"]
    # The original numbers entry points four to an entry (the party's
    # places); scenario.txt numbers entries, and puts the party round the
    # first place.
    for eid, x, y, d in o.entries:
        if eid % 4 == 0:
            lines.append(f"entry={eid // 4},{','.join(map(str, spot(x, y)))},{d % 8}")
    lines.append(f"town={our_number(o.town)}")
    if o.transport:
        lines.append("transport=1")
    for left, top, right, bottom, target, entry in written_exits(o, number):
        lines.append(f"exit={left - x0},{top - y0},{right - x0},{bottom - y0},{our_number(target)},{entry}")
    for c in o.groups[2]:
        if c["kind"] not in enemy_kinds:
            continue
        x, y = spot(c["x"], c["y"])
        stats = enemy_kinds[c["kind"]]
        lines.append(f"enemy={c['kind']},{x},{y},{c['direction'] % 8},{stats}")
        if o.light != 2000 and c["light"] >= 0:
            lines.append(f"glow={c['light']}")
    for c in o.groups[1]:
        if c["kind"] in people_kinds:
            x, y = spot(c["x"], c["y"])
            name = PEOPLE_NAMES.get((number, c["id"]))
            if not name and number not in AREAS[1]:
                name = f"p{c['id']}"   # what scripts.py calls them
            lines.append(f"person={c['kind']},{x},{y},{c['direction'] % 8}" + (f",{name}" if name else ""))
            if o.light != 2000 and c["light"] >= 0:
                lines.append(f"glow={c['light']}")
    # In the dark, the original's lit objects (torches and braziers) become
    # lamps where they stood, and the area keeps its light level.
    if o.light != 2000:
        lines.append(f"light={o.light}")
        for c in o.groups[0] + o.groups[3]:
            if c["light"] >= 0:
                lines.append(f"lamp={','.join(map(str, spot(c['x'], c['y'])))},{c['light']}")
    folder = out / "Scenario" / f"{our_number(number):08d}"
    folder.mkdir(parents=True, exist_ok=True)
    (folder / "scenario.txt").write_text("\n".join(lines) + "\n")
    print(f"{our_number(number)} {map_file} {o.title}: {tw}x{th} tiles, {len(objects)} objects, {len(o.exits)} exits")


def our_stats(out, kinds):
    """What goes after kind, x, y, direction on each kind's enemy= lines
    until enemy_stats.py gives them the original's numbers: the wander
    radius, AI list and drop list the pack already gives that kind (or a
    goblin's), with stand-in numbers."""
    ours = {}
    for f in sorted((out / "Scenario").glob("*/scenario.txt")):
        for line in f.read_text().split("\n"):
            if line.startswith("enemy="):
                v = line[6:].split("#")[0].strip().split(",")
                ours.setdefault(int(v[0]), (v[6], v[7], v[8] if len(v) > 8 else "1"))
    fallback = ours.get(1, ("960", "goblin", "1"))
    return {k: "1,1,{},{},{},0,0,0,1,0,50,0".format(*ours.get(k, fallback)) for k in kinds}


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
    partners = out / "Character" / "PARTNER"
    people |= {1000000 + int(p.name) for p in partners.iterdir()} if partners.is_dir() else set()
    enemies = our_stats(out, {int(p.name) for p in (out / "Character" / "ENEMY").iterdir()})
    areas = [int(a) for a in sys.argv[3:]] or list(AREAS)
    rng = random.Random(1)
    for area in areas:
        for number in AREAS[area]:
            # The first area's maps were laid out one after another from one
            # seed; later areas seed each map by its number, so one map can
            # be laid out again without changing the others.
            build(game, out, number, frames, sizes, rules, people, enemies, rng if area == 1 else random.Random(number))


if __name__ == "__main__":
    main()
