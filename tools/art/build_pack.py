#!/usr/bin/env python3
"""Assemble a content pack from the art made in tools/art.

    python3 tools/art/build_pack.py <rendered art folder> <output pack>

The art folder holds what props.py and creatures.py rendered:
props/, goblin/ and hero/. This script adds the ground tiles
(ground.py), lays out a test map, a meadow with a goblin camp, and writes
the pack's text files. Needs Pillow and numpy.
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
    atlas = Atlas()
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
    goblin_dir = out / "Character" / "ENEMY" / "00000001"
    (goblin_dir / "Animation.anim").parent.mkdir(parents=True, exist_ok=True)
    (goblin_dir / "Animation.anim").write_text(character(art / "goblin", goblin_dir, ENEMY_CHARTS))
    print("characters done")

    # Bank 0: ground tiles. Bank 1: props.
    size = 40
    terrain, (cx, cy, r) = meadow(rng, size)
    tiles = Atlas()
    for t in ground.build_tiles():
        tiles.add(len(tiles.frames), t)
    (out / "Map").mkdir(exist_ok=True)
    tiles.write(out / "Map" / "ground.sheet", "ground.png")
    props = Atlas()
    anchors = {}
    for line in (art / "props" / "props.txt").read_text().split("\n"):
        if line:
            name, ax, ay = line.split()
            anchors[name] = props.add(name, cropped(art / "props" / f"{name}.png", int(ax), int(ay)))
    props.write(out / "Map" / "props.sheet", "props.png")

    blocked = np.zeros((size * PER, size * PER), dtype=bool)
    blocked[0, :] = blocked[-1, :] = blocked[:, 0] = blocked[:, -1] = True
    objects = []

    def place(name, wx, wy, block=True):
        objects.append(f"{wx},{wy},1,{anchors[name]}")
        if block:
            blocked[min(size * PER - 1, wy // CELL), min(size * PER - 1, wx // CELL)] = True

    occupied = set()
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
    (out / "Scenario" / "00000001").mkdir(parents=True, exist_ok=True)
    (out / "Scenario" / "00000001" / "scenario.txt").write_text("\n".join(lines) + "\n")
    shutil.copy(HERE.parent / "content" / "ai.txt", out / "ai.txt")
    (out / "content.txt").write_text("# Art made for this project: see tools/art.\nname=ShadowFlare-style test\n"
                                     "start=meadow\nprojection=15,10\n")
    print(f"meadow: {len(objects)} objects")


if __name__ == "__main__":
    main()
