#!/bin/sh
# Installs Ghidra 12.1.4 into ~/.cache/rea/ghidra so REA can decompile
# ShadowFlare.exe and its DLLs. Needs a JDK 21+ (java and javac on PATH).
# Run it once per machine or cloud session before native analysis, then
# restart Claude Code (or run /mcp) so the rea server sees it.
set -eu
TAG=Ghidra_12.1.4_build
DEST="$HOME/.cache/rea/ghidra"

if [ -x "$DEST/support/analyzeHeadless" ]; then
  echo "Ghidra already installed in $DEST"; exit 0
fi
command -v javac >/dev/null || { echo "Install a JDK 21 or newer first (javac not found)." >&2; exit 1; }

url=$(curl -fsSL "https://api.github.com/repos/NationalSecurityAgency/ghidra/releases/tags/$TAG" |
  python3 -c 'import json,sys; print(next(a["browser_download_url"] for a in json.load(sys.stdin)["assets"] if a["name"].endswith(".zip")))')
tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT
echo "Downloading $url"
curl -fL --retry 3 -o "$tmp/ghidra.zip" "$url"
unzip -q "$tmp/ghidra.zip" -d "$tmp"
mkdir -p "$(dirname "$DEST")"
rm -rf "$DEST"
mv "$tmp"/ghidra_*_PUBLIC "$DEST"
echo "Installed Ghidra in $DEST"
