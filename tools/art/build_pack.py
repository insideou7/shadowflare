#!/usr/bin/env python3
"""Assemble a content pack from the art made in tools/art.

    python3 tools/art/build_pack.py <rendered art folder> <output pack>

The art folder holds what props.py and creatures.py rendered:
props/, town/, dungeon/, cave/, fort/, hero/, the monsters (one folder per
ENEMIES entry) and townspeople (person<N>/). This script adds the ground
tiles (ground.py), lays out the test maps (a meadow with a goblin camp, the
waterworks under it, the caves and coal mine below that and the fort above
the mine) and writes the
pack's text files. Needs Pillow and numpy.
"""

import random
import shutil
import sys
from pathlib import Path

import numpy as np
from PIL import Image

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
sys.path.insert(0, str(HERE.parent / "content"))
import ground  # noqa: E402
from import_flare import Atlas  # noqa: E402

TILE = ground.TILE
CELL = 160
PER = TILE // CELL

PLAYER_CHARTS = [(0, "idle"), (1, "walk"), (3, "hit"), (5, "attack")]
ENEMY_CHARTS = [(0, "idle"), (1, "walk"), (2, "hit"), (3, "death"), (4, "attack")]
PEOPLE_CHARTS = [(0, "idle"), (1, "walk")]
# Monster art and the Character/ENEMY folder each one fills.
ENEMIES = {"fly": 0, "goblin": 1, "ghoul": 2, "demon": 3, "golem": 4, "shaman": 5, "spider": 6, "bigspider": 7,
           "archer": 10, "swordsman": 11, "dragon": 12, "ogre": 13}


def cropped(path, ax, ay):
    im = Image.open(path).convert("RGBA")
    box = im.getbbox() or (0, 0, 1, 1)
    return im.crop(box), ax - box[0], ay - box[1]


def character(src, dst, charts):
    lines = (src / "frames.txt").read_text().split("\n")
    ax, ay = map(int, lines[0].split()[1:])
    anims = {}
    for line in lines[1:]:
        if line.startswith("animation"):
            _, name, frames, loop = line.split()
            anims[name] = (int(frames), loop == "loop")
    dst.mkdir(parents=True, exist_ok=True)
    atlas = Atlas(width=4096)  # big characters (the golem) overflow a narrower sheet
    out = ["# Rendered by tools/art/creatures.py.", "sheet=Animation.sheet"]
    for chart, name in charts:
        frames, loop = anims[name]
        out.append(f"\n[chart {chart} {'loop' if loop else 'once'}]")
        for d in range(8):
            idx = [atlas.add((name, d, f), cropped(src / f"{name}_{d}_{f}.png", ax, ay)) for f in range(frames)]
            out.append(f"{d}=" + ",".join(map(str, idx)))
    atlas.write(dst / "Animation.sheet", "Animation.png")
    return "\n".join(out) + "\n"


def meadow(rng, size):
    """Corner terrain (1 = grass), props and the goblin camp's fence."""
    n = size + 1
    yy, xx = np.mgrid[0:n, 0:n]
    terrain = np.ones((n, n), dtype=int)
    # A winding dirt path from the west edge to a camp clearing.
    for x in range(n):
        y = int(round(n * 0.55 + 3 * np.sin(x / 4.0)))
        terrain[max(0, y - 1):y + 1, x] = 0
    cx, cy, r = int(n * 0.68), int(n * 0.42), 6
    terrain[(xx - cx) ** 2 + (yy - cy) ** 2 < r * r] = 0
    # Bare patches here and there.
    for _ in range(10):
        px, py, pr = rng.randrange(n), rng.randrange(n), rng.uniform(1.0, 2.2)
        terrain[(xx - px) ** 2 + (yy - py) ** 2 < pr * pr] = 0
    return terrain, (cx, cy, r)


def main():
    art, out = Path(sys.argv[1]), Path(sys.argv[2])
    for sub in ("Map", "Scenario", "Character", "Player"):
        shutil.rmtree(out / sub, ignore_errors=True)
    out.mkdir(parents=True, exist_ok=True)
    rng = random.Random(7)

    (out / "Player" / "Male" / "Animation00.anim").parent.mkdir(parents=True, exist_ok=True)
    (out / "Player" / "Male" / "Animation00.anim").write_text(character(art / "hero", out / "Player" / "Male", PLAYER_CHARTS))
    for name, kind in ENEMIES.items():
        if (art / name / "frames.txt").exists():
            folder = out / "Character" / "ENEMY" / f"{kind:08d}"
            (folder / "Animation.anim").parent.mkdir(parents=True, exist_ok=True)
            (folder / "Animation.anim").write_text(character(art / name, folder, ENEMY_CHARTS))
    people = sorted(int(p.name[6:]) for p in art.glob("person*") if (p / "frames.txt").exists())
    for kind in people:
        folder = out / "Character" / "PEOPLE" / f"{kind:08d}"
        folder.mkdir(parents=True, exist_ok=True)
        (folder / "Animation.anim").write_text(character(art / f"person{kind}", folder, PEOPLE_CHARTS))
    print("characters done")

    # Bank 0: ground tiles. Bank 1: props.
    size = 40
    terrain, (cx, cy, r) = meadow(rng, size)
    # A bare, trodden square in the village round the well.
    yy, xx = np.mgrid[0:size + 1, 0:size + 1]
    terrain[(xx - 11) ** 2 + ((yy - 17) * 1.2) ** 2 < 10] = 0
    tiles = Atlas()
    for t in ground.build_tiles():
        tiles.add(len(tiles.frames), t)
    (out / "Map").mkdir(exist_ok=True)
    tiles.write(out / "Map" / "ground.sheet", "ground.png")
    props = Atlas()
    anchors = {}
    for folder, listing in (("props", "props.txt"), ("town", "town.txt"), ("dungeon", "dungeon.txt"),
                            ("cave", "cave.txt"), ("fort", "fort.txt")):
        if not (art / folder / listing).exists():
            continue
        for line in (art / folder / listing).read_text().split("\n"):
            if line:
                name, ax, ay = line.split()
                anchors[name] = props.add(name, cropped(art / folder / f"{name}.png", int(ax), int(ay)))
    props.write(out / "Map" / "props.sheet", "props.png")

    blocked = np.zeros((size * PER, size * PER), dtype=bool)
    blocked[0, :] = blocked[-1, :] = blocked[:, 0] = blocked[:, -1] = True
    objects = []

    def place(name, wx, wy, block=True):
        objects.append(f"{wx},{wy},1,{anchors[name]}")
        if block:
            blocked[min(size * PER - 1, wy // CELL), min(size * PER - 1, wx // CELL)] = True

    occupied = set()

    def building(name, tx, ty, x0, x1, y0, y1):
        """Place a large prop at a tile centre and block its footprint, given
        in Blender units (hundreds of world units) around that point."""
        wx, wy = int(tx * TILE), int(ty * TILE)
        place(name, wx, wy, block=False)
        for gy in range(int((wy + y0 * 100) // CELL), int((wy + y1 * 100) // CELL) + 1):
            for gx in range(int((wx + x0 * 100) // CELL), int((wx + x1 * 100) // CELL) + 1):
                if 0 <= gx < size * PER and 0 <= gy < size * PER:
                    blocked[gy, gx] = True
        for oy in range(int(ty + y0 / 3.2) - 1, int(ty + y1 / 3.2) + 2):
            for ox in range(int(tx + x0 / 3.2) - 1, int(tx + x1 / 3.2) + 2):
                occupied.add((ox, oy))

    if "cottage" in anchors:
        # The village, north of the path near where the player starts.
        building("cottage", 7.5, 13.5, -4.8, 6.6, -8.5, 4.0)
        building("hall", 15.0, 12.5, -7.6, 8.8, -9.0, 5.6)
        building("well", 11.0, 17.0, -1.4, 1.4, -1.4, 1.4)
        building("stall", 6.5, 18.0, -1.9, 1.9, -1.2, 1.2)
        building("stall", 8.7, 18.6, -1.9, 1.9, -1.2, 1.2)
        building("crate_yellow", 5.6, 19.2, -0.8, 0.8, -0.8, 0.8)
        building("crate_red", 9.8, 19.7, -0.8, 0.8, -0.8, 0.8)
        building("cart", 13.6, 17.5, -1.6, 4.0, -1.2, 1.2)
        building("signpost", 4.4, 19.6, -0.2, 0.2, -0.2, 0.2)
        building("barrel", 11.5, 10.6, -0.4, 0.4, -0.4, 0.4)
        building("barrel", 11.8, 10.9, -0.4, 0.4, -0.4, 0.4)
        building("sacks", 10.2, 11.0, -1.0, 1.0, -0.6, 0.6)
        building("firewood", 4.6, 12.0, -0.6, 0.6, -0.6, 0.6)
        building("tank", 18.8, 15.8, -1.5, 2.6, -1.0, 1.0)
        for tx, ty in ((3.5, 9.5), (19.5, 9.0), (12.0, 7.5), (21.0, 13.0)):
            building("oak", tx, ty, -0.5, 0.5, -0.5, 0.5)
        # Old ruins in the meadow south of the path.
        building("pillar", 9.0, 30.0, -1.0, 1.0, -1.0, 1.0)
        building("pillar_broken", 12.0, 30.0, -1.0, 1.0, -1.0, 1.0)
        building("pillar", 9.0, 33.0, -1.0, 1.0, -1.0, 1.0)
        building("standing_stone", 15.0, 31.5, -1.2, 1.2, -0.9, 0.9)
        for tx in range(7, 11):
            building("wall_x", tx + 0.5, 35.0, -1.6, 1.6, -0.3, 0.3)
        building("wall_broken", 11.5, 35.0, -1.6, 1.6, -0.3, 0.3)
        for ty in range(31, 35):
            building("wall_y", 6.0, ty + 0.5, -0.3, 0.3, -1.6, 1.6)
        building("log", 13.5, 27.5, -2.5, 2.5, -0.3, 0.3)
        building("stones", 16.5, 28.0, -0.3, 0.3, -0.3, 0.3)
    if "stairs_down" in anchors:
        # Steps down into the old waterworks, east of the ruins.
        building("stairs_down", 21.0, 36.0, -2.3, 2.3, -2.0, -1.0)
    # The camp: a ring of palisade along tile edges, open towards the path.
    for tx in range(cx - 4, cx + 4):
        for ty, gap in ((cy - 4, False), (cy + 4, tx in (cx - 1, cx))):
            if not gap:
                place("fence_x", tx * TILE + TILE // 2, ty * TILE, block=False)
                blocked[ty * PER, tx * PER:(tx + 1) * PER] = True
    for ty in range(cy - 4, cy + 4):
        for tx in (cx - 4, cx + 4):
            place("fence_y", tx * TILE, ty * TILE + TILE // 2, block=False)
            blocked[ty * PER:(ty + 1) * PER, tx * PER] = True
    for tx in range(cx - 5, cx + 5):
        for ty in range(cy - 5, cy + 5):
            occupied.add((tx, ty))
    # Forest along the edges, thinning inwards, and scattered rocks.
    for ty in range(size):
        for tx in range(size):
            if (tx, ty) in occupied or terrain[ty, tx] + terrain[ty + 1, tx + 1] < 2:
                continue
            edge = min(tx, ty, size - 1 - tx, size - 1 - ty)
            chance = 0.75 if edge < 3 else 0.35 if edge < 5 else 0.05
            if rng.random() < chance:
                jitter = lambda: rng.randrange(-TILE // 4, TILE // 4)
                place(f"tree{rng.randint(1, 3)}", tx * TILE + TILE // 2 + jitter(), ty * TILE + TILE // 2 + jitter())
            elif rng.random() < 0.03:
                place(f"rock{rng.randint(1, 2)}", tx * TILE + TILE // 2, ty * TILE + TILE // 2)

    rows = []
    for ty in range(size):
        row = []
        for tx in range(size):
            corners = (terrain[ty, tx], terrain[ty, tx + 1], terrain[ty + 1, tx], terrain[ty + 1, tx + 1])
            row.append(f"0:{ground.pattern_index(corners, tx, ty)}")
        rows.append(" ".join(row))
    judge = ["".join("x" if b else "." for b in line) for line in blocked]
    with open(out / "Map" / "meadow.txt", "w", newline="\n") as f:
        f.write("# A meadow with a goblin camp: generated by tools/art/build_pack.py\n")
        f.write(f"size={size},{size}\ntiles={TILE}\ncell={CELL},{CELL}\njudge=0,0,{size * PER},{size * PER}\n")
        f.write("bank=Map/ground.sheet\nbank=Map/props.sheet\n\n[ground]\n" + "\n".join(rows))
        f.write("\n\n[judge]\n" + "\n".join(judge) + "\n\n[objects]\n" + "\n".join(objects) + "\n")

    start_y = int(size * 0.55 + 1)
    lines = ["# generated by tools/art/build_pack.py", "title=Goblin Meadow", "map=meadow",
             f"entry=0,{3 * TILE},{start_y * TILE},2"]
    for _ in range(7):
        gx, gy = cx + rng.randint(-3, 2), cy + rng.randint(-3, 2)
        lines.append(f"enemy=1,{gx * TILE + TILE // 2},{gy * TILE + TILE // 2},{rng.randrange(8)},30,4,{2 * TILE},goblin")
    for _ in range(4):
        gx, gy = rng.randint(8, size - 8), rng.randint(8, size - 8)
        lines.append(f"enemy=1,{gx * TILE + TILE // 2},{gy * TILE + TILE // 2},{rng.randrange(8)},30,4,{3 * TILE},goblin")
    def enemy(name, tx, ty, hp, attack, wander):
        if (out / "Character" / "ENEMY" / f"{ENEMIES[name]:08d}").exists():
            lines.append(f"enemy={ENEMIES[name]},{int(tx * TILE)},{int(ty * TILE)},{rng.randrange(8)},{hp},{attack},"
                         f"{wander * TILE},goblin")

    # The camp's shaman, flies and ghouls in the woods, a golem by the ruins.
    enemy("shaman", cx + 0.5, cy - 1.5, 45, 6, 1)
    for _ in range(5):
        enemy("fly", rng.randint(24, size - 4), rng.randint(4, 14), 12, 2, 3)
    for _ in range(4):
        enemy("ghoul", rng.randint(22, size - 6), rng.randint(26, size - 4), 35, 5, 3)
    enemy("golem", 18.5, 33.5, 90, 9, 2)
    # Villagers stand about the square, the stalls and the houses.
    spots = [(10.0, 15.5), (12.6, 16.0), (7.2, 19.4), (9.6, 18.9), (6.0, 16.5), (13.5, 15.0), (16.0, 16.5),
             (9.0, 14.8), (11.9, 18.6), (5.5, 14.8), (17.2, 14.6), (14.8, 18.8), (8.2, 16.2), (3.8, 17.6)]
    for kind, (tx, ty) in zip(people, spots):
        lines.append(f"person={kind},{int(tx * TILE)},{int(ty * TILE)},{rng.randrange(8)}")
    if "stairs_down" in anchors:
        lines.append(f"entry=1,{21 * TILE},{int(37.2 * TILE)},0")
        lines.append(f"exit={int(20.4 * TILE)},{int(35.4 * TILE)},{int(21.6 * TILE)},{int(36.4 * TILE)},2,1")
        waterworks(out, anchors, rng)
    (out / "Scenario" / "00000001").mkdir(parents=True, exist_ok=True)
    (out / "Scenario" / "00000001" / "scenario.txt").write_text("\n".join(lines) + "\n")
    shutil.copy(HERE.parent / "content" / "ai.txt", out / "ai.txt")
    (out / "content.txt").write_text("# Art made for this project: see tools/art.\nname=ShadowFlare-style test\n"
                                     "start=meadow\nprojection=15,10\n")
    print(f"meadow: {len(objects)} objects")


def waterworks(out, anchors, rng):
    """A small dungeon under the meadow: four rooms of mossy block walls
    joined by corridors, with copper pipes, vats and gates, reached by the
    steps by the ruins. Writes Map/waterworks.txt and Scenario/00000002."""
    size = 24
    floor = Atlas()
    for t in ground.build_tiles(seed=3, kind="dungeon"):
        floor.add(len(floor.frames), t)
    floor.write(out / "Map" / "floor.sheet", "floor.png")
    rooms = [(2, 2, 10, 10), (14, 2, 22, 10), (2, 14, 10, 22), (14, 14, 22, 22)]
    # Corridors: (x0, y0, x1, y1) in tiles, two tiles wide.
    halls = [(10, 6, 14, 8), (16, 10, 18, 14), (6, 10, 8, 14), (10, 16, 14, 18)]
    open_ = np.zeros((size, size), dtype=bool)
    for x0, y0, x1, y1 in rooms + halls:
        open_[y0:y1, x0:x1] = True
    # Corner terrain: 1 = brick floor, 0 = rubble, in a few patches.
    n = size + 1
    terrain = np.ones((n, n), dtype=int)
    yy, xx = np.mgrid[0:n, 0:n]
    for _ in range(9):
        px, py, pr = rng.randrange(n), rng.randrange(n), rng.uniform(1.0, 2.0)
        terrain[(xx - px) ** 2 + (yy - py) ** 2 < pr * pr] = 0
    rows = []
    for ty in range(size):
        row = []
        for tx in range(size):
            if not open_[ty, tx]:
                row.append(".")
                continue
            corners = (terrain[ty, tx], terrain[ty, tx + 1], terrain[ty + 1, tx], terrain[ty + 1, tx + 1])
            row.append(f"0:{ground.pattern_index(corners, tx, ty)}")
        rows.append(" ".join(row))
    blocked = ~np.repeat(np.repeat(open_, PER, axis=0), PER, axis=1)
    objects = []

    def place(name, tx, ty, block=None):
        objects.append(f"{int(tx * TILE)},{int(ty * TILE)},1,{anchors[name]}")
        if block:
            x0, y0, x1, y1 = block
            blocked[int(y0 * PER):int(y1 * PER), int(x0 * PER):int(x1 * PER)] = True

    # Walls along every edge between open floor and solid rock, in two-tile
    # pieces; a few carry pipes or a grate.
    def fitting(k):
        return "" if k % 5 else rng.choice(["_pipes", "_grate", "_pipes"])

    k = 0
    for ty in range(size + 1):
        for tx in range(0, size, 2):
            above = ty > 0 and open_[ty - 1, tx:tx + 2].all()
            below = ty < size and open_[ty, tx:tx + 2].all()
            if above != below:
                place(f"dwall{fitting(k)}_x", tx + 1, ty, (tx, ty - 0.15, tx + 2, ty + 0.15))
                k += 1
    for tx in range(size + 1):
        for ty in range(0, size, 2):
            left = tx > 0 and open_[ty:ty + 2, tx - 1].all()
            right = tx < size and open_[ty:ty + 2, tx].all()
            if left != right:
                place(f"dwall{fitting(k)}_y", tx, ty + 1, (tx - 0.15, ty, tx + 0.15, ty + 2))
                k += 1
    # Fittings in the rooms.
    place("stairs_down", 4.0, 3.5, (2.5, 2, 5.5, 3.2))
    place("brazier", 2.7, 9.3, (2.4, 9, 3, 9.6))
    place("brazier", 9.3, 2.7, (9, 2.4, 9.6, 3))
    place("pipe_hub", 18.0, 6.0, (16.6, 4.6, 19.4, 7.4))
    place("pipe_x", 15.6, 3.2, (14.8, 2.8, 16.4, 3.6))
    place("pipe_valve", 20.4, 3.2, (19.6, 2.8, 21.2, 3.6))
    place("pipe_y", 21.0, 8.4, (20.6, 7.6, 21.4, 9.2))
    place("pipe_elbow", 15.2, 9.0, (14.6, 8.4, 15.8, 9.6))
    for vx, vy in ((3.0, 15.0), (3.0, 16.4), (9.0, 21.0)):
        place("vat", vx, vy, (vx - 0.35, vy - 0.35, vx + 0.35, vy + 0.35))
    place("gate_y", 9.9, 18.0, (9.8, 17, 10, 19))
    place("gate_x", 6.0, 21.9, (5, 21.8, 7, 22))
    place("hatch", 6.0, 6.0)
    place("hatch_big", 18.0, 18.0)
    place("hatch", 7.5, 17.0)
    place("obelisk", 18.0, 15.4, (17.6, 15, 18.4, 15.8))
    place("dpillar", 16.0, 20.0, (15.7, 19.7, 16.3, 20.3))
    place("dpillar", 20.0, 20.0, (19.7, 19.7, 20.3, 20.3))
    place("chest", 21.2, 21.2, (20.9, 20.9, 21.5, 21.5))
    place("chest", 3.0, 21.0, (2.7, 20.7, 3.3, 21.3))
    deeper = "rock_x" in anchors
    if deeper:
        # Steps further down, to the caves and the old coal mine.
        place("stairs_down", 20.5, 15.5, (19, 14, 22, 15.2))
    judge = ["".join("x" if b else "." for b in line) for line in blocked]
    with open(out / "Map" / "waterworks.txt", "w", newline="\n") as f:
        f.write("# The old waterworks: generated by tools/art/build_pack.py\n")
        f.write(f"size={size},{size}\ntiles={TILE}\ncell={CELL},{CELL}\njudge=0,0,{size * PER},{size * PER}\n")
        f.write("bank=Map/floor.sheet\nbank=Map/props.sheet\n\n[ground]\n" + "\n".join(rows))
        f.write("\n\n[judge]\n" + "\n".join(judge) + "\n\n[objects]\n" + "\n".join(objects) + "\n")
    lines = ["# generated by tools/art/build_pack.py", "title=Old Waterworks", "map=waterworks",
             f"entry=0,{4 * TILE},{int(4.6 * TILE)},0", f"entry=1,{4 * TILE},{int(4.6 * TILE)},0",
             f"exit={int(3.2 * TILE)},{int(2.3 * TILE)},{int(4.8 * TILE)},{int(3.2 * TILE)},1,1"]
    if deeper:
        lines += [f"entry=2,{int(20.5 * TILE)},{int(16.6 * TILE)},0",
                  f"exit={int(19.7 * TILE)},{int(14.3 * TILE)},{int(21.3 * TILE)},{int(15.3 * TILE)},3,1"]

    def enemy(name, tx, ty, hp, attack, wander):
        lines.append(f"enemy={ENEMIES[name]},{int(tx * TILE)},{int(ty * TILE)},{rng.randrange(8)},{hp},{attack},"
                     f"{wander * TILE},goblin")

    for tx, ty in ((16, 4), (20, 8), (17, 9), (12, 7)):
        enemy("goblin", tx, ty, 30, 4, 2)
    for tx, ty in ((5, 16), (8, 19), (4, 20)):
        enemy("ghoul", tx, ty, 35, 5, 2)
    for tx, ty in ((7, 7), (17, 12), (12, 17)):
        enemy("fly", tx, ty, 12, 2, 3)
    enemy("shaman", 19, 17, 45, 6, 1)
    enemy("golem", 17, 19.5, 90, 9, 1)
    (out / "Scenario" / "00000002").mkdir(parents=True, exist_ok=True)
    (out / "Scenario" / "00000002" / "scenario.txt").write_text("\n".join(lines) + "\n")
    print(f"waterworks: {len(objects)} objects")
    if deeper:
        caves(out, anchors, rng)


def caves(out, anchors, rng):
    """The caves under the waterworks and the coal mine dug into them: on
    the west, rough rock caverns; on the east, galleries lined with planks
    and timber. Writes Map/caves.txt and Scenario/00000003."""
    size = 28
    floor = Atlas()
    cave_tiles = ground.build_tiles(seed=5, kind="cave")
    for t in cave_tiles + ground.build_tiles(seed=6, kind="mine"):
        floor.add(len(floor.frames), t)
    floor.write(out / "Map" / "cavefloor.sheet", "cavefloor.png")
    mine_from = 15  # tiles east of this are the mine

    caverns = [(2, 2, 12, 10), (2, 14, 12, 24)]
    galleries = [(18, 2, 26, 8), (16, 18, 26, 26)]
    halls = [(6, 10, 8, 14), (12, 4, 18, 6), (20, 8, 22, 18), (12, 20, 16, 22)]
    open_ = np.zeros((size, size), dtype=bool)
    for x0, y0, x1, y1 in caverns + galleries + halls:
        open_[y0:y1, x0:x1] = True
    n = size + 1
    terrain = np.ones((n, n), dtype=int)
    yy, xx = np.mgrid[0:n, 0:n]
    for _ in range(14):
        px, py, pr = rng.randrange(n), rng.randrange(n), rng.uniform(1.0, 2.2)
        terrain[(xx - px) ** 2 + (yy - py) ** 2 < pr * pr] = 0
    rows = []
    for ty in range(size):
        row = []
        for tx in range(size):
            if not open_[ty, tx]:
                row.append(".")
                continue
            corners = (terrain[ty, tx], terrain[ty, tx + 1], terrain[ty + 1, tx], terrain[ty + 1, tx + 1])
            base = len(cave_tiles) if tx >= mine_from else 0
            row.append(f"0:{base + ground.pattern_index(corners, tx, ty)}")
        rows.append(" ".join(row))
    blocked = ~np.repeat(np.repeat(open_, PER, axis=0), PER, axis=1)
    objects = []

    def place(name, tx, ty, block=None):
        objects.append(f"{int(tx * TILE)},{int(ty * TILE)},1,{anchors[name]}")
        if block:
            x0, y0, x1, y1 = block
            blocked[int(y0 * PER):int(y1 * PER), int(x0 * PER):int(x1 * PER)] = True

    def wall(tx, ty, along_y, k):
        mine = tx >= mine_from
        extra = "" if k % 4 else ("_broken" if mine else "_props")
        name = f"{'mine' if mine else 'rock'}{extra}_{'y' if along_y else 'x'}"
        if along_y:
            place(name, tx, ty + 1, (tx - 0.15, ty, tx + 0.15, ty + 2))
        else:
            place(name, tx + 1, ty, (tx, ty - 0.15, tx + 2, ty + 0.15))

    k = 0
    for ty in range(size + 1):
        for tx in range(0, size, 2):
            above = ty > 0 and open_[ty - 1, tx:tx + 2].all()
            below = ty < size and open_[ty, tx:tx + 2].all()
            if above != below:
                wall(tx, ty, False, k)
                k += 1
    for tx in range(size + 1):
        for ty in range(0, size, 2):
            left = tx > 0 and open_[ty:ty + 2, tx - 1].all()
            right = tx < size and open_[ty:ty + 2, tx].all()
            if left != right:
                wall(tx, ty, True, k)
                k += 1
    # The caverns: stone steps back up, columns of rock, glowing crystals.
    place("rock_stairs", 6.0, 3.0, (4.5, 2, 7.5, 3.6))
    for px, py in ((4.0, 7.5), (10.0, 6.0), (4.5, 18.0), (9.5, 20.5)):
        place("rock_pillar", px, py, (px - 0.4, py - 0.4, px + 0.4, py + 0.4))
    for name, px, py in (("crystal_blue", 3.0, 15.2), ("crystal_red", 11.0, 15.0), ("crystal_blue", 11.2, 23.0)):
        place(name, px, py, (px - 0.4, py - 0.4, px + 0.4, py + 0.4))
    place("chest_silver", 3.0, 23.0, (2.7, 22.7, 3.3, 23.3))
    place("lantern", 7.6, 9.2)
    # The mine: timber frames at the gallery mouths, props, kegs and crates.
    place("frame_y", 18.0, 5.0, (17.9, 4, 18.1, 4.2))
    place("frame_x", 21.0, 8.0)
    place("frame_x", 21.0, 18.0)
    place("frame_y", 16.0, 21.0)
    for px in (20.0, 23.0):
        place("pit_prop", px, 2.6, (px - 0.2, 2.4, px + 0.2, 2.8))
    place("lantern", 25.4, 2.8)
    place("lantern", 16.6, 18.8)
    place("keg", 25.2, 7.2, (24.9, 6.9, 25.5, 7.5))
    place("keg", 24.6, 7.4, (24.3, 7.1, 24.9, 7.7))
    place("long_crate", 23.5, 3.0, (22.8, 2.7, 24.2, 3.3))
    place("lever", 19.0, 7.4, (18.8, 7.2, 19.2, 7.6))
    place("mine_pillar", 19.0, 22.0, (18.6, 21.6, 19.4, 22.4))
    place("mine_pillar", 23.0, 22.0, (22.6, 21.6, 23.4, 22.4))
    place("wooden_stairs", 24.5, 19.5, (23, 18, 26, 21))
    place("warp_circle", 21.0, 23.0)
    place("chest_gold", 25.2, 25.2, (24.9, 24.9, 25.5, 25.5))
    place("chest_open", 17.0, 25.2, (16.7, 24.9, 17.3, 25.5))
    judge = ["".join("x" if b else "." for b in line) for line in blocked]
    with open(out / "Map" / "caves.txt", "w", newline="\n") as f:
        f.write("# The caves and the old coal mine: generated by tools/art/build_pack.py\n")
        f.write(f"size={size},{size}\ntiles={TILE}\ncell={CELL},{CELL}\njudge=0,0,{size * PER},{size * PER}\n")
        f.write("bank=Map/cavefloor.sheet\nbank=Map/props.sheet\n\n[ground]\n" + "\n".join(rows))
        f.write("\n\n[judge]\n" + "\n".join(judge) + "\n\n[objects]\n" + "\n".join(objects) + "\n")
    lines = ["# generated by tools/art/build_pack.py", "title=Caves and Coal Mine", "map=caves",
             f"entry=0,{6 * TILE},{int(4.4 * TILE)},0", f"entry=1,{6 * TILE},{int(4.4 * TILE)},0",
             f"exit={int(5.2 * TILE)},{int(2.4 * TILE)},{int(6.8 * TILE)},{int(3.4 * TILE)},2,2"]

    def enemy(name, tx, ty, hp, attack, wander):
        if (out / "Character" / "ENEMY" / f"{ENEMIES[name]:08d}").exists():
            lines.append(f"enemy={ENEMIES[name]},{int(tx * TILE)},{int(ty * TILE)},{rng.randrange(8)},{hp},{attack},"
                         f"{wander * TILE},goblin")

    for tx, ty in ((8, 7), (5, 9), (10, 3), (7, 12)):
        enemy("spider", tx, ty, 25, 4, 2)
    for tx, ty in ((5, 20), (8, 16), (10, 22)):
        enemy("bigspider", tx, ty, 50, 7, 2)
    enemy("demon", 9, 5, 40, 6, 3)
    enemy("demon", 6, 17, 40, 6, 3)
    for tx, ty in ((20, 4), (24, 5), (21, 12)):
        enemy("swordsman", tx, ty, 45, 7, 2)
    for tx, ty in ((25, 3), (21, 15)):
        enemy("archer", tx, ty, 35, 6, 1)
    enemy("ogre", 14, 21, 80, 10, 1)
    enemy("dragon", 21.5, 22.5, 300, 16, 1)
    if "fwall_x" in anchors:
        # The mine's wooden stairs climb up into the fort.
        lines += [f"entry=2,{int(22.0 * TILE)},{int(19.5 * TILE)},0",
                  f"exit={int(22.4 * TILE)},{int(18.6 * TILE)},{int(23.0 * TILE)},{int(20.4 * TILE)},4,1"]
        fort(out, anchors, rng)
    (out / "Scenario" / "00000003").mkdir(parents=True, exist_ok=True)
    (out / "Scenario" / "00000003" / "scenario.txt").write_text("\n".join(lines) + "\n")
    print(f"caves: {len(objects)} objects")



def fort(out, anchors, rng):
    """The thieves' fort above the coal mine and the wizard's tower room
    beside it: grey stone halls with iron doors and torch brackets, a
    teleport dais and levers, and the tower's boiler behind iron railings.
    Writes Map/fort.txt and Scenario/00000004."""
    size = 26
    floor = Atlas()
    for t in ground.build_tiles(seed=8, kind="fort"):
        floor.add(len(floor.frames), t)
    floor.write(out / "Map" / "fortfloor.sheet", "fortfloor.png")
    halls = [(2, 2, 12, 12), (14, 2, 24, 10), (2, 14, 24, 24)]
    passages = [(12, 6, 14, 8), (6, 12, 8, 14), (18, 10, 20, 14)]
    open_ = np.zeros((size, size), dtype=bool)
    for x0, y0, x1, y1 in halls + passages:
        open_[y0:y1, x0:x1] = True
    n = size + 1
    terrain = np.ones((n, n), dtype=int)
    yy, xx = np.mgrid[0:n, 0:n]
    for _ in range(10):
        px, py, pr = rng.randrange(n), rng.randrange(n), rng.uniform(1.0, 2.0)
        terrain[(xx - px) ** 2 + (yy - py) ** 2 < pr * pr] = 0
    rows = []
    for ty in range(size):
        row = []
        for tx in range(size):
            if not open_[ty, tx]:
                row.append(".")
                continue
            corners = (terrain[ty, tx], terrain[ty, tx + 1], terrain[ty + 1, tx], terrain[ty + 1, tx + 1])
            row.append(f"0:{ground.pattern_index(corners, tx, ty)}")
        rows.append(" ".join(row))
    blocked = ~np.repeat(np.repeat(open_, PER, axis=0), PER, axis=1)
    objects = []

    def place(name, tx, ty, block=None):
        objects.append(f"{int(tx * TILE)},{int(ty * TILE)},1,{anchors[name]}")
        if block:
            x0, y0, x1, y1 = block
            blocked[int(y0 * PER):int(y1 * PER), int(x0 * PER):int(x1 * PER)] = True

    # Walls on every edge between floor and solid stone; every so often a
    # torch bracket, an old bloodstain or a shut iron door.
    def variant(k):
        return ["", "", "_torch", "", "_blood", "", "door"][k % 7]

    k = 0
    for ty in range(size + 1):
        for tx in range(0, size, 2):
            above = ty > 0 and open_[ty - 1, tx:tx + 2].all()
            below = ty < size and open_[ty, tx:tx + 2].all()
            if above != below:
                v = variant(k)
                name = "fdoor_x" if v == "door" else f"fwall{v}_x"
                place(name, tx + 1, ty, (tx, ty - 0.15, tx + 2, ty + 0.15))
                k += 1
    for tx in range(size + 1):
        for ty in range(0, size, 2):
            left = tx > 0 and open_[ty:ty + 2, tx - 1].all()
            right = tx < size and open_[ty:ty + 2, tx].all()
            if left != right:
                v = variant(k)
                name = "fdoor_y" if v == "door" else f"fwall{v}_y"
                place(name, tx, ty + 1, (tx - 0.15, ty, tx + 0.15, ty + 2))
                k += 1
    # The entrance hall: the stairway down to the mine, and pillars.
    place("fstairs", 7.0, 2.9, (5.9, 2, 8.1, 3.3))
    for px, py in ((4.0, 6.0), (10.0, 6.0), (4.0, 10.0), (10.0, 10.0)):
        place("fpillar", px, py, (px - 0.4, py - 0.4, px + 0.4, py + 0.4))
    place("chest", 2.8, 11.2, (2.5, 10.9, 3.1, 11.5))
    # The east hall: a teleport dais worked by two levers.
    place("teleporter", 19.0, 6.0, (18.4, 5.4, 19.6, 6.6))
    place("switch_up", 15.0, 2.8, (14.8, 2.6, 15.2, 3.0))
    place("switch_down", 23.0, 2.8, (22.8, 2.6, 23.2, 3.0))
    place("fpillar", 16.0, 8.5, (15.6, 8.1, 16.4, 8.9))
    place("fpillar", 22.0, 8.5, (21.6, 8.1, 22.4, 8.9))
    # The tower room: the boiler fenced off with railings, pipes along the
    # floor, glowing pads and a broken ring of slabs.
    place("boiler", 20.5, 18.5, (19.4, 17.4, 21.6, 19.6))
    for tx in (18, 21):
        place("railing_x", tx + 0.75, 16.5, (tx, 16.4, tx + 1.5, 16.6))
    place("railing_plain_y", 17.5, 18.75, (17.4, 17.5, 17.6, 20))
    place("railing_plain_y", 23.5, 18.75, (23.4, 17.5, 23.6, 20))
    for px, py in ((17.5, 16.5), (23.5, 16.5), (17.5, 20.5), (23.5, 20.5)):
        place("iron_post", px, py, (px - 0.15, py - 0.15, px + 0.15, py + 0.15))
    place("floor_pipe_x", 12.0, 22.6)
    place("floor_pipe_x", 14.0, 22.6)
    place("pipe_bend", 15.0, 22.6)
    place("floor_pipe_y", 15.2, 20.6)
    place("portal_blue", 5.0, 17.0)
    place("portal_red", 9.0, 17.0)
    place("broken_ring", 6.0, 21.0)
    place("obelisk", 3.4, 22.6, (3.0, 22.2, 3.8, 23.0))
    judge = ["".join("x" if b else "." for b in line) for line in blocked]
    with open(out / "Map" / "fort.txt", "w", newline="\n") as f:
        f.write("# The thieves' fort and the tower room: generated by tools/art/build_pack.py\n")
        f.write(f"size={size},{size}\ntiles={TILE}\ncell={CELL},{CELL}\njudge=0,0,{size * PER},{size * PER}\n")
        f.write("bank=Map/fortfloor.sheet\nbank=Map/props.sheet\n\n[ground]\n" + "\n".join(rows))
        f.write("\n\n[judge]\n" + "\n".join(judge) + "\n\n[objects]\n" + "\n".join(objects) + "\n")
    lines = ["# generated by tools/art/build_pack.py", "title=Fort of Thieves", "map=fort",
             f"entry=0,{7 * TILE},{int(4.6 * TILE)},0", f"entry=1,{7 * TILE},{int(4.6 * TILE)},0",
             f"exit={int(6.4 * TILE)},{int(3.3 * TILE)},{int(7.6 * TILE)},{int(3.9 * TILE)},3,2"]

    def enemy(name, tx, ty, hp, attack, wander):
        if (out / "Character" / "ENEMY" / f"{ENEMIES[name]:08d}").exists():
            lines.append(f"enemy={ENEMIES[name]},{int(tx * TILE)},{int(ty * TILE)},{rng.randrange(8)},{hp},{attack},"
                         f"{wander * TILE},goblin")

    # The thieves hold the halls; the wizard's creatures haunt the tower room.
    for tx, ty in ((5, 8), (9, 4), (16, 4), (22, 6), (19, 9)):
        enemy("archer", tx, ty, 35, 6, 1)
    for tx, ty in ((8, 9), (17, 7), (21, 4)):
        enemy("ogre", tx, ty, 80, 10, 1)
    for tx, ty in ((7, 19), (12, 17)):
        enemy("demon", tx, ty, 40, 6, 3)
    enemy("golem", 14, 20, 90, 9, 1)
    enemy("shaman", 10, 21, 45, 6, 1)
    (out / "Scenario" / "00000004").mkdir(parents=True, exist_ok=True)
    (out / "Scenario" / "00000004" / "scenario.txt").write_text("\n".join(lines) + "\n")
    print(f"fort: {len(objects)} objects")


if __name__ == "__main__":
    main()
