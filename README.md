# ShadowFlare engine

An open source reimplementation of the ShadowFlare engine in C++20 and SDL2.

This repository contains **no game data**. To play, you need your own copy of
the original game; the engine reads the files from it at runtime. Copy them
into a `game/` folder at the repository root (git ignores it), or point the
engine elsewhere with `--data`.

## Status

A walkable map viewer. The engine loads any level from the original game
(ground tiles, walls, trees and props) and puts the player character in
it, animated in all eight directions, and walls and trees block the way.
Townspeople and monsters stand where the game's scenario files put them,
but don't move, talk or fight yet. Items, sound and menus are not in yet. See [docs/formats.md](docs/formats.md)
for what has been decoded so far.

Controls: hold the left mouse button to walk towards the cursor, or use
the arrow keys or WASD. N/P (or Page Down/Up) switch to the next/previous
map, Home returns to the start point, J shows the walkability grid, Esc quits.

## Building

Needs CMake 3.20+, a C++20 compiler and SDL2. If SDL2 is not installed,
CMake downloads and builds it.

    cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug
    cmake --build build
    ./build/shadowflare            # reads ./game by default

Options:

- `--data <path>` folder with the original game files (default `game`). It can be
  the install folder itself or any folder above it.
- `--map <name>` map to open first, e.g. `c02_00` (default) or `d02_01`
- `--frames <n>` quit after n frames, for smoke tests
- `--screenshot <file.bmp>` save the last frame when quitting
- `--show-judge` start with the walkability grid shown
- `--walk <x>,<y>` start walking to a point given in screen pixels from the player

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
    src/game/       maps, game state, simulation tick and drawing
    tests/          unit tests that run without game data
    tools/          inventory script and reverse engineering helpers (Python)
    docs/           file format notes

## Surveying the original files

    python3 tools/inventory.py /path/to/Shadowflare

lists every file with its size and detected type. `tools/re/` has Python
decoders for the formats in `docs/formats.md`, for example

    python3 tools/re/drawscene.py /path/to/ShadowFlare/Map c02_00 town.png 0 0 2

renders a whole map to an image.

## Legal

The original game is copyrighted. Do not commit its files, extracted
assets or decompiled code to this repository.
