# Browser build

The engine builds to WebAssembly with Emscripten from the same code as the
native game. The browser build has no code of its own in `src/` apart from a
few lines in `main.cpp` (marked `__EMSCRIPTEN__`).

## Building

Needs the [Emscripten SDK](https://emscripten.org/docs/getting_started/downloads.html)
(tested with emcc 6.0.12). Emscripten downloads its own SDL2 port the first
time.

    emcmake cmake -S . -B build-web -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=OFF
    cmake --build build-web

This makes four files in `build-web/`, which go on any static web server
together:

| File | What it is | Size |
| --- | --- | --- |
| `shadowflare.html` | the page (from `web/shell.html`) | 5 KB |
| `shadowflare.js` | Emscripten's loader, with `web/pre.js` in it | 260 KB |
| `shadowflare.wasm` | the engine | 2 MB |
| `shadowflare.data` | the open content pack (`content/`) | 58 MB |

Browsers do not run WebAssembly from `file://`, so test with a local server:

    python3 -m http.server -d build-web 8000

and open <http://localhost:8000/shadowflare.html>.

If Emscripten cannot download the SDL2 port (for example behind a proxy that
blocks GitHub archives), clone it and point Emscripten at it:

    git clone --depth 1 -b release-2.32.10 https://github.com/libsdl-org/SDL.git sdl2-src
    EMCC_LOCAL_PORTS=sdl2=$PWD/sdl2-src cmake --build build-web

## The page

The start page offers two ways in:

- **Play the open game.** Runs the open content pack, which is preloaded
  into `/content` in the browser's memory.
- **Choose my ShadowFlare folder.** A folder picker for the player's own
  copy of the original. They can pick the game's folder (the one with `Map`
  and `System` in it) or any folder above it. The page finds the game in
  what was picked, copies that folder into `/originals` and starts the
  engine with `--data /originals`. The files are read by the browser on the
  player's computer and never sent anywhere. Our art (F12) still works,
  since the open pack is in `/content`.

"Keep the files in this browser for next time" (on by default) stores the
original files in the browser's IndexedDB, so the next visit shows **Play
with my saved files**, which loads them in about 2 seconds instead of
reading the folder again. **Forget my saved files** deletes them.

Folder pickers work in Chrome, Edge, Firefox and Safari on computers; phones
cannot pick folders.

## Saves and settings

SDL's settings folder in the browser is `/libsdl/ShadowFlare/open/`. The
page keeps it in IndexedDB: the six save slots, `options.txt` and the area
map's pictures all survive a reload. The engine asks the page to copy that
folder to IndexedDB every 120 frames (about 2 seconds) and when the game
quits; the page also copies it when the tab is closed. Saving with F10 and
closing the tab at once can lose that last save.

Quitting from the title menu or the Settings Menu goes back to the start
page.

## How it differs from the native build

- `main()` hands the frame function to `emscripten_set_main_loop_arg` and
  returns; the browser calls `frame()` once per animation frame. The `App`
  is `static` there, because `main()`'s stack is reused after it returns.
- C++ exceptions use WebAssembly's own exceptions (`-fwasm-exceptions`),
  since the format readers report bad data by throwing.
- The JavaScript side (`web/pre.js`) mounts `/libsdl` and `/originals` as
  IndexedDB folders and syncs each on its own, so saving the game never
  rewrites the few hundred MB of original files.
- Sound goes through SDL's Web Audio output. Browsers start it after the
  first click or key press.

## Tested

In Chromium 1194 (Playwright, software WebGL):

- The open game: title, Create New Character, Remote Town, walking.
- The original files picked from a folder above the game (316 MB, read
  and kept in about 34 seconds), playing in Remote Town, F10 save, reload,
  **Play with my saved files**, and Continue lit up on the title.
- Quit returns to the start page.

Playwright's `headless_shell` build crashes as soon as the game starts; the
full Chromium build does not, and neither do normal browsers we know of.
Firefox and Safari are not tested yet.

## Not done yet

- Publishing it. The four files can go on GitHub Pages next to
  `docs/index.html` (`docs/play/`), but the 58 MB data file would then be in
  the repository, and Pages needs the repository public or a paid plan.
- Online multiplayer (a separate design).
