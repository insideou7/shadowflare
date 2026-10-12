#!/usr/bin/env python3
"""Lay out the open content's first area after the original's: the opening
fields, Remote Town, the Dusty Ruins, Cold Svalt Town and the forests and
four dungeons round it (29 maps). Needs the player's own copy of the game.

    python3 tools/content/first_area.py <game folder> <content pack>

Each map follows its original's routes, drawn only with our art: where the
ground can be walked on (the original's walkability grid, cropped to the
part that is drawn), where its paths and clearings are (grass or dirt from
the original ground's colour, one choice per tile corner), where its exits
lead and where the player arrives, the monsters and townspeople in it, and
its landmarks (buildings, stairs, fences, pipes) as content/skin.txt maps
them. Forests and dungeon walls are our own: trees are scattered over
ground that cannot be walked on, and walls run along the edges of the
dungeons' floors. No original picture, tile or sprite is copied.

Writes Map/<original map name>.txt and Scenario/<original number>/ for
each, keeping the original's scenario numbers so exits match. Needs
Pillow and numpy.
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

ENEMY_AI = "goblin"


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


def build(game, out, number, frames, rules, people_kinds, enemy_kinds, rng):
    o = Original(game, number)
    dungeon = number in DUNGEONS
    # Crop to the drawn part of the map: cells whose ground has a picture.
    jh, jw = o.judge.shape
    drawn = np.zeros((jh, jw), bool)
    for jy in range(0, jh, 2):
        for jx in range(0, jw, 2):
            x, y = (jx + o.jx0) * CELL + CELL, (jy + o.jy0) * CELL + CELL
            sx, sy = (x - y) * PROJ[0] / 100, (x + y) * PROJ[1] / 100
            w, h, cw, ch = o.chips
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

    # Walkability, cell by cell; everything off the drawn map is blocked.
    blocked = np.ones((th * PER, tw * PER), bool)
    for cy in range(th * PER):
        for cx in range(tw * PER):
            x, y = x0 + cx * CELL + CELL // 2, y0 + cy * CELL + CELL // 2
            jx, jy = int(x // CELL) - o.jx0, int(y // CELL) - o.jy0
            if 0 <= jy < jh and 0 <= jx < jw and drawn[jy, jx]:
                blocked[cy, cx] = o.blocked(x, y)
    inside = np.zeros((th, tw), bool)
    for ty in range(th):
        for tx in range(tw):
            x, y = world(tx, ty)
            jx, jy = int((x + CELL) // CELL) - o.jx0, int((y + CELL) // CELL) - o.jy0
            inside[ty, tx] = 0 <= jy < jh and 0 <= jx < jw and drawn[jy, jx]
    open_ = ~blocked.reshape(th, PER, tw, PER).all(axis=(1, 3))   # a tile with any walkable cell

    # Ground: corner terrain from the original's colour; 1 = grass or brick.
    terrain = np.ones((th + 1, tw + 1), int)
    for ty in range(th + 1):
        for tx in range(tw + 1):
            c = o.colour(*world(tx, ty))
            if c is None:
                continue
            if dungeon:
                terrain[ty, tx] = 0 if rng.random() < 0.08 else 1   # rubble here and there
            else:
                terrain[ty, tx] = 1 if c[1] > c[0] * 1.02 else 0
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
    taken = np.zeros((th, tw), bool)

    def place(name, wx, wy):
        if name in frames:
            objects.append((wy + wx, f"{int(wx - x0)},{int(wy - y0)},1,{frames[name]}"))
            tx, ty = int((wx - x0) // TILE), int((wy - y0) // TILE)
            taken[max(0, ty - 1):ty + 2, max(0, tx - 1):tx + 2] = True

    # Landmarks where the original has them.
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
            place(name.rstrip("!"), ob["x"] + fx + dx, ob["y"] + fy + dy)

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
    judge = ["".join("x" if b else "." for b in line) for line in blocked]
    sheet = "Map/floor.sheet" if dungeon else "Map/ground.sheet"
    with open(out / "Map" / f"{o.name}.txt", "w", newline="\n") as f:
        f.write(f"# {o.title}, laid out after the original's {o.name}: generated by tools/content/first_area.py\n")
        f.write(f"size={tw},{th}\ntiles={TILE}\ncell={CELL},{CELL}\njudge=0,0,{tw * PER},{th * PER}\n")
        f.write(f"bank={sheet}\nbank=Map/props.sheet\n\n[ground]\n" + "\n".join(rows))
        f.write("\n\n[judge]\n" + "\n".join(judge) + "\n\n[objects]\n" + "\n".join(t for _, t in objects) + "\n")

    lines = [f"# generated by tools/content/first_area.py from the original's scenario {number}",
             f"title={o.title}", f"map={o.name}"]
    for eid, x, y, d in o.entries:
        lines.append(f"entry={eid},{x - x0},{y - y0},{d % 8}")
    for left, top, right, bottom, target, entry in o.exits:
        lines.append(f"exit={left - x0},{top - y0},{right - x0},{bottom - y0},{target},{entry}")
    for c in o.groups[2]:
        if c["kind"] not in enemy_kinds:
            continue
        w = c["words"]
        wander = max(abs(v) for v in w[1:5]) or 640
        lines.append(f"enemy={c['kind']},{c['x'] - x0},{c['y'] - y0},{c['direction'] % 8},{w[8]},{w[23]},{wander},"
                     f"{ENEMY_AI},{w[14]},{w[49]},{w[50]},{w[51]},{w[13]},{w[9]},{w[29]},{w[10]},{w[11]},{w[6]}")
    for c in o.groups[1]:
        if c["kind"] in people_kinds:
            lines.append(f"person={c['kind']},{c['x'] - x0},{c['y'] - y0},{c['direction'] % 8}")
    folder = out / "Scenario" / f"{number:08d}"
    folder.mkdir(parents=True, exist_ok=True)
    (folder / "scenario.txt").write_text("\n".join(lines) + "\n")
    print(f"{number} {o.name} {o.title}: {tw}x{th} tiles, {len(objects)} objects, {len(o.exits)} exits")


def main():
    game, out = Path(sys.argv[1]), Path(sys.argv[2])
    if not (game / "Map").is_dir():
        game = next(p.parent for p in game.rglob("Map") if (p / "Pattern").is_dir())
    frames = {}
    for line in (out / "Map" / "props.sheet").read_text().split("\n"):
        if line.startswith("frame="):
            v = line[6:].split(",")
            if len(v) == 7:
                frames[v[6].strip()] = len(frames)
            else:
                frames[f"#{len(frames)}"] = len(frames)
    _, rules = read_skin(out / "skin.txt")
    people = {int(p.name) for p in (out / "Character" / "PEOPLE").iterdir()}
    enemies = {int(p.name) for p in (out / "Character" / "ENEMY").iterdir()}
    rng = random.Random(1)
    for number in FIELDS + DUNGEONS:
        build(game, out, number, frames, rules, people, enemies, rng)


if __name__ == "__main__":
    main()
