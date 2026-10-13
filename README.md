# ShadowFlare engine

An open source reimplementation of the ShadowFlare engine in C++20 and SDL2.

This repository contains **none of the original game's data**. There are two
ways to play:

- With your own copy of ShadowFlare: copy its files into a `game/` folder at
  the repository root (git ignores it), or point the engine elsewhere with
  `--data`. The engine reads them at runtime and draws the game with its
  own art. F12 (or Art in the Settings Menu) swaps to the art in
  [content/](content/) for its maps, people, monsters and hero and back
  again while you play, like a remaster's classic-graphics button.
  `--our-art` starts with it.
- Without it: the engine falls back to the open replacement content in
  [content/](content/): our own ShadowFlare-style art, made from scratch
  in Blender, with the original's first area (Remote Town and the fields,
  forests and dungeons round it) laid out with our art.
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
With the original data the scenario scripts run: click a person to talk,
pick an answer with the mouse or the arrow keys and Enter, and shops,
healers and the class instructors work as their scripts say. The hero
becomes a warrior at level 5 and is offered the hunter or wizard by how
they kill monsters, as in the original; S opens the status window with its
Change Career button. Heroes learn spells as they level up, as in the
original (see [docs/magic.md](docs/magic.md)). Sound is not in yet. See [docs/formats.md](docs/formats.md)
for what has been decoded so far.

Controls follow the original manual (see [docs/controls.md](docs/controls.md)):
hold the left mouse button to walk towards the cursor (or to attack the
monster under it), hold Ctrl to run or press R to switch between walking
and running, and hold Shift to stop or to attack in place. F1-F8 choose a
spell from the bar at the bottom right and F9 the attack; the right mouse
button casts the spell (or attacks) at the cursor; casting a shield or
Berserker again turns it off. With the original game's files the hero has
a dog: Space (or a click on its gauge) sends it into battle or calls it back
(see [docs/partners.md](docs/partners.md)). S opens the status window, I the inventory,
H the keyboard help, and Print Screen saves a screen shot. Esc closes the
open window, or opens the Settings Menu: the original's options (screen
mode, see-through player and shadows, click range and priority), original
mode, help, and save and return to title or save and exit.
Walk onto an item to pick it up. In the inventory, click an item to
pick it up and click again to put it in the bag or an equipment slot;
right-click a potion to drink it; an item dropped outside the panel lands
at the hero's feet. F10 saves the game and F11 loads it. As in the original,
each hero has their own save, up to six: `save1.txt` to `save6.txt` (small
text files, each with a `.bmp` picture of where it was saved) in SDL's
settings folder for the game (on Linux `~/.local/share/ShadowFlare/open/`, on
Windows `%APPDATA%\ShadowFlare\open\`). Continue on the title opens the Load
Data screen, which lists them and can delete one. A `save.txt` from an older
version moves into the first free slot.

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
- `--our-art` draw the original game with our art from content/ rather than
  its own (`--original-art` starts with the original's). Without either,
  the Art row of the Settings Menu (F12 swaps it) decides.
- `--skin <path>` draw the original game with another content pack's art
- `--map <name>` map or scenario number to open first, e.g. `c02_00`, `d02_01`
  or `0`. New games begin where the original's do, in Remote Town (scenario 0;
  `10000000` in the open content)
- `--frames <n>` quit after n frames
- `--ticks <n>` run exactly n simulation ticks as fast as possible, then quit;
  the same command always gives the same result, so use this for smoke tests
- `--screenshot <file.bmp>` save the last frame when quitting
- `--record-sound <file.wav>` save what the game plays (sound effects and music) instead of sending it to the speakers
- `--auto-fight` make the player attack nearby monsters on their own, for smoke tests
- `--partner <kind>` bring companion `kind` (0 Kerberos ... 5 Hawk; Ash ... Kestrel in the open pack), ACTIVE
- `--show-judge` start with the walkability grid shown
- `--at <x>,<y>` start at a point on the map, in map pixels (moved to the nearest walkable spot)
- `--walk <x>,<y>` start walking to a point given in screen pixels from the player
- `--give <class>:<code>[:<count>]` start with an item in the bag (class 0 weapon,
  1 armour, 2 accessory, 3 consumable, 4 special; `--give 4:0:500` is 500 gold)
- `--equip <class>:<code>` start wearing an item, e.g. `--equip 0:0` (Short Sword);
  both take a fourth number for how worn the item starts, e.g. `--equip 0:0:1:150`
- `--drop <class>:<code>[:<count>]` put an item on the ground next to the player
- `--inventory` start with the inventory panel open
- `--status` start with the status window open
- `--save-file <path>` save to and load from this one file instead of the six slots
- `--save-folder <dir>` keep the six save slots in this folder; `--load-data`
  start on the Load Data screen
- `--load` start from the saved game; `--save-at-end` saves when the run ends
- `--level <n>` start the hero at level n
- `--class <warrior|hunter|wizard>` the class `--level` gives the hero from level 5 on
- `--hurt <percent>` drop the hero's HP to that share four seconds in, and
  `--experience <percent>` start with the EXP gauge that full (screenshots of the panel)
- `--talk <id>` walk up to a character and speak to them (in-game id, e.g. 12000001)
- `--answers <a,b,...>` answer the next messages with these options (-1 = none)
- `--use <id>` use a scenario object (a chest, lever or door) from where the
  player stands, as clicking it does; `--hold <id>` holds the left button on
  one (a Brain Globe counts down while the hero is within reach)
- `--save-flag <id>=<value>` set a SAVE story flag first, e.g. `6=2` to open Malse's shop
- `--quest-flag <id>=<state>` set a mission's flag first (1 taken, 2 cleared)
- `--missions` start with the mission list open; `--missions=<n>` with mission n's description open
- `--warp-flag <gate>=<value>` mark a warp gate found (`--warp-flag all` for every gate),
  `--warp-gate[=<page>]` start with the GATE window open, and `--warp-to <row>` pick that
  row of it, e.g. `--map 0 --warp-flag all --warp-gate=2 --warp-to 5` to go to the
  South Camp of Yugunos (episode 3)
- `--original` play in original mode: the original game's rules, bugs included
  (also in the menu; off by default)
- `--cast <slot>:<x>,<y>` cast the spell on F1-F8 (slot 1-8) at a point given in
  screen pixels from the player, e.g. `--level 3 --cast 1:120,0`
- `--spell <n>[:<level>]` teach spell n (0-21, the original's numbering) at
  that level and put it on F1, e.g. `--spell 3:10 --cast 1:200,60` for Plasma
- `--no-menu` skip the main menu (it is also skipped when any of the options above is given)
- `--options` / `--help-window` start with the Settings Menu or the Help window open
- `--options-file <path>` read and write the options there instead of `options.txt`

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
    content/        open replacement content (MIT)
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

## License

The engine, tools and the open content in [content/](content/) (our own
art, maps and text) are under the MIT license; see [LICENSE](LICENSE).
stb_image is public domain (or MIT).

The original game is copyrighted and is not covered by this license. Do
not commit its files, extracted assets or decompiled code to this
repository.
