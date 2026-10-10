# ShadowFlare engine

An open source reimplementation of the ShadowFlare engine in C++20 and SDL2.

This repository contains **no game data**. To play, you need your own copy of
the original game; the engine reads the files from it at runtime. Copy them
into a `game/` folder at the repository root (git ignores it), or point the
engine elsewhere with `--data`.

## Status

Skeleton only: a window, a fixed-rate game loop and a case-insensitive data
loader. Nothing from the original game is decoded yet.

## Building

Needs CMake 3.20+, a C++20 compiler and SDL2. If SDL2 is not installed,
CMake downloads and builds it.

    cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug
    cmake --build build
    ./build/shadowflare            # reads ./game by default

Options:

- `--data <path>` folder with the original game files (default `game`)
- `--frames <n>` quit after n frames, for smoke tests

### Browser build (later)

The main loop runs one frame per call, so the same code builds with Emscripten:

    emcmake cmake -S . -B build-web
    cmake --build build-web

Players will still need to supply their own game files.

## Layout

    src/core/       logging and shared helpers
    src/platform/   OS boundary: file access (all data goes through FileSystem)
    src/game/       game state, simulation tick and rendering
    tools/          reverse engineering helpers (Python)

## Surveying the original files

    python3 tools/inventory.py /path/to/Shadowflare

lists every file with its size and detected type.

## Legal

The original game is copyrighted. Do not commit its files, extracted
assets or decompiled code to this repository.
