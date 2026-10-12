# REA (reverse engineering for agents)

[REA](https://github.com/morluto/rea) is an MCP server and CLI that lets
Claude Code inspect ShadowFlare.exe and its RKC_*.dll modules: PE layout,
imports and exports, resources and strings, and, with Ghidra installed,
function pseudocode, assembly, calls and cross-references. It is MIT licensed
and is not vendored here; `npx` fetches the pinned release.

What this repo sets up:

- `.mcp.json` registers the `rea` server, started by `tools/rea/mcp.mjs`
  (pins rea-agents 6.4.0). `.claude/settings.json` enables it, so Claude Code
  sessions in this repo get the `mcp__rea__*` tools without running
  `rea setup`.
- `.claude/skills/reverse-engineer-anything/` is REA's own skill for that
  release, telling Claude which tool to use for which target.
- `tools/rea/install-ghidra.sh` installs Ghidra 12.1.4 into
  `~/.cache/rea/ghidra`, which `mcp.mjs` uses unless `GHIDRA_INSTALL_DIR` is
  already set. Static PE inspection works without it; decompiling needs it.

Requirements: Node.js 22.19+ (or 24.11+), and for decompiling a JDK 21+ and
Ghidra 12.1.x.

## Using it on the game

Point REA at your own copy of the game. Extract the binaries somewhere outside
the repo (or use `game/`, which is gitignored), for example:

    unzip -j game.zip 'game/Shadowflare/ShadowFlare/*.exe' \
        'game/Shadowflare/ShadowFlare/*.dll' -d /tmp/sf-bin

Then ask Claude to inspect or decompile a function in `/tmp/sf-bin/...`, or
use the CLI directly:

    npx -y rea-agents@6.4.0 inspect-artifact /tmp/sf-bin/ShadowFlare.exe --json

Never commit the game files or large decompiler dumps of them. Notes on what
the code does belong in docs/; the older objdump helpers are in tools/re/.

## On your own PC

Opening the repo in Claude Code picks up `.mcp.json`; approve the `rea` server
if asked. For decompiling, install a JDK 21+ and Ghidra 12.1.x (on Windows,
unzip Ghidra and set `GHIDRA_INSTALL_DIR` to its folder), then restart Claude
Code. `npx -y rea-agents@6.4.0 doctor` shows what is missing.
