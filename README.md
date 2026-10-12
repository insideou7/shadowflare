# ShadowFlare engine

An open source reimplementation of the ShadowFlare engine in C++20 and SDL2.

This repository contains **none of the original game's data**. There are two
ways to play:

- With your own copy of ShadowFlare: copy its files into a `game/` folder at
  the repository root (git ignores it), or point the engine elsewhere with
  `--data`. The engine reads them at runtime.
- Without it: the engine falls back to the open replacement content in
  [content/](content/): our own ShadowFlare-style art, made from scratch
  in Blender, with seven areas from a village meadow down to a castle.
  See [docs/content.md](docs/content.md).

## Status

A walkable map viewer. The engine loads any level from the original game
(ground tiles, walls, trees and props) and puts the player character in
it, animated in all eight directions, and walls and trees block the way.
Townspeople and monsters stand where the game's scenario files put them,
and walking into an exit takes you to the next area, following the
game's own scripts, so the world can be explored on foot.
Monsters behave as the game's own AI data tells them: they wander, close
in, back off and strike or cast; click a monster to punch it.
Monster health and attack come from the scenario files. Items come from
the game's item table (`Item.Ibn`, read with the help of the player's own
`ShadowFlare.exe`): they lie where the scenarios put them, monsters drop
gold and items, and what the hero wears shows on them and adds to attack
and defense. The damage formula and spell effects are still placeholders.
Story events, levels, shops, sound and menus are not in yet. See [docs/formats.md](docs/formats.md)
for what has been decoded so far.

Controls: hold the left mouse button to walk towards the cursor (or to
attack the monster under it), or use
the arrow keys or WASD. N/P (or Page Down/Up) switch to the next/previous
map, Home returns to the start point, J shows the walkability grid, Esc opens the menu (resume, save, load, new game, quit).
Walk onto an item to pick it up. I opens the inventory: click an item to
pick it up and click again to put it in the bag or an equipment slot;
right-click a potion to drink it; an item dropped outside the panel lands
at the hero's feet. F5 saves the game and F9 loads it. The save is a small
text file, `save.txt` in SDL's settings folder for the game (on Linux
`~/.local/share/ShadowFlare/open/`, on Windows `%APPDATA%\ShadowFlare\open\`).

## Building

Needs CMake 3.20+, a C++20 compiler and SDL2. If SDL2 is not installed,
CMake downloads and builds it.

    cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug
    cmake --build build
    ./build/shadowflare            # reads ./game, or ./content if there is no game

Options:

- `--data <path>` folder with the original game files (default `game`, then
  `content`). It can be the install folder itself or any folder above it, or
  an open content pack.
- `--map <name>` map to open first, e.g. `c02_00` (the default with the original
  game) or `d02_01`
- `--frames <n>` quit after n frames
- `--ticks <n>` run exactly n simulation ticks as fast as possible, then quit;
  the same command always gives the same result, so use this for smoke tests
- `--screenshot <file.bmp>` save the last frame when quitting
- `--auto-fight` make the player attack nearby monsters on their own, for smoke tests
- `--show-judge` start with the walkability grid shown
- `--at <x>,<y>` start at a point on the map, in map pixels (moved to the nearest walkable spot)
- `--walk <x>,<y>` start walking to a point given in screen pixels from the player
- `--give <class>:<code>[:<count>]` start with an item in the bag (class 0 weapon,
  1 armour, 2 accessory, 3 consumable, 4 special; `--give 4:0:500` is 500 gold)
- `--equip <class>:<code>` start wearing an item, e.g. `--equip 0:0` (Short Sword)
- `--drop <class>:<code>[:<count>]` put an item on the ground next to the player
- `--inventory` start with the inventory panel open
- `--save-file <path>` save to and load from this file instead
- `--load` start from the saved game; `--save-at-end` saves when the run ends
- `--level <n>` start the hero at level n
- `--no-menu` skip the main menu (it is also skipped when any of the options above is given)

Run the unit tests with `ctest --test-dir build`.

### Browser build (later)

The main loop runs one frame per call, so the same code builds with Emscripten:

    emcmake cmake -S . -B build-web
    cmake --build build-web

Players will still need to supply their own game files.

## Layout

    src/core/       logging and shared helpers
    src/platform/   OS boundary: file access (all data goes through FileSystem)
    src/formats/    readers for the original data files (no SDL dependency)
    src/render/     turning decoded sprites into textures
    src/logic/      game rules without SDL: scripts, AI choice, movement
    src/game/       maps, game state, simulation tick and drawing
    third_party/    stb_image (PNG decoding for the open content)
    content/        open replacement content (CC-BY-SA 3.0)
    tests/          unit tests that run without game data
    tools/          inventory script, reverse engineering helpers, the
                    Blender art scripts (tools/art) and a Flare importer
    docs/           file format notes

## Surveying the original files

    python3 tools/inventory.py /path/to/Shadowflare

lists every file with its size and detected type. `tools/re/` has Python
decoders for the formats in `docs/formats.md`, for example

    python3 tools/re/drawscene.py /path/to/ShadowFlare/Map c02_00 town.png 0 0 2

renders a whole map to an image.

## Licence

The engine and tools are under the MIT licence; see [LICENSE](LICENSE).
The open content in [content/](content/) is our own art, under CC-BY-SA
3.0; see [content/README.md](content/README.md). stb_image is public domain (or MIT).

The original game is copyrighted and is not covered by this licence. Do
not commit its files, extracted assets or decompiled code to this
repository.
