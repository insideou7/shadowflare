#!/usr/bin/env python3
"""Build the open content pack (content/) from Flare: Empyrean Campaign.

Flare's art, maps and data are CC-BY-SA 3.0 (https://github.com/flareteam/flare-game).
This script turns a chosen part of them into the engine's open content
format (docs/content.md):

- characters: Flare's layered animation sheets become one composited
  sprite sheet and an .anim file per character,
- maps: Flare map files (as saved by Tiled) become Map/<name>.txt, using only
  the tiles they need, plus Scenario/<n>/scenario.txt with their monsters
  and the exits between the imported maps.

Monster stats and behaviour are our own, set in this file and in
tools/content/ai.txt. Usage:

    python3 tools/content/import_flare.py /path/to/flare-game content

Needs Pillow. The output is deterministic, so re-running it on the same
Flare checkout gives the same files.
"""

import argparse
import random
import shutil
import sys
from pathlib import Path

from PIL import Image

HERE = Path(__file__).resolve().parent

# Engine conventions (src/formats/map_layers.h, src/game/character.h).
TILE_WORLD = 480       # world units per tile edge: 192x96 tiles with projection 20/10
JUDGE_CELL = 160       # walkability cell, world units
TICK_MS = 50           # the engine runs at 20 ticks a second
PROJECTION = (20, 10)

# Our directions start at south and turn counter-clockwise on screen;
# Flare's start at west and turn clockwise.
def flare_direction(ours):
    return (6 - ours) % 8

# Charts the engine plays, by number: (chart, Flare animation, loops).
PLAYER_CHARTS = [(0, "stance", True), (1, "run", True), (3, "hit", False), (5, "swing", False)]
ENEMY_CHARTS = [(0, "stance", True), (1, "run", True), (2, "hit", False), (3, "die", False), (4, "swing", False)]

# The player: Flare's male avatar layers, drawn in the order hero_layers.txt gives.
PLAYER_LAYERS = {
    "head": "animations/avatar/male/head_short.txt",
    "chest": "animations/avatar/male/cloth_shirt.txt",
    "legs": "animations/avatar/male/cloth_pants.txt",
    "hands": "animations/avatar/male/default_hands.txt",
    "feet": "animations/avatar/male/cloth_sandals.txt",
    "main": "animations/avatar/male/shortsword.txt",
}

# Monster kinds (Character/ENEMY/<kind>/), the Flare map categories that
# become them, and their stats: hp, attack, wander radius, AI list.
# level matters for item drops; gold = (chance in percent, min, max) dropped on death.
ENEMIES = {
    1: dict(name="goblin", anim="animations/enemies/goblin.txt", categories=["goblin"], hp=30, attack=4, ai="goblin",
            level=1, gold=(30, 2, 12)),
    2: dict(name="goblin spearman", anim="animations/enemies/goblin_elite.txt",
            categories=["goblin_spearman", "goblin_chief"], hp=60, attack=7, ai="goblin", level=3, gold=(50, 10, 30)),
    3: dict(name="skeleton", anim="animations/enemies/skeleton.txt", categories=["undead"], hp=50, attack=6, ai="skeleton",
            level=2, gold=(30, 5, 20)),
}

# Items lying on each map from the start: (class, code, world x, world y),
# as defined in content/items.txt.
MAP_ITEMS = {
    "goblin_camp": [(0, 0, 3120, 1680), (1, 1000000, 3120, 720)],
}

# Maps to import, in scenario order; the first is where the game starts.
MAPS = ["goblin_camp", "goblin_cave"]

MODS = ["mods/empyrean_campaign", "mods/fantasycore"]

# Grassland tiles 92 to 95 are small blue triangles that Flare's maps put
# along the edge of the void; they are not meant to be seen, so leave them out.
SKIP_TILES = {"tilesetdefs/tileset_grassland.txt": {92, 93, 94, 95}}


class Flare:
    def __init__(self, root):
        self.root = Path(root)

    def find(self, rel):
        for mod in MODS:
            p = self.root / mod / rel
            if p.exists():
                return p
        sys.exit(f"not found in the Flare data: {rel}")


def read_lines(path):
    for raw in path.read_text(encoding="utf-8").splitlines():
        line = raw.strip()
        if line and not line.startswith("#"):
            yield line


# --- Characters ---------------------------------------------------------

def parse_animation(path):
    """Flare animation definition -> (image path, {name: animation})."""
    image, anims, cur = None, {}, None
    for line in read_lines(path):
        if line.startswith("["):
            cur = anims.setdefault(line.strip("[]"), {"frames": 1, "duration": 100, "type": "looped", "cells": {}})
            continue
        key, _, value = line.partition("=")
        if key == "image":
            image = value
        elif cur is None:
            continue
        elif key == "frames":
            cur["frames"] = int(value)
        elif key == "duration":
            cur["duration"] = int(value[:-2]) if value.endswith("ms") else int(float(value[:-1]) * 1000)
        elif key == "type":
            cur["type"] = value
        elif key == "frame":
            i, d, x, y, w, h, ox, oy = map(int, value.split(","))
            cur["cells"][(i, d)] = (x, y, w, h, ox, oy)
    return image, anims


def tick_sequence(anim):
    """Frame numbers, one per engine tick, for one play of an animation."""
    n = anim["frames"]
    order = list(range(n))
    if anim["type"] == "back_forth" and n > 2:
        order += list(range(n - 2, 0, -1))
    per_frame = anim["duration"] / max(1, n) / TICK_MS
    seq, t = [], 0.0
    for f in order:
        t += per_frame
        while len(seq) < max(1, round(t)):
            seq.append(f)
    return seq


class Layer:
    def __init__(self, flare, anim_path):
        image, self.anims = parse_animation(flare.find(anim_path))
        self.image = Image.open(flare.find(image)).convert("RGBA")

    def cell(self, anim, frame, direction):
        a = self.anims.get(anim)
        if not a:
            return None
        c = a["cells"].get((frame, direction))
        if not c:
            return None
        x, y, w, h, ox, oy = c
        return self.image.crop((x, y, x + w, y + h)), ox, oy


def composite(parts):
    """Stack (image, anchor x, anchor y) parts on a shared anchor."""
    parts = [p for p in parts if p]
    if not parts:
        return None
    left = max(ox for _, ox, _ in parts)
    top = max(oy for _, _, oy in parts)
    right = max(im.width - ox for im, ox, _ in parts)
    bottom = max(im.height - oy for im, _, oy in parts)
    out = Image.new("RGBA", (left + right, top + bottom))
    for im, ox, oy in parts:
        out.alpha_composite(im, (left - ox, top - oy))
    box = out.getbbox()
    if not box:
        return None
    return out.crop(box), left - box[0], top - box[1]


class Atlas:
    """Packs frames into one image with a shelf packer and writes a .sheet."""

    def __init__(self, width=2048):
        self.width = width
        self.frames = []  # (image, anchor x, anchor y)
        self.index = {}   # key -> frame number

    def add(self, key, frame):
        if key not in self.index:
            self.index[key] = len(self.frames)
            self.frames.append(frame)
        return self.index[key]

    def write(self, sheet_path, image_name, note="Generated by tools/content/import_flare.py from Flare art (CC-BY-SA 3.0)."):
        """Frames added under a string key carry it as their name, so a
        skin (docs/content.md) can name them."""
        order = sorted(range(len(self.frames)), key=lambda i: -self.frames[i][0].height)
        x = y = shelf = 0
        placed = [None] * len(self.frames)
        for i in order:
            im = self.frames[i][0]
            if x + im.width > self.width:
                x, y, shelf = 0, y + shelf, 0
            placed[i] = (x, y)
            x += im.width
            shelf = max(shelf, im.height)
        out = Image.new("RGBA", (self.width, max(1, y + shelf)))
        for (im, _, _), (px, py) in zip(self.frames, placed):
            out.paste(im, (px, py))
        out.save(sheet_path.parent / image_name, optimize=True)
        with open(sheet_path, "w", newline="\n") as f:
            f.write(f"# {note}\n")
            f.write(f"image={image_name}\n")
            names = {i: key for key, i in self.index.items() if isinstance(key, str)}
            for i, ((im, ax, ay), (px, py)) in enumerate(zip(self.frames, placed)):
                name = f",{names[i]}" if i in names else ""
                f.write(f"frame={px},{py},{im.width},{im.height},{ax},{ay}{name}\n")


def hero_layer_order(flare):
    order = {}
    names = {"S": 0, "SE": 1, "E": 2, "NE": 3, "N": 4, "NW": 5, "W": 6, "SW": 7}
    for line in read_lines(flare.find("engine/hero_layers.txt")):
        key, _, value = line.partition("=")
        if key == "layer":
            fields = value.split(",")
            order[names[fields[0]]] = fields[1:]
    return order


def build_character(out_dir, layers, charts, order=None):
    """layers: {slot: Layer}; order: our direction -> slots back to front."""
    out_dir.mkdir(parents=True, exist_ok=True)
    atlas = Atlas()
    lines = ["# Generated by tools/content/import_flare.py from Flare art (CC-BY-SA 3.0).", "sheet=Animation.sheet"]
    for chart, name, loop in charts:
        lines.append("")
        lines.append(f"[chart {chart} {'loop' if loop else 'once'}]")
        first = next(iter(layers.values()))
        seq = tick_sequence(first.anims[name])
        for d in range(8):
            fd = flare_direction(d)
            slots = order[d] if order else list(layers)
            frames = []
            for f in seq:
                key = (name, f, fd)
                if key not in atlas.index:
                    image = composite([layers[s].cell(name, f, fd) for s in slots if s in layers])
                    if image is None:
                        image = (Image.new("RGBA", (1, 1)), 0, 0)
                    atlas.add(key, image)
                frames.append(atlas.index[key])
            lines.append(f"{d}=" + ",".join(map(str, frames)))
    atlas.write(out_dir / "Animation.sheet", "Animation.png")
    (out_dir / "Animation.anim").write_text("\n".join(lines) + "\n", newline="\n")


# --- Maps ----------------------------------------------------------------

def parse_tileset(flare, rel):
    """Flare tileset definition -> {tile id: (image, x, y, w, h, ox, oy)}."""
    tiles, image = {}, None
    for line in read_lines(flare.find(rel)):
        key, _, value = line.partition("=")
        if key == "img":
            image = Image.open(flare.find(value)).convert("RGBA")
        elif key == "tile":
            t, x, y, w, h, ox, oy = map(int, value.split(","))
            tiles[t] = (image, x, y, w, h, ox, oy)
    return tiles


def parse_map(path):
    m = {"header": {}, "layers": {}, "enemies": [], "events": []}
    section, data_rows, layer_type, block = None, None, None, None
    for raw in path.read_text(encoding="utf-8").splitlines():
        line = raw.strip()
        if line.startswith("#"):
            continue
        if line.startswith("["):
            section = line.strip("[]")
            data_rows, layer_type = None, None
            block = {}
            if section == "enemy":
                m["enemies"].append(block)
            elif section == "event":
                m["events"].append(block)
            continue
        if data_rows is not None:
            if not line:
                m["layers"][layer_type] = data_rows
                data_rows = None
            else:
                data_rows.append([int(v) for v in line.rstrip(",").split(",")])
            continue
        if not line:
            continue
        key, _, value = line.partition("=")
        if section == "header":
            m["header"][key] = value
        elif section == "layer":
            if key == "type":
                layer_type = value
            elif key == "data":
                data_rows = []
        elif block is not None:
            block.setdefault(key, value)
    if data_rows is not None:
        m["layers"][layer_type] = data_rows
    return m


def import_maps(flare, out):
    maps = {name: parse_map(flare.find(f"maps/{name}.txt")) for name in MAPS}
    number = {name: i + 1 for i, name in enumerate(MAPS)}
    category = {c: kind for kind, e in ENEMIES.items() for c in e["categories"]}
    # Entry 0 of each map is its start; every exit arriving from another
    # imported map adds an entry where it lands.
    entries = {name: [tuple(map(int, maps[name]["header"]["hero_pos"].split(",")))] for name in MAPS}
    exits = {name: [] for name in MAPS}
    for name, m in maps.items():
        for ev in m["events"]:
            if "intermap" not in ev:
                continue
            target, tx, ty = ev["intermap"].split(",")
            target = Path(target).stem
            if target not in maps:
                continue
            entries[target].append((int(tx), int(ty)))
            x, y, w, h = map(int, ev["location"].split(","))
            exits[name].append((x, y, w, h, number[target], len(entries[target]) - 1))

    for name, m in maps.items():
        width, height = int(m["header"]["width"]), int(m["header"]["height"])
        tiles = parse_tileset(flare, m["header"]["tileset"])
        for t in SKIP_TILES.get(m["header"]["tileset"], ()):
            tiles.pop(t, None)
        atlas = Atlas()

        def pattern(t):
            if t not in tiles:
                return None
            image, x, y, w, h, ox, oy = tiles[t]
            return atlas.add(t, (image.crop((x, y, x + w, y + h)), ox, oy))

        ground = m["layers"].get("background")
        objects = m["layers"].get("object", [])
        collision = m["layers"].get("collision")
        rows = []
        for ty in range(height):
            row = []
            for tx in range(width):
                p = pattern(ground[ty][tx]) if ground and ground[ty][tx] else None
                row.append("." if p is None else f"0:{p}")
            rows.append(" ".join(row))
        objs = []
        for ty, line in enumerate(objects):
            for tx, t in enumerate(line):
                p = pattern(t) if t else None
                if p is not None:
                    objs.append(f"{tx * TILE_WORLD + TILE_WORLD // 2},{ty * TILE_WORLD + TILE_WORLD // 2},0,{p}")

        per = TILE_WORLD // JUDGE_CELL
        blocked = [[collision is not None and collision[ty][tx] in (1, 2, 3, 4) for tx in range(width)] for ty in range(height)]
        judge = ["".join("x" if blocked[jy // per][jx // per] else "." for jx in range(width * per)) for jy in range(height * per)]

        name_dir = out / "Map"
        name_dir.mkdir(parents=True, exist_ok=True)
        atlas.write(name_dir / f"{name}.sheet", f"{name}.png")
        with open(name_dir / f"{name}.txt", "w", newline="\n") as f:
            f.write(f"# {m['header'].get('title', name)}: generated by tools/content/import_flare.py\n")
            f.write(f"# from Flare's maps/{name}.txt (CC-BY-SA 3.0).\n")
            f.write(f"size={width},{height}\ntiles={TILE_WORLD}\ncell={JUDGE_CELL},{JUDGE_CELL}\n")
            f.write(f"judge=0,0,{width * per},{height * per}\nbank=Map/{name}.sheet\n\n[ground]\n")
            f.write("\n".join(rows) + "\n\n[judge]\n" + "\n".join(judge) + "\n\n[objects]\n" + "\n".join(objs) + "\n")

        # Monsters: each Flare enemy group places its smallest number of
        # monsters on free tiles inside its area, the same way every time.
        rng = random.Random(name)
        lines = [f"# {m['header'].get('title', name)}: generated by tools/content/import_flare.py", f"title={m['header'].get('title', name)}", f"map={name}"]
        for i, (tx, ty) in enumerate(entries[name]):
            lines.append(f"entry={i},{tx * TILE_WORLD + TILE_WORLD // 2},{ty * TILE_WORLD + TILE_WORLD // 2},0")
        for group in m["enemies"]:
            kind = category.get(group.get("category"))
            if kind is None:
                continue
            e = ENEMIES[kind]
            x, y, w, h = map(int, group["location"].split(","))
            count = int(group.get("number", "1").split(",")[0])
            radius = int(group.get("wander_radius", "3"))
            free = [(tx, ty) for ty in range(y, y + h) for tx in range(x, x + w)
                    if 0 <= tx < width and 0 <= ty < height and not blocked[ty][tx]]
            for tx, ty in rng.sample(free, min(count, len(free))):
                lines.append(f"enemy={kind},{tx * TILE_WORLD + TILE_WORLD // 2},{ty * TILE_WORLD + TILE_WORLD // 2},"
                             f"{rng.randrange(8)},{e['hp']},{e['attack']},{max(1, radius) * TILE_WORLD},{e['ai']},"
                             f"{e['level']},{e['gold'][0]},{e['gold'][1]},{e['gold'][2]}")
        for cls, code, x, y in MAP_ITEMS.get(name, []):
            lines.append(f"item={cls},{code},{x},{y}")
        for x, y, w, h, target, entry in exits[name]:
            lines.append(f"exit={x * TILE_WORLD},{y * TILE_WORLD},{(x + w) * TILE_WORLD},{(y + h) * TILE_WORLD},{target},{entry}")
        scen = out / "Scenario" / f"{number[name]:08d}"
        scen.mkdir(parents=True, exist_ok=True)
        (scen / "scenario.txt").write_text("\n".join(lines) + "\n", newline="\n")
        print(f"{name}: {len(atlas.frames)} tiles, {len(objs)} objects, "
              f"{sum(l.startswith('enemy=') for l in lines)} monsters, {len(exits[name])} exits")


def main():
    parser = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    parser.add_argument("flare", help="flare-game checkout")
    parser.add_argument("out", help="output folder, normally content/")
    args = parser.parse_args()
    flare = Flare(args.flare)
    out = Path(args.out)
    for sub in ("Map", "Scenario", "Character", "Player"):
        shutil.rmtree(out / sub, ignore_errors=True)
    out.mkdir(parents=True, exist_ok=True)

    order = hero_layer_order(flare)
    layers = {slot: Layer(flare, path) for slot, path in PLAYER_LAYERS.items()}
    build_character(out / "Player" / "Male", layers, PLAYER_CHARTS, order)
    (out / "Player" / "Male" / "Animation.anim").rename(out / "Player" / "Male" / "Animation00.anim")
    print("player done")
    for kind, e in ENEMIES.items():
        build_character(out / "Character" / "ENEMY" / f"{kind:08d}", {"body": Layer(flare, e["anim"])}, ENEMY_CHARTS)
        print(f"{e['name']} done")
    import_maps(flare, out)

    shutil.copy(HERE / "ai.txt", out / "ai.txt")
    shutil.copy(Path(args.flare) / "CREDITS.txt", out / "CREDITS-flare.txt")
    with open(out / "content.txt", "w", newline="\n") as f:
        f.write("# The open content pack: see docs/content.md and content/README.md.\n")
        f.write(f"name=Flare demo\nstart={MAPS[0]}\nprojection={PROJECTION[0]},{PROJECTION[1]}\n")


if __name__ == "__main__":
    main()
